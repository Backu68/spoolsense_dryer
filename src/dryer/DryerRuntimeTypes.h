#pragma once

#include <Arduino.h>

#include "DryerBuildConfig.h"
#include "DryerTemperaturePlanner.h"
#include "SpoolmanClient.h"

// Shared application-facing dryer state. Hardware/UI backends consume these
// structures without needing to know how the state was produced.
enum class DryingSessionState : uint8_t {
    INACTIVE,
    WAITING_FOR_TEMP,
    DRYING,
    COMPLETE
};

struct DryerStationView {
    DryerStationId id = 0;
    DryerZoneId zone = 0;
    String label;

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

struct DryerZoneView {
    DryerZoneId id = 0;
    String label;
    float chamberTempC = 0.0f;
    bool temperatureValid = false;
    DryerTemperaturePlanView temperaturePlan;
};

struct DryerRuntimeStatus {
    bool wifiConnected = false;
    String ipAddress;
    bool spoolmanConfigured = false;
    bool nfcAvailable = false;
    bool setupPortalActive = false;
    bool homeAssistantConfigured = false;
    bool homeAssistantConnected = false;

    DryerStationId selectedStation = DRYER_DEFAULT_STATION;
    DryerStationView stations[DRYER_STATION_COUNT];
    DryerZoneView zones[DRYER_ZONE_COUNT];
};
