#include "DryerHomeAssistant.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_mac.h>

namespace {
String makeUniqueId(const char* deviceId, const String& suffix) {
    return "spoolsense_" + String(deviceId) + "_" + suffix;
}

String fallbackSpoolName(const DryerStationView& station) {
    if (!station.spool.name.isEmpty()) {
        return station.spool.name;
    }
    if (!station.spool.material.isEmpty()) {
        return station.spool.material;
    }
    return station.occupied ? "Unknown spool" : "Empty";
}
}

bool DryerHomeAssistant::begin(
    DryerConfig& config,
    StatusProvider statusProvider
) {
    config_ = &config;
    statusProvider_ = statusProvider;
    getDeviceId(deviceId_, sizeof(deviceId_));

    configured_ = config_->hasHomeAssistant();
    connected_ = false;
    lastMqttState_ = -1;
    reconnectDelayMs_ = 1000;
    lastReconnectAttemptMs_ = 0;
    lastPublishMs_ = 0;

    mqttClient_.setClient(wifiClient_);
    mqttClient_.setBufferSize(2048);
    mqttClient_.setKeepAlive(30);

    if (!configured_) {
        Serial.println("Home Assistant MQTT: not configured.");
        return true;
    }

    mqttClient_.setServer(
        config_->getHAMqttHost(),
        config_->getHAMqttPort()
    );

    Serial.print("Home Assistant MQTT: configured for ");
    Serial.print(config_->getHAMqttHost());
    Serial.print(":");
    Serial.println(config_->getHAMqttPort());
    return true;
}

void DryerHomeAssistant::loop() {
    if (!configured_ || config_ == nullptr || statusProvider_ == nullptr) {
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        connected_ = false;
        return;
    }

    uint32_t now = millis();

    if (!mqttClient_.connected()) {
        connected_ = false;

        if ((now - lastReconnectAttemptMs_) < reconnectDelayMs_) {
            return;
        }

        lastReconnectAttemptMs_ = now;
        if (reconnect()) {
            reconnectDelayMs_ = 1000;
            lastPublishMs_ = now;
        } else {
            reconnectDelayMs_ = min(
                reconnectDelayMs_ * 2UL,
                MAX_RECONNECT_DELAY_MS
            );
        }
        return;
    }

    connected_ = true;
    mqttClient_.loop();

    if ((now - lastPublishMs_) >= STATE_PUBLISH_INTERVAL_MS) {
        lastPublishMs_ = now;
        publishState();
    }
}

bool DryerHomeAssistant::isConfigured() const {
    return configured_;
}

bool DryerHomeAssistant::isConnected() const {
    return connected_;
}

int DryerHomeAssistant::getLastMqttState() const {
    return lastMqttState_;
}

bool DryerHomeAssistant::reconnect() {
    if (config_ == nullptr || !configured_) {
        return false;
    }

    String clientId = "spoolsense_" + String(deviceId_) + "_dryer";
    String lwt = availabilityTopic();

    bool ok = false;
    if (strlen(config_->getHAMqttUser()) > 0) {
        ok = mqttClient_.connect(
            clientId.c_str(),
            config_->getHAMqttUser(),
            config_->getHAMqttPassword(),
            lwt.c_str(),
            0,
            true,
            "offline"
        );
    } else {
        ok = mqttClient_.connect(
            clientId.c_str(),
            lwt.c_str(),
            0,
            true,
            "offline"
        );
    }

    lastMqttState_ = mqttClient_.state();
    connected_ = ok;

    if (!ok) {
        Serial.print("Home Assistant MQTT: connect failed, state ");
        Serial.println(lastMqttState_);
        return false;
    }

    Serial.println("Home Assistant MQTT: connected.");
    publishAvailability("online");
    publishDiscovery();
    publishState();
    return true;
}

void DryerHomeAssistant::publishAvailability(const char* state) {
    String topic = availabilityTopic();
    mqttClient_.publish(topic.c_str(), state, true);
}

void DryerHomeAssistant::publishDiscoveryEntity(
    const char* component,
    const String& objectId,
    const String& payload
) {
    String topic = "homeassistant/" + String(component) +
        "/spoolsense_" + String(deviceId_) +
        "/" + objectId + "/config";

    if (!mqttClient_.publish(topic.c_str(), payload.c_str(), true)) {
        Serial.print("Home Assistant MQTT: discovery publish failed: ");
        Serial.println(objectId);
    }
}

void DryerHomeAssistant::addDeviceMetadata(JsonObject device) const {
    JsonArray identifiers = device["identifiers"].to<JsonArray>();
    identifiers.add("spoolsense_dryer_" + String(deviceId_));

    String displayId = String(deviceId_);
    displayId.toUpperCase();
    device["name"] = "SpoolSense Dryer " + displayId;
    device["manufacturer"] = "SpoolSense";
    device["model"] = "Dryer Controller";
    device["sw_version"] = FIRMWARE_VERSION;
}

void DryerHomeAssistant::publishDiscovery() {
    const String availability = availabilityTopic();

    // Aggregate active state. WAITING_FOR_TEMP and DRYING both count as active,
    // which gives Home Assistant one stable source for external smart-plug
    // automation without duplicating the ESP32's session timers.
    {
        JsonDocument doc;
        doc["name"] = "Dryer Active";
        doc["unique_id"] = makeUniqueId(deviceId_, "dryer_active");
        doc["state_topic"] = baseTopic() + "/dryer/active";
        doc["availability_topic"] = availability;
        doc["payload_on"] = "ON";
        doc["payload_off"] = "OFF";
        doc["icon"] = "mdi:tumble-dryer";
        addDeviceMetadata(doc["device"].to<JsonObject>());

        String payload;
        serializeJson(doc, payload);
        publishDiscoveryEntity("binary_sensor", "dryer_active", payload);
    }

    for (DryerZoneId zone = 0; zone < DRYER_ZONE_COUNT; ++zone) {
        String zoneNumber = String(static_cast<unsigned int>(zone) + 1U);
        String topic = zoneStateTopic(zone);

        {
            JsonDocument doc;
            doc["name"] = dryerZoneLabel(zone) + " Temperature";
            doc["unique_id"] = makeUniqueId(
                deviceId_,
                "dryer_zone_" + zoneNumber + "_temperature"
            );
            doc["state_topic"] = topic;
            doc["availability_topic"] = availability;
            doc["value_template"] = "{{ value_json.temperature_c }}";
            doc["unit_of_measurement"] = "°C";
            doc["device_class"] = "temperature";
            doc["state_class"] = "measurement";
            addDeviceMetadata(doc["device"].to<JsonObject>());

            String payload;
            serializeJson(doc, payload);
            publishDiscoveryEntity(
                "sensor",
                "dryer_zone_" + zoneNumber + "_temperature",
                payload
            );
        }

        {
            JsonDocument doc;
            doc["name"] = dryerZoneLabel(zone) + " Recommended Setpoint";
            doc["unique_id"] = makeUniqueId(
                deviceId_,
                "dryer_zone_" + zoneNumber + "_target"
            );
            doc["state_topic"] = topic;
            doc["availability_topic"] = availability;
            doc["value_template"] = "{{ value_json.target_c }}";
            doc["unit_of_measurement"] = "°C";
            doc["device_class"] = "temperature";
            doc["icon"] = "mdi:thermometer-check";
            addDeviceMetadata(doc["device"].to<JsonObject>());

            String payload;
            serializeJson(doc, payload);
            publishDiscoveryEntity(
                "sensor",
                "dryer_zone_" + zoneNumber + "_target",
                payload
            );
        }

        {
            JsonDocument doc;
            doc["name"] = dryerZoneLabel(zone) + " Drying Plan";
            doc["unique_id"] = makeUniqueId(
                deviceId_,
                "dryer_zone_" + zoneNumber + "_plan"
            );
            doc["state_topic"] = topic;
            doc["availability_topic"] = availability;
            doc["value_template"] = "{{ value_json.plan }}";
            doc["json_attributes_topic"] = topic;
            doc["icon"] = "mdi:chart-timeline-variant";
            addDeviceMetadata(doc["device"].to<JsonObject>());

            String payload;
            serializeJson(doc, payload);
            publishDiscoveryEntity(
                "sensor",
                "dryer_zone_" + zoneNumber + "_plan",
                payload
            );
        }
    }

    for (DryerStationId station = 0; station < DRYER_STATION_COUNT; ++station) {
        String stationNumber = String(static_cast<unsigned int>(station) + 1U);
        String topic = stationStateTopic(station);
        String label = dryerStationLabel(station);

        {
            JsonDocument doc;
            doc["name"] = label + " Session";
            doc["unique_id"] = makeUniqueId(
                deviceId_,
                "dryer_station_" + stationNumber + "_session"
            );
            doc["state_topic"] = topic;
            doc["availability_topic"] = availability;
            doc["value_template"] = "{{ value_json.session }}";
            doc["json_attributes_topic"] = topic;
            doc["icon"] = "mdi:progress-clock";
            addDeviceMetadata(doc["device"].to<JsonObject>());

            String payload;
            serializeJson(doc, payload);
            publishDiscoveryEntity(
                "sensor",
                "dryer_station_" + stationNumber + "_session",
                payload
            );
        }

        {
            JsonDocument doc;
            doc["name"] = label + " Remaining";
            doc["unique_id"] = makeUniqueId(
                deviceId_,
                "dryer_station_" + stationNumber + "_remaining"
            );
            doc["state_topic"] = topic;
            doc["availability_topic"] = availability;
            doc["value_template"] = "{{ value_json.remaining_seconds }}";
            doc["unit_of_measurement"] = "s";
            doc["device_class"] = "duration";
            doc["icon"] = "mdi:timer-outline";
            addDeviceMetadata(doc["device"].to<JsonObject>());

            String payload;
            serializeJson(doc, payload);
            publishDiscoveryEntity(
                "sensor",
                "dryer_station_" + stationNumber + "_remaining",
                payload
            );
        }
    }

    Serial.println("Home Assistant MQTT: discovery published.");
}

void DryerHomeAssistant::publishState() {
    if (!mqttClient_.connected() || statusProvider_ == nullptr) {
        return;
    }

    DryerRuntimeStatus status = statusProvider_();

    bool active = false;
    for (DryerStationId station = 0; station < DRYER_STATION_COUNT; ++station) {
        const String& session = status.stations[station].sessionStatus;
        if (session == "Waiting for temp" || session == "Drying") {
            active = true;
            break;
        }
    }

    String activeTopic = baseTopic() + "/dryer/active";
    mqttClient_.publish(activeTopic.c_str(), active ? "ON" : "OFF", true);

    for (DryerZoneId zone = 0; zone < DRYER_ZONE_COUNT; ++zone) {
        const DryerZoneView& view = status.zones[zone];
        JsonDocument doc;
        doc["label"] = view.label;
        if (view.temperatureValid) {
            doc["temperature_c"] = view.chamberTempC;
        } else {
            doc["temperature_c"] = nullptr;
        }

        if (view.temperaturePlan.recommendedTargetC > 0) {
            doc["target_c"] = view.temperaturePlan.recommendedTargetC;
        } else {
            doc["target_c"] = nullptr;
        }

        doc["plan"] = view.temperaturePlan.status;
        doc["automatic_usable"] = view.temperaturePlan.automaticPlanUsable;
        doc["spool_count"] = view.temperaturePlan.spoolCount;
        doc["common_min_c"] = view.temperaturePlan.commonMinC;
        doc["common_max_c"] = view.temperaturePlan.commonMaxC;
        doc["compromise_gap_c"] = view.temperaturePlan.compromiseGapC;

        String payload;
        serializeJson(doc, payload);
        String topic = zoneStateTopic(zone);
        mqttClient_.publish(topic.c_str(), payload.c_str(), true);
    }

    for (DryerStationId station = 0; station < DRYER_STATION_COUNT; ++station) {
        const DryerStationView& view = status.stations[station];
        JsonDocument doc;
        doc["label"] = view.label;
        doc["zone"] = static_cast<unsigned int>(view.zone) + 1U;
        doc["occupied"] = view.occupied;
        doc["session"] = view.sessionStatus;
        doc["remaining_seconds"] = view.remainingSeconds;
        doc["start_threshold_c"] = view.startThresholdC;
        doc["uid"] = view.uid;
        doc["spool"] = fallbackSpoolName(view);
        doc["material"] = view.spool.material;
        doc["vendor"] = view.spool.vendor;
        doc["spoolman_id"] = view.spool.spoolId;
        doc["dry_temp_c"] = view.spool.dryTempC;
        doc["dry_temp_min_c"] = view.spool.dryTempMinC;
        doc["dry_temp_max_c"] = view.spool.dryTempMaxC;
        doc["dry_time_hours"] = view.spool.dryTimeHours;
        doc["lookup_error"] = view.lookupError;

        String payload;
        serializeJson(doc, payload);
        String topic = stationStateTopic(station);
        mqttClient_.publish(topic.c_str(), payload.c_str(), true);
    }
}

String DryerHomeAssistant::baseTopic() const {
    return "spoolsense/" + String(deviceId_);
}

String DryerHomeAssistant::availabilityTopic() const {
    return baseTopic() + "/availability";
}

String DryerHomeAssistant::zoneStateTopic(DryerZoneId zone) const {
    return baseTopic() + "/dryer/zone/" +
        String(static_cast<unsigned int>(zone)) + "/state";
}

String DryerHomeAssistant::stationStateTopic(DryerStationId station) const {
    return baseTopic() + "/dryer/station/" +
        String(static_cast<unsigned int>(station)) + "/state";
}

String DryerHomeAssistant::slugify(const String& value) {
    String result;
    result.reserve(value.length());
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value.charAt(i);
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            result += c;
        } else if (!result.endsWith("_")) {
            result += '_';
        }
    }
    while (result.endsWith("_")) {
        result.remove(result.length() - 1);
    }
    return result;
}

void DryerHomeAssistant::getDeviceId(char* buf, size_t bufSize) {
    if (buf == nullptr || bufSize < 7) {
        if (buf != nullptr && bufSize > 0) {
            buf[0] = '\0';
        }
        return;
    }

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(
        buf,
        bufSize,
        "%02x%02x%02x",
        mac[3],
        mac[4],
        mac[5]
    );
}
