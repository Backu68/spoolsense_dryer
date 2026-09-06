#include "DryerConfig.h"

#include <Preferences.h>

static constexpr const char* NVS_NAMESPACE = "dryer";

bool DryerConfig::begin() {
    Preferences prefs;

    if (!prefs.begin(NVS_NAMESPACE, true)) {
        Serial.println("DryerConfig: failed to open NVS.");
        return false;
    }

    ssid_ = prefs.getString("ssid", "");
    password_ = prefs.getString("wifi_pass", "");
    spoolmanUrl_ = prefs.getString("spoolman", "");

    prefs.end();

    Serial.print("DryerConfig: WiFi configured: ");
    Serial.println(hasWiFi() ? "yes" : "no");

    Serial.print("DryerConfig: Spoolman configured: ");
    Serial.println(hasSpoolman() ? "yes" : "no");

    return true;
}

bool DryerConfig::hasWiFi() const {
    return !ssid_.isEmpty();
}

bool DryerConfig::hasSpoolman() const {
    return !spoolmanUrl_.isEmpty();
}

const char* DryerConfig::getSSID() const {
    return ssid_.c_str();
}

const char* DryerConfig::getPassword() const {
    return password_.c_str();
}

const char* DryerConfig::getSpoolmanURL() const {
    return spoolmanUrl_.c_str();
}

bool DryerConfig::save(
    const String& ssid,
    const String& password,
    const String& spoolmanUrl
) {
    Preferences prefs;

    if (!prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }

    prefs.putString("ssid", ssid);
    prefs.putString("wifi_pass", password);
    prefs.putString("spoolman", spoolmanUrl);

    prefs.end();

    ssid_ = ssid;
    password_ = password;
    spoolmanUrl_ = spoolmanUrl;

    return true;
}

void DryerConfig::clear() {
    Preferences prefs;

    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.clear();
        prefs.end();
    }

    ssid_ = "";
    password_ = "";
    spoolmanUrl_ = "";
}