#include "DryerTimeService.h"

#include <time.h>

void DryerTimeService::begin() {
    configTime(
        0,
        0,
        "pool.ntp.org",
        "time.nist.gov",
        "time.google.com"
    );
    Serial.println("NTP: synchronization requested.");
}

bool DryerTimeService::waitForSync(uint32_t timeoutMs) {
    unsigned long start = millis();

    while (!isSynced() && (millis() - start) < timeoutMs) {
        delay(100);
    }

    if (!isSynced()) {
        Serial.println("NTP: not synchronized yet; millis() fallback active.");
        return false;
    }

    Serial.print("NTP synchronized. Epoch: ");
    Serial.println(nowEpoch());
    return true;
}

bool DryerTimeService::isSynced() const {
    time_t now = time(nullptr);
    return now >= static_cast<time_t>(MIN_VALID_EPOCH);
}

uint32_t DryerTimeService::nowEpoch() const {
    time_t now = time(nullptr);
    if (now < static_cast<time_t>(MIN_VALID_EPOCH)) {
        return 0;
    }

    return static_cast<uint32_t>(now);
}
