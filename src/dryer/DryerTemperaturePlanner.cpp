#include "DryerTemperaturePlanner.h"

bool DryerTemperaturePlanner::normalizeProfile(
    const DryerSpoolInfo& spool,
    int& minC,
    int& preferredC,
    int& maxC
) {
    if (!spool.found) {
        return false;
    }

    preferredC = spool.dryTempC;
    minC = spool.dryTempMinC > 0 ? spool.dryTempMinC : preferredC;
    maxC = spool.dryTempMaxC > 0 ? spool.dryTempMaxC : preferredC;

    if (minC <= 0 || maxC <= 0) {
        return false;
    }

    if (minC > maxC) {
        int swap = minC;
        minC = maxC;
        maxC = swap;
    }

    if (preferredC <= 0) {
        preferredC = (minC + maxC + 1) / 2;
    }

    if (preferredC < minC) {
        preferredC = minC;
    } else if (preferredC > maxC) {
        preferredC = maxC;
    }

    return true;
}

DryerTemperaturePlan DryerTemperaturePlanner::calculate(
    const DryerSpoolInfo* const* spools,
    size_t count
) {
    DryerTemperaturePlan plan;

    if (spools == nullptr || count == 0) {
        return plan;
    }

    bool first = true;
    bool invalidProfileSeen = false;
    int preferredSum = 0;
    int preferredMin = 0;
    int preferredMax = 0;

    for (size_t i = 0; i < count; ++i) {
        if (spools[i] == nullptr || !spools[i]->found) {
            continue;
        }

        int minC = 0;
        int preferredC = 0;
        int maxC = 0;

        if (!normalizeProfile(*spools[i], minC, preferredC, maxC)) {
            invalidProfileSeen = true;
            continue;
        }

        ++plan.spoolCount;
        preferredSum += preferredC;

        if (first) {
            plan.commonMinC = minC;
            plan.commonMaxC = maxC;
            preferredMin = preferredC;
            preferredMax = preferredC;
            first = false;
        } else {
            plan.commonMinC = max(plan.commonMinC, minC);
            plan.commonMaxC = min(plan.commonMaxC, maxC);
            preferredMin = min(preferredMin, preferredC);
            preferredMax = max(preferredMax, preferredC);
        }
    }

    if (plan.spoolCount == 0) {
        if (invalidProfileSeen) {
            plan.state = DryerTemperaturePlanState::INVALID_PROFILE;
        }
        return plan;
    }

    if (invalidProfileSeen) {
        plan.state = DryerTemperaturePlanState::INVALID_PROFILE;
        return plan;
    }

    if (plan.spoolCount == 1) {
        plan.state = DryerTemperaturePlanState::SINGLE;
        plan.recommendedTargetC = preferredSum;
        plan.automaticPlanUsable = true;
        return plan;
    }

    if (plan.commonMinC <= plan.commonMaxC) {
        int meanPreferred =
            (preferredSum + static_cast<int>(plan.spoolCount / 2)) /
            static_cast<int>(plan.spoolCount);

        plan.recommendedTargetC = constrain(
            meanPreferred,
            plan.commonMinC,
            plan.commonMaxC
        );
        plan.automaticPlanUsable = true;

        // "Ideal" means every spool's preferred temperature itself falls inside
        // the common legal range. Otherwise the combination is still safe, but
        // at least one spool must be moved away from its preferred point.
        if (
            preferredMin >= plan.commonMinC &&
            preferredMax <= plan.commonMaxC
        ) {
            plan.state = DryerTemperaturePlanState::IDEAL;
        } else {
            plan.state = DryerTemperaturePlanState::COMPATIBLE;
        }

        return plan;
    }

    // No common range exists. The least aggressive universally safe candidate
    // is the lowest maximum temperature among the loaded spools (commonMaxC).
    // It may sit below another spool's minimum, so it is advisory only. We do
    // not fabricate a temperature/time compensation curve here.
    plan.state = DryerTemperaturePlanState::COMPROMISE_REQUIRED;
    plan.recommendedTargetC = plan.commonMaxC;
    plan.compromiseGapC = plan.commonMinC - plan.commonMaxC;
    plan.automaticPlanUsable = false;
    return plan;
}

const char* DryerTemperaturePlanner::stateText(
    DryerTemperaturePlanState state
) {
    switch (state) {
        case DryerTemperaturePlanState::SINGLE:
            return "Single spool";
        case DryerTemperaturePlanState::IDEAL:
            return "Ideal shared range";
        case DryerTemperaturePlanState::COMPATIBLE:
            return "Compatible shared range";
        case DryerTemperaturePlanState::COMPROMISE_REQUIRED:
            return "Compromise required";
        case DryerTemperaturePlanState::INVALID_PROFILE:
            return "Dry profile incomplete";
        case DryerTemperaturePlanState::NONE:
        default:
            return "No drying profile";
    }
}
