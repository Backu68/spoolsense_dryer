#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiClient.h>

#include "DryerConfig.h"
#include "DryerRuntimeTypes.h"

// Home Assistant/MQTT integration for the dryer controller.
//
// The ESP32 remains the source of truth for drying sessions and timers. MQTT is
// deliberately supervisory/read-only here: it publishes state and Home
// Assistant discovery, but does not expose a writable heater setpoint for the
// retrofit dryer because the physical appliance temperature is manually set.
class DryerHomeAssistant {
public:
    using StatusProvider = DryerRuntimeStatus (*)();

    bool begin(DryerConfig& config, StatusProvider statusProvider);
    void loop();

    bool isConfigured() const;
    bool isConnected() const;
    int getLastMqttState() const;

private:
    bool reconnect();
    void publishDiscovery();
    void publishState();
    void publishAvailability(const char* state);

    void publishDiscoveryEntity(
        const char* component,
        const String& objectId,
        const String& payload
    );

    String baseTopic() const;
    String availabilityTopic() const;
    String zoneStateTopic(DryerZoneId zone) const;
    String stationStateTopic(DryerStationId station) const;

    void addDeviceMetadata(JsonObject device) const;
    static String slugify(const String& value);
    static void getDeviceId(char* buf, size_t bufSize);

    DryerConfig* config_ = nullptr;
    StatusProvider statusProvider_ = nullptr;

    WiFiClient wifiClient_;
    PubSubClient mqttClient_;

    char deviceId_[7] = {0};
    bool configured_ = false;
    bool connected_ = false;
    int lastMqttState_ = -1;

    uint32_t reconnectDelayMs_ = 1000;
    uint32_t lastReconnectAttemptMs_ = 0;
    uint32_t lastPublishMs_ = 0;

    static constexpr uint32_t MAX_RECONNECT_DELAY_MS = 30000;
    static constexpr uint32_t STATE_PUBLISH_INTERVAL_MS = 2000;
};
