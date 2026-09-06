#pragma once

#include <Arduino.h>
#include <cstdint>

// Small wall-clock service for dryer session timing. Uses the ESP32 SNTP
// implementation via configTime() and exposes only UTC epoch seconds; dryer
// logic does not need timezone/local-time handling.
class DryerTimeService {
public:
    void begin();
    bool waitForSync(uint32_t timeoutMs = 5000);
    bool isSynced() const;
    uint32_t nowEpoch() const;

private:
    static constexpr uint32_t MIN_VALID_EPOCH = 1700000000UL;
};
