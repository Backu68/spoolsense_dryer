#include "DryerSessionStore.h"

bool DryerSessionStore::begin() {
    ready_ = prefs_.begin("dry_sessions", false);
    if (!ready_) {
        Serial.println("DryerSessionStore: failed to open NVS namespace.");
        return false;
    }

    uint8_t version = prefs_.getUChar("ver", 0);
    if (version != FORMAT_VERSION) {
        prefs_.clear();
        prefs_.putUChar("ver", FORMAT_VERSION);
        Serial.println("DryerSessionStore: initialized persistence format.");
    }

    return true;
}

String DryerSessionStore::key(DryerStation station, const char* suffix) const {
    String result = station == DryerStation::TOP ? "t_" : "b_";
    result += suffix;
    return result;
}

bool DryerSessionStore::load(DryerStation station, DryerStationState& state) {
    state = DryerStationState{};

    if (!ready_) {
        return false;
    }

    String occupiedKey = key(station, "occ");
    if (!prefs_.isKey(occupiedKey.c_str()) ||
        !prefs_.getBool(occupiedKey.c_str(), false)) {
        return false;
    }

    state.occupied = true;
    state.uid = prefs_.getString(key(station, "uid").c_str(), "");
    state.lookupError = prefs_.getString(key(station, "err").c_str(), "");

    state.spool.found = prefs_.getBool(key(station, "found").c_str(), false);
    state.spool.spoolId = prefs_.getInt(key(station, "sid").c_str(), -1);
    state.spool.filamentId = prefs_.getInt(key(station, "fid").c_str(), -1);
    state.spool.name = prefs_.getString(key(station, "name").c_str(), "");
    state.spool.vendor = prefs_.getString(key(station, "vend").c_str(), "");
    state.spool.material = prefs_.getString(key(station, "mat").c_str(), "");
    state.spool.dryTempC = prefs_.getInt(key(station, "dtemp").c_str(), 0);
    state.spool.dryTimeHours = prefs_.getInt(key(station, "dhrs").c_str(), 0);

    uint8_t rawState = prefs_.getUChar(key(station, "state").c_str(), 0);
    if (rawState > static_cast<uint8_t>(DryingSessionState::COMPLETE)) {
        rawState = static_cast<uint8_t>(DryingSessionState::INACTIVE);
    }
    state.sessionState = static_cast<DryingSessionState>(rawState);

    state.totalSeconds = prefs_.getUInt(key(station, "total").c_str(), 0);
    state.remainingSeconds = prefs_.getUInt(key(station, "remain").c_str(), 0);
    state.startThresholdC = prefs_.getInt(key(station, "thresh").c_str(), 0);

    // A reboot/power interruption pauses the drying clock. If the station was
    // actively drying, resume from the saved remaining time only after the
    // chamber reaches the start threshold again.
    if (state.sessionState == DryingSessionState::DRYING &&
        state.remainingSeconds > 0) {
        state.sessionState = DryingSessionState::WAITING_FOR_TEMP;
    }

    state.lastSessionTickMs = 0;
    state.lastPersistMs = millis();
    return true;
}

bool DryerSessionStore::save(
    DryerStation station,
    const DryerStationState& state
) {
    if (!ready_) {
        return false;
    }

    if (!state.occupied) {
        return clear(station);
    }

    bool ok = true;
    ok &= prefs_.putBool(key(station, "occ").c_str(), state.occupied) > 0;
    ok &= prefs_.putString(key(station, "uid").c_str(), state.uid) > 0;
    prefs_.putString(key(station, "err").c_str(), state.lookupError);

    prefs_.putBool(key(station, "found").c_str(), state.spool.found);
    prefs_.putInt(key(station, "sid").c_str(), state.spool.spoolId);
    prefs_.putInt(key(station, "fid").c_str(), state.spool.filamentId);
    prefs_.putString(key(station, "name").c_str(), state.spool.name);
    prefs_.putString(key(station, "vend").c_str(), state.spool.vendor);
    prefs_.putString(key(station, "mat").c_str(), state.spool.material);
    prefs_.putInt(key(station, "dtemp").c_str(), state.spool.dryTempC);
    prefs_.putInt(key(station, "dhrs").c_str(), state.spool.dryTimeHours);

    prefs_.putUChar(
        key(station, "state").c_str(),
        static_cast<uint8_t>(state.sessionState)
    );
    prefs_.putUInt(key(station, "total").c_str(), state.totalSeconds);
    prefs_.putUInt(key(station, "remain").c_str(), state.remainingSeconds);
    prefs_.putInt(key(station, "thresh").c_str(), state.startThresholdC);

    return ok;
}

bool DryerSessionStore::checkpoint(
    DryerStation station,
    DryingSessionState sessionState,
    uint32_t remainingSeconds
) {
    if (!ready_) {
        return false;
    }

    prefs_.putUChar(
        key(station, "state").c_str(),
        static_cast<uint8_t>(sessionState)
    );
    prefs_.putUInt(key(station, "remain").c_str(), remainingSeconds);
    return true;
}

bool DryerSessionStore::clear(DryerStation station) {
    if (!ready_) {
        return false;
    }

    const char* suffixes[] = {
        "occ", "uid", "err", "found", "sid", "fid", "name", "vend",
        "mat", "dtemp", "dhrs", "state", "total", "remain", "thresh"
    };

    for (const char* suffix : suffixes) {
        String k = key(station, suffix);
        if (prefs_.isKey(k.c_str())) {
            prefs_.remove(k.c_str());
        }
    }

    return true;
}
