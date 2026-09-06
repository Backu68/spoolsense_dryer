#pragma once

#include <Arduino.h>

// Dryer shape is selected when the firmware is built/flashed. These defaults
// preserve the current retrofit: two spool stations sharing one thermal zone.
#ifndef DRYER_STATION_COUNT
#define DRYER_STATION_COUNT 2
#endif

#ifndef DRYER_ZONE_COUNT
#define DRYER_ZONE_COUNT 1
#endif

static_assert(DRYER_STATION_COUNT >= 1, "DRYER_STATION_COUNT must be at least 1");
static_assert(DRYER_STATION_COUNT <= 8, "DRYER_STATION_COUNT currently supports up to 8 stations");
static_assert(DRYER_ZONE_COUNT >= 1, "DRYER_ZONE_COUNT must be at least 1");
static_assert(DRYER_ZONE_COUNT <= DRYER_STATION_COUNT, "Cannot have more thermal zones than stations");
static_assert(DRYER_ZONE_COUNT <= 4, "DRYER_ZONE_COUNT currently supports up to 4 zones");

using DryerStationId = uint8_t;
using DryerZoneId = uint8_t;

constexpr DryerStationId DRYER_DEFAULT_STATION =
    DRYER_STATION_COUNT == 2 ? 1 : 0;

inline DryerZoneId dryerZoneForStation(DryerStationId station) {
    // Default mapping keeps adjacent stations together. Examples:
    // 4 stations / 1 zone -> 0,0,0,0
    // 4 stations / 2 zones -> 0,0,1,1
    // 4 stations / 4 zones -> 0,1,2,3
    return static_cast<DryerZoneId>(
        (static_cast<uint16_t>(station) * DRYER_ZONE_COUNT) /
        DRYER_STATION_COUNT
    );
}

inline String dryerStationLabel(DryerStationId station) {
    // Preserve the familiar retrofit labels while allowing arbitrary station
    // counts for shelves/bays in other builds.
    if (DRYER_STATION_COUNT == 2) {
        return station == 0 ? "TOP" : "BOTTOM";
    }

    return "S" + String(static_cast<unsigned int>(station) + 1U);
}

inline String dryerZoneLabel(DryerZoneId zone) {
    if (DRYER_ZONE_COUNT == 1) {
        return "CHAMBER";
    }

    return "ZONE " + String(static_cast<unsigned int>(zone) + 1U);
}
