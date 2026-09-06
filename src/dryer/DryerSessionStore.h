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

    // Runtime-only timing. These values are intentionally not persisted.
    unsigned long lastSessionTickMs = 0;
    unsigned long lastPersistMs = 0;
};

// NVS-backed persistence for assigned spools and drying-session progress.
// Active timers are periodically checkpointed rather than written every second
// to avoid unnecessary flash wear.
class DryerSessionStore {
public:
    bool begin();
    bool load(DryerStation station, DryerStationState& state);
    bool save(DryerStation station, const DryerStationState& state);
    bool clear(DryerStation station);

private:
    String key(DryerStation station, const char* suffix) const;

    Preferences prefs_;
    bool ready_ = false;

    static constexpr uint8_t FORMAT_VERSION = 1;
};
