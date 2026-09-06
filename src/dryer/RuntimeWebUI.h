#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "DryerRuntimeTypes.h"

class RuntimeWebUI {
public:
    using StatusProvider = DryerRuntimeStatus (*)();
    using ClearHandler = void (*)(bool topStation);

    bool begin(StatusProvider statusProvider, ClearHandler clearHandler);
    void loop();
    bool isActive() const;

private:
    void handleRoot();
    void handleStatus();
    void handleClear();

    static String htmlEscape(const String& value);

    WebServer server_{80};
    StatusProvider statusProvider_ = nullptr;
    ClearHandler clearHandler_ = nullptr;
    bool active_ = false;
};
