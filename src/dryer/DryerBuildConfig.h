#pragma once

#include <Arduino.h>

// Dryer shape is selected when the firmware is built/flashed. Station/zone
// counts describe the controller topology; layout controls human-facing station
// names and the natural default station.
#define DRYER_LAYOUT_VERTICAL 0
#define DRYER_LAYOUT_HORIZONTAL 1

#ifndef DRYER_LAYOUT
#define DRYER_LAYOUT DRYER_LAYOUT_VERTICAL
#endif

#ifndef DRYER_STATION_COUNT
#define DRYER_STATION_COUNT 2
#endif

#ifndef DRYER_ZONE_COUNT
#define DRYER_ZONE_COUNT 1
#endif

static_assert(
    DRYER_LAYOUT == DRYER_LAYOUT_VERTICAL ||
    DRYER_LAYOUT == DRYER_LAYOUT_HORIZONTAL,
    "DRYER_LAYOUT must be DRYER_LAYOUT_VERTICAL (0) or DRYER_LAYOUT_HORIZONTAL (1)"
);
static_assert(DRYER_STATION_COUNT >= 1, "DRYER_STATION_COUNT must be at least 1");
static_assert(DRYER_STATION_COUNT <= 8, "DRYER_STATION_COUNT currently supports up to 8 stations");
static_assert(DRYER_ZONE_COUNT >= 1, "DRYER_ZONE_COUNT must be at least 1");
static_assert(DRYER_ZONE_COUNT <= DRYER_STATION_COUNT, "Cannot have more thermal zones than stations");
static_assert(DRYER_ZONE_COUNT <= 4, "DRYER_ZONE_COUNT currently supports up to 4 zones");

using DryerStationId = uint8_t;
using DryerZoneId = uint8_t;

constexpr bool DRYER_IS_HORIZONTAL =
    DRYER_LAYOUT == DRYER_LAYOUT_HORIZONTAL;

// Horizontal dryers naturally begin at BAY 1. Vertical dryers naturally begin
// at the lowest physical shelf, preserving the retrofit's BOTTOM-first behavior
// for any number of stacked levels.
constexpr DryerStationId DRYER_DEFAULT_STATION =
    DRYER_IS_HORIZONTAL
        ? static_cast<DryerStationId>(0)
        : static_cast<DryerStationId>(DRYER_STATION_COUNT - 1);

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
    if (DRYER_IS_HORIZONTAL) {
        return "BAY " + String(static_cast<unsigned int>(station) + 1U);
    }

    if (DRYER_STATION_COUNT == 1) {
        return "SHELF";
    }

    if (station == 0) {
        return "TOP";
    }

    if (station == DRYER_STATION_COUNT - 1) {
        return "BOTTOM";
    }

    // Three levels read TOP / MIDDLE / BOTTOM. Four or more levels retain clear
    // physical ordering without pretending there is only one middle position.
    if (DRYER_STATION_COUNT == 3) {
        return "MIDDLE";
    }

    return "MID " + String(static_cast<unsigned int>(station));
}

inline String dryerZoneLabel(DryerZoneId zone) {
    if (DRYER_ZONE_COUNT == 1) {
        return "CHAMBER";
    }

    return "ZONE " + String(static_cast<unsigned int>(zone) + 1U);
}
