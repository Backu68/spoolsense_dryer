#include "DryerConfig.h"

#include <Preferences.h>

static constexpr const char* NVS_NAMESPACE = "dryer";

bool DryerConfig::begin() {
    ssid_ = "";
    password_ = "";
    spoolmanUrl_ = "";

    Preferences prefs;

    // A brand-new device may not have the namespace yet. That is a normal
    // first-boot state, not an error; the setup portal will create it on save.
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        Serial.println("DryerConfig: no saved configuration.");
        return true;
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
    String cleanSsid = ssid;
    String cleanSpoolmanUrl = spoolmanUrl;

    cleanSsid.trim();
    cleanSpoolmanUrl.trim();

    if (cleanSsid.isEmpty() || cleanSpoolmanUrl.isEmpty()) {
        return false;
    }

    Preferences prefs;

    if (!prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }

    size_t writtenSsid = prefs.putString("ssid", cleanSsid);
    prefs.putString("wifi_pass", password);
    size_t writtenSpoolman = prefs.putString("spoolman", cleanSpoolmanUrl);

    prefs.end();

    if (writtenSsid == 0 || writtenSpoolman == 0) {
        return false;
    }

    ssid_ = cleanSsid;
    password_ = password;
    spoolmanUrl_ = cleanSpoolmanUrl;

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
