#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "DryerConfig.h"

class SetupPortal {
public:
    bool begin(DryerConfig& config);
    void loop();

    bool isActive() const;
    const String& getApSsid() const;

private:
    void handleRoot();
    void handleSave();
    String buildNetworkOptions();
    static String htmlEscape(const String& value);

    WebServer server_{80};
    DryerConfig* config_ = nullptr;
    bool active_ = false;
    bool restartPending_ = false;
    unsigned long restartAtMs_ = 0;
    String apSsid_;
};
