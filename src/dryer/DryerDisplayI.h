#pragma once

#include <cstddef>
#include "DryerRuntimeTypes.h"

// Dryer display abstraction, following the same pattern as the official
// SpoolSense DisplayI layer: application logic renders state without knowing
// which physical display is attached.
class DryerDisplayI {
public:
    virtual ~DryerDisplayI() = default;

    virtual bool begin() = 0;
    virtual bool isAvailable() const = 0;
    virtual void render(const DryerRuntimeStatus& status) = 0;

    virtual void getDisplayInfo(char* buf, size_t len) const = 0;
};
