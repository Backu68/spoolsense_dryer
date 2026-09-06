#include "SpoolmanClient.h"

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

void SpoolmanClient::setBaseUrl(const String& baseUrl) {
    baseUrl_ = baseUrl;
    baseUrl_.trim();

    while (baseUrl_.endsWith("/")) {
        baseUrl_.remove(baseUrl_.length() - 1);
    }
}

bool SpoolmanClient::isConfigured() const {
    return !baseUrl_.isEmpty();
}

String SpoolmanClient::cleanExtraValue(const String& value) {
    String result = value;
    result.trim();

    // SpoolSense historically stores nfc_id as a quoted string
    // inside Spoolman's text extra field, e.g. "\"04AABBCC\"".
    if (
        result.length() >= 2 &&
        result.charAt(0) == '"' &&
        result.charAt(result.length() - 1) == '"'
    ) {
        result = result.substring(1, result.length() - 1);
    }

    return result;
}

int SpoolmanClient::readExtraInt(JsonVariantConst value) {
    if (value.is<int>()) {
        return value.as<int>();
    }

    if (value.is<float>()) {
        return static_cast<int>(value.as<float>() + 0.5f);
    }

    if (value.is<const char*>()) {
        String text = cleanExtraValue(value.as<const char*>());
        return static_cast<int>(text.toFloat() + 0.5f);
    }

    return 0;
}

uint8_t SpoolmanClient::extractPositiveInts(
    const String& text,
    int* values,
    uint8_t maxValues
) {
    if (values == nullptr || maxValues == 0) {
        return 0;
    }

    uint8_t count = 0;
    String token;
    bool decimalSeen = false;

    auto flushToken = [&]() {
        if (token.isEmpty() || count >= maxValues) {
            token = "";
            decimalSeen = false;
            return;
        }

        float parsed = token.toFloat();
        if (parsed > 0.0f) {
            values[count++] = static_cast<int>(parsed + 0.5f);
        }

        token = "";
        decimalSeen = false;
    };

    for (size_t i = 0; i < text.length(); ++i) {
        char c = text.charAt(i);

        if (c >= '0' && c <= '9') {
            token += c;
            continue;
        }

        if (c == '.' && !token.isEmpty() && !decimalSeen) {
            token += c;
            decimalSeen = true;
            continue;
        }

        flushToken();
        if (count >= maxValues) {
            break;
        }
    }

    if (count < maxValues) {
        flushToken();
    }

    return count;
}

void SpoolmanClient::readDryTemperatureProfile(
    JsonObjectConst extra,
    DryerSpoolInfo& spool
) {
    int parsedMin = 0;
    int parsedPreferred = 0;
    int parsedMax = 0;

    JsonVariantConst dryTemp = extra["dry_temp"];

    if (dryTemp.is<int>() || dryTemp.is<float>()) {
        parsedPreferred = readExtraInt(dryTemp);
        parsedMin = parsedPreferred;
        parsedMax = parsedPreferred;
    } else if (dryTemp.is<const char*>()) {
        String text = cleanExtraValue(dryTemp.as<const char*>());
        int values[3] = {0, 0, 0};
        uint8_t count = extractPositiveInts(text, values, 3);

        if (count == 1) {
            parsedMin = values[0];
            parsedPreferred = values[0];
            parsedMax = values[0];
        } else if (count >= 2) {
            parsedMin = min(values[0], values[count - 1]);
            parsedMax = max(values[0], values[count - 1]);
            parsedPreferred = (parsedMin + parsedMax + 1) / 2;

            // A three-number form such as "55/60/65" may be used to express
            // min/preferred/max explicitly.
            if (count >= 3 && values[1] >= parsedMin && values[1] <= parsedMax) {
                parsedPreferred = values[1];
            }
        }
    }

    // Explicit fields take precedence when present. Supporting both forms lets
    // installations use either one range-valued custom field or three separate
    // custom fields without changing firmware.
    int explicitMin = readExtraInt(extra["dry_temp_min"]);
    int explicitPreferred = readExtraInt(extra["dry_temp_preferred"]);
    int explicitMax = readExtraInt(extra["dry_temp_max"]);

    if (explicitMin > 0) {
        parsedMin = explicitMin;
    }
    if (explicitMax > 0) {
        parsedMax = explicitMax;
    }
    if (explicitPreferred > 0) {
        parsedPreferred = explicitPreferred;
    }

    if (parsedMin <= 0 && parsedPreferred > 0) {
        parsedMin = parsedPreferred;
    }
    if (parsedMax <= 0 && parsedPreferred > 0) {
        parsedMax = parsedPreferred;
    }

    if (parsedMin > 0 && parsedMax > 0 && parsedMin > parsedMax) {
        int swap = parsedMin;
        parsedMin = parsedMax;
        parsedMax = swap;
    }

    if (parsedPreferred <= 0 && parsedMin > 0 && parsedMax > 0) {
        parsedPreferred = (parsedMin + parsedMax + 1) / 2;
    }

    if (parsedPreferred > 0 && parsedMin > 0 && parsedPreferred < parsedMin) {
        parsedPreferred = parsedMin;
    }
    if (parsedPreferred > 0 && parsedMax > 0 && parsedPreferred > parsedMax) {
        parsedPreferred = parsedMax;
    }

    spool.dryTempMinC = parsedMin;
    spool.dryTempC = parsedPreferred;
    spool.dryTempMaxC = parsedMax;
}

bool SpoolmanClient::lookupByUid(
    const String& uid,
    DryerSpoolInfo& spool,
    String& error
) {
    spool = DryerSpoolInfo{};
    error = "";

    if (!isConfigured()) {
        error = "Spoolman URL not configured";
        return false;
    }

    String url =
        baseUrl_ +
        "/api/v1/spool?extra.nfc_id=" +
        uid;

    WiFiClient client;
    HTTPClient http;

    http.setTimeout(8000);

    if (!http.begin(client, url)) {
        error = "HTTP begin failed";
        return false;
    }

    int httpCode = http.GET();

    if (httpCode != 200) {
        error = "Spoolman HTTP " + String(httpCode);
        http.end();
        return false;
    }

    JsonDocument document;

    DeserializationError jsonError =
        deserializeJson(document, http.getStream());

    http.end();

    if (jsonError) {
        error = "Invalid Spoolman JSON";
        return false;
    }

    if (!document.is<JsonArray>()) {
        error = "Unexpected Spoolman response";
        return false;
    }

    JsonArrayConst spools = document.as<JsonArrayConst>();

    for (JsonObjectConst item : spools) {
        bool archived = item["archived"] | false;

        if (archived) {
            continue;
        }

        String storedUid;

        if (!item["extra"]["nfc_id"].isNull()) {
            storedUid =
                cleanExtraValue(
                    item["extra"]["nfc_id"].as<String>()
                );
        }

        // Important safety check:
        // Do not trust the API filter alone. If nfc_id is not actually
        // present/matching, this is NOT our spool.
        if (!storedUid.equalsIgnoreCase(uid)) {
            continue;
        }

        spool.found = true;
        spool.spoolId = item["id"] | -1;

        JsonObjectConst filament =
            item["filament"].as<JsonObjectConst>();

        spool.filamentId =
            filament["id"] | -1;

        spool.name =
            filament["name"] | "";

        spool.material =
            filament["material"] | "";

        if (!filament["vendor"].isNull()) {
            spool.vendor =
                filament["vendor"]["name"] | "";
        }

        JsonObjectConst filamentExtra =
            filament["extra"].as<JsonObjectConst>();

        readDryTemperatureProfile(filamentExtra, spool);

        spool.dryTimeHours =
            readExtraInt(
                filamentExtra["dry_time_hours"]
            );

        // If Spoolman doesn't have a product name, at least give
        // the display something useful.
        if (spool.name.isEmpty()) {
            spool.name = spool.material;
        }

        return true;
    }

    error = "UID not linked in Spoolman";
    return false;
}
