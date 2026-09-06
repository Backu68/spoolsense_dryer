#pragma once

#include <Arduino.h>
#include <stddef.h>

#include "SpoolmanClient.h"

enum class DryerTemperaturePlanState : uint8_t {
    NONE,
    SINGLE,
    IDEAL,
    COMPATIBLE,
    COMPROMISE_REQUIRED,
    INVALID_PROFILE
};

struct DryerTemperaturePlan {
    DryerTemperaturePlanState state = DryerTemperaturePlanState::NONE;
    uint8_t spoolCount = 0;

    // Intersection of all valid drying ranges. When there is no intersection,
    // commonMinC will be greater than commonMaxC.
    int commonMinC = 0;
    int commonMaxC = 0;

    // Best shared chamber target. For COMPROMISE_REQUIRED this is the highest
    // temperature that does not exceed any loaded filament's maximum; it is
    // advisory only until a validated time-compensation model exists.
    int recommendedTargetC = 0;
    int compromiseGapC = 0;

    // True only when the planner can recommend a target that lies inside every
    // loaded spool's stated drying range.
    bool automaticPlanUsable = false;
};

class DryerTemperaturePlanner {
public:
    static DryerTemperaturePlan calculate(
        const DryerSpoolInfo* const* spools,
        size_t count
    );

    static const char* stateText(DryerTemperaturePlanState state);

private:
    static bool normalizeProfile(
        const DryerSpoolInfo& spool,
        int& minC,
        int& preferredC,
        int& maxC
    );
};
