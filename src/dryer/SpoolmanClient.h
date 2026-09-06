#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

struct DryerSpoolInfo {
    bool found = false;

    int32_t spoolId = -1;
    int32_t filamentId = -1;

    String name;
    String vendor;
    String material;

    // Preferred/nominal drying temperature. Kept as dryTempC for backwards
    // compatibility with the existing session code.
    int dryTempC = 0;

    // Safe/recommended drying range. If Spoolman only provides a single
    // temperature these collapse to dryTempC.
    int dryTempMinC = 0;
    int dryTempMaxC = 0;

    int dryTimeHours = 0;
};

class SpoolmanClient {
public:
    void setBaseUrl(const String& baseUrl);
    bool isConfigured() const;

    bool lookupByUid(
        const String& uid,
        DryerSpoolInfo& spool,
        String& error
    );

private:
    String baseUrl_;

    static String cleanExtraValue(const String& value);
    static int readExtraInt(JsonVariantConst value);
    static uint8_t extractPositiveInts(
        const String& text,
        int* values,
        uint8_t maxValues
    );
    static void readDryTemperatureProfile(
        JsonObjectConst extra,
        DryerSpoolInfo& spool
    );
};
