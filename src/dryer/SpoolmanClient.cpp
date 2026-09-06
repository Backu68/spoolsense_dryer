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
        return static_cast<int>(value.as<float>());
    }

    if (value.is<const char*>()) {
        String text = cleanExtraValue(value.as<const char*>());
        return text.toInt();
    }

    return 0;
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

        spool.dryTempC =
            readExtraInt(
                filament["extra"]["dry_temp"]
            );

        spool.dryTimeHours =
            readExtraInt(
                filament["extra"]["dry_time_hours"]
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