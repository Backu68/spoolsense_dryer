#pragma once

#include <Arduino.h>

class DryerConfig {
public:
    bool begin();

    bool hasWiFi() const;
    bool hasSpoolman() const;
    bool hasHomeAssistant() const;

    const char* getSSID() const;
    const char* getPassword() const;
    const char* getSpoolmanURL() const;

    bool getHAEnabled() const;
    const char* getHAMqttHost() const;
    uint16_t getHAMqttPort() const;
    const char* getHAMqttUser() const;
    const char* getHAMqttPassword() const;

    bool save(
        const String& ssid,
        const String& password,
        const String& spoolmanUrl
    );

    bool saveHomeAssistant(
        bool enabled,
        const String& mqttHost,
        uint16_t mqttPort,
        const String& mqttUser,
        const String& mqttPassword
    );

    void clear();

private:
    String ssid_;
    String password_;
    String spoolmanUrl_;

    bool haEnabled_ = false;
    String mqttHost_;
    uint16_t mqttPort_ = 1883;
    String mqttUser_;
    String mqttPassword_;
};