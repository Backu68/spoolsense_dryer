#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "DryerRuntimeTypes.h"

// Full station/session state used by the dryer application. Hardware/UI layers
// consume DryerRuntimeStatus instead; this structure remains application-owned.
struct DryerStationState {
    bool occupied = false;
    String uid;
    DryerSpoolInfo spool;
    String lookupError;

    DryingSessionState sessionState = DryingSessionState::INACTIVE;
    uint32_t totalSeconds = 0;
    uint32_t remainingSeconds = 0;
    int startThresholdC = 0;

    // Absolute UTC completion deadline while actively drying. Zero means the
    // session is using the millis() fallback because wall-clock time is not yet
    // synchronized.
    uint32_t finishEpoch = 0;

    // Runtime-only timing. These values are intentionally not persisted.
    unsigned long lastSessionTickMs = 0;
    unsigned long lastPersistMs = 0;
};

// NVS-backed persistence for assigned spools and drying-session progress.
// Full station records are written only when assignments/state change; active
// timers use a small periodic checkpoint to limit flash wear.
class DryerSessionStore {
public:
    bool begin();
    bool load(DryerStation station, DryerStationState& state);
    bool save(DryerStation station, const DryerStationState& state);
    bool checkpoint(
        DryerStation station,
        DryingSessionState sessionState,
        uint32_t remainingSeconds,
        uint32_t finishEpoch
    );
    bool clear(DryerStation station);

private:
    String key(DryerStation station, const char* suffix) const;

    Preferences prefs_;
    bool ready_ = false;

    static constexpr uint8_t FORMAT_VERSION = 1;
};
