#include "DryerConfig.h"

#include <Preferences.h>

static constexpr const char* NVS_NAMESPACE = "dryer";

bool DryerConfig::begin() {
    ssid_ = "";
    password_ = "";
    spoolmanUrl_ = "";
    haEnabled_ = false;
    mqttHost_ = "";
    mqttPort_ = 1883;
    mqttUser_ = "";
    mqttPassword_ = "";

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

    haEnabled_ = prefs.getBool("ha_en", false);
    mqttHost_ = prefs.getString("mqtt_host", "");
    mqttPort_ = prefs.getUShort("mqtt_port", 1883);
    mqttUser_ = prefs.getString("mqtt_user", "");
    mqttPassword_ = prefs.getString("mqtt_pass", "");

    prefs.end();

    Serial.print("DryerConfig: WiFi configured: ");
    Serial.println(hasWiFi() ? "yes" : "no");

    Serial.print("DryerConfig: Spoolman configured: ");
    Serial.println(hasSpoolman() ? "yes" : "no");

    Serial.print("DryerConfig: Home Assistant configured: ");
    Serial.println(hasHomeAssistant() ? "yes" : "no");

    return true;
}

bool DryerConfig::hasWiFi() const {
    return !ssid_.isEmpty();
}

bool DryerConfig::hasSpoolman() const {
    return !spoolmanUrl_.isEmpty();
}

bool DryerConfig::hasHomeAssistant() const {
    return haEnabled_ && !mqttHost_.isEmpty() && mqttPort_ > 0;
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

bool DryerConfig::getHAEnabled() const {
    return haEnabled_;
}

const char* DryerConfig::getHAMqttHost() const {
    return mqttHost_.c_str();
}

uint16_t DryerConfig::getHAMqttPort() const {
    return mqttPort_;
}

const char* DryerConfig::getHAMqttUser() const {
    return mqttUser_.c_str();
}

const char* DryerConfig::getHAMqttPassword() const {
    return mqttPassword_.c_str();
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

bool DryerConfig::saveHomeAssistant(
    bool enabled,
    const String& mqttHost,
    uint16_t mqttPort,
    const String& mqttUser,
    const String& mqttPassword
) {
    String cleanHost = mqttHost;
    String cleanUser = mqttUser;
    cleanHost.trim();
    cleanUser.trim();

    if (enabled && (cleanHost.isEmpty() || mqttPort == 0)) {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }

    bool ok = true;
    ok &= prefs.putBool("ha_en", enabled) > 0;
    prefs.putString("mqtt_host", cleanHost);
    ok &= prefs.putUShort("mqtt_port", mqttPort == 0 ? 1883 : mqttPort) > 0;
    prefs.putString("mqtt_user", cleanUser);
    prefs.putString("mqtt_pass", mqttPassword);
    prefs.end();

    if (!ok) {
        return false;
    }

    haEnabled_ = enabled;
    mqttHost_ = cleanHost;
    mqttPort_ = mqttPort == 0 ? 1883 : mqttPort;
    mqttUser_ = cleanUser;
    mqttPassword_ = mqttPassword;
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
    haEnabled_ = false;
    mqttHost_ = "";
    mqttPort_ = 1883;
    mqttUser_ = "";
    mqttPassword_ = "";
}
