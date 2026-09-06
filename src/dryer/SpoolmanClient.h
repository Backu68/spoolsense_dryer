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

    int dryTempC = 0;
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
};