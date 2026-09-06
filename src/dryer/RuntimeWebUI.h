#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "SpoolmanClient.h"

struct RuntimeStationView {
    bool occupied = false;
    String uid;
    DryerSpoolInfo spool;
    String lookupError;

    String sessionStatus;
    uint32_t remainingSeconds = 0;
    int startThresholdC = 0;
};

struct RuntimeStatus {
    float chamberTempC = 0.0f;
    bool temperatureValid = false;
    bool wifiConnected = false;
    bool spoolmanConfigured = false;
    bool nfcAvailable = false;
    bool topSelected = false;
    RuntimeStationView top;
    RuntimeStationView bottom;
};

class RuntimeWebUI {
public:
    using StatusProvider = RuntimeStatus (*)();
    using ClearHandler = void (*)(bool topStation);

    bool begin(StatusProvider statusProvider, ClearHandler clearHandler);
    void loop();
    bool isActive() const;

private:
    void handleRoot();
    void handleStatus();
    void handleClear();

    static String htmlEscape(const String& value);

    WebServer server_{80};
    StatusProvider statusProvider_ = nullptr;
    ClearHandler clearHandler_ = nullptr;
    bool active_ = false;
};
