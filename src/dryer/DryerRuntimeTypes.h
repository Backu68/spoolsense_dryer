#pragma once

#include <Arduino.h>
#include "DryerTemperaturePlanner.h"
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

struct DryerTemperaturePlanView {
    DryerTemperaturePlanState state = DryerTemperaturePlanState::NONE;
    String status;
    uint8_t spoolCount = 0;
    int commonMinC = 0;
    int commonMaxC = 0;
    int recommendedTargetC = 0;
    int compromiseGapC = 0;
    bool automaticPlanUsable = false;
};

struct DryerRuntimeStatus {
    float chamberTempC = 0.0f;
    bool temperatureValid = false;

    bool wifiConnected = false;
    String ipAddress;
    bool spoolmanConfigured = false;
    bool nfcAvailable = false;
    bool setupPortalActive = false;

    // The current prototype is one thermal zone shared by TOP and BOTTOM.
    // The planner itself accepts any number of spool profiles so this view can
    // become a per-zone array when station/zone counts are generalized.
    DryerTemperaturePlanView temperaturePlan;

    DryerStation selectedStation = DryerStation::BOTTOM;
    DryerStationView top;
    DryerStationView bottom;
};
