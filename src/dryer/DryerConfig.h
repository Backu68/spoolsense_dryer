#pragma once

#include <Arduino.h>

class DryerConfig {
public:
    bool begin();

    bool hasWiFi() const;
    bool hasSpoolman() const;

    const char* getSSID() const;
    const char* getPassword() const;
    const char* getSpoolmanURL() const;

    bool save(
        const String& ssid,
        const String& password,
        const String& spoolmanUrl
    );

    void clear();

private:
    String ssid_;
    String password_;
    String spoolmanUrl_;
};