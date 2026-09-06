#pragma once

#include <Arduino.h>
#include "SpoolmanClient.h"

// Shared application-facing dryer state. Hardware/UI backends consume these
// structures without needing to know how the state was produced.
enum class DryerStation {
    TOP,
    BOTTOM
};

enum class DryingSessionState {
    INACTIVE,
    WAITING_FOR_TEMP,
    DRYING,
    COMPLETE
};

struct DryerStationView {
    bool occupied = false;
    String uid;
    DryerSpoolInfo spool;
    String lookupError;

    String sessionStatus;
    uint32_t remainingSeconds = 0;
    int startThresholdC = 0;
};

struct DryerRuntimeStatus {
    float chamberTempC = 0.0f;
    bool temperatureValid = false;

    bool wifiConnected = false;
    String ipAddress;
    bool spoolmanConfigured = false;
    bool nfcAvailable = false;
    bool setupPortalActive = false;

    DryerStation selectedStation = DryerStation::BOTTOM;
    DryerStationView top;
    DryerStationView bottom;
};
