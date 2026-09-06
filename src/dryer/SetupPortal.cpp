#include "SetupPortal.h"

#include <WiFi.h>

bool SetupPortal::begin(DryerConfig& config) {
    config_ = &config;

    uint64_t chipId = ESP.getEfuseMac();
    char suffix[7];
    snprintf(
        suffix,
        sizeof(suffix),
        "%06llX",
        static_cast<unsigned long long>(chipId & 0xFFFFFFULL)
    );

    apSsid_ = "SpoolSense-Dryer-" + String(suffix);

    WiFi.mode(WIFI_AP_STA);

    if (!WiFi.softAP(apSsid_.c_str())) {
        Serial.println("SetupPortal: failed to start access point.");
        return false;
    }

    server_.on("/", HTTP_GET, [this]() {
        handleRoot();
    });

    server_.on("/save", HTTP_POST, [this]() {
        handleSave();
    });

    server_.onNotFound([this]() {
        server_.sendHeader("Location", "/", true);
        server_.send(302, "text/plain", "");
    });

    server_.begin();
    active_ = true;

    Serial.println("SetupPortal: active");
    Serial.print("SetupPortal: SSID: ");
    Serial.println(apSsid_);
    Serial.print("SetupPortal: open http://");
    Serial.println(WiFi.softAPIP());

    return true;
}

void SetupPortal::loop() {
    if (!active_) {
        return;
    }

    server_.handleClient();

    if (restartPending_ && millis() >= restartAtMs_) {
        ESP.restart();
    }
}

bool SetupPortal::isActive() const {
    return active_;
}

const String& SetupPortal::getApSsid() const {
    return apSsid_;
}

String SetupPortal::htmlEscape(const String& value) {
    String result;
    result.reserve(value.length() + 8);

    for (size_t i = 0; i < value.length(); ++i) {
        switch (value.charAt(i)) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default: result += value.charAt(i); break;
        }
    }

    return result;
}

String SetupPortal::buildNetworkOptions() {
    String options;

    int count = WiFi.scanNetworks();

    if (count <= 0) {
        return options;
    }

    for (int i = 0; i < count; ++i) {
        String ssid = WiFi.SSID(i);

        options += "<option value=\"";
        options += htmlEscape(ssid);
        options += "\">";
        options += htmlEscape(ssid);
        options += " (";
        options += String(WiFi.RSSI(i));
        options += " dBm)</option>";
    }

    WiFi.scanDelete();
    return options;
}

void SetupPortal::handleRoot() {
    String page;
    page.reserve(5000);

    page += F(
        "<!doctype html><html><head>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>SpoolSense Dryer Setup</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;background:#111;color:#eee;margin:0;padding:24px;}"
        ".card{max-width:520px;margin:auto;background:#1d1d1d;padding:24px;border-radius:12px;}"
        "h1{font-size:24px;margin-top:0;}"
        "label{display:block;margin-top:16px;margin-bottom:6px;}"
        "input,select{box-sizing:border-box;width:100%;padding:11px;border-radius:6px;border:1px solid #555;background:#292929;color:#fff;}"
        "button{width:100%;margin-top:22px;padding:12px;border:0;border-radius:6px;font-weight:bold;font-size:16px;}"
        ".small{font-size:13px;color:#aaa;margin-top:18px;}"
        "</style></head><body><div class='card'>"
        "<h1>SpoolSense Dryer</h1>"
        "<p>Configure Wi-Fi and Spoolman.</p>"
        "<form method='post' action='/save'>"
        "<label for='ssid'>Wi-Fi network</label>"
        "<select id='ssid' name='ssid' required>"
    );

    page += buildNetworkOptions();

    page += F(
        "</select>"
        "<label for='password'>Wi-Fi password</label>"
        "<input id='password' name='password' type='password'>"
        "<label for='spoolman'>Spoolman URL</label>"
        "<input id='spoolman' name='spoolman' type='url' placeholder='http://spoolman.local:7912' required>"
        "<button type='submit'>Save & Restart</button>"
        "</form>"
        "<div class='small'>Setup access point: "
    );

    page += htmlEscape(apSsid_);
    page += F("</div></div></body></html>");

    server_.send(200, "text/html", page);
}

void SetupPortal::handleSave() {
    if (config_ == nullptr) {
        server_.send(500, "text/plain", "Configuration unavailable.");
        return;
    }

    String ssid = server_.arg("ssid");
    String password = server_.arg("password");
    String spoolmanUrl = server_.arg("spoolman");

    ssid.trim();
    spoolmanUrl.trim();

    if (ssid.isEmpty() || spoolmanUrl.isEmpty()) {
        server_.send(400, "text/plain", "Wi-Fi SSID and Spoolman URL are required.");
        return;
    }

    if (!config_->save(ssid, password, spoolmanUrl)) {
        server_.send(500, "text/plain", "Failed to save configuration.");
        return;
    }

    server_.send(
        200,
        "text/html",
        "<!doctype html><html><body style='font-family:Arial;background:#111;color:#eee;padding:24px'>"
        "<h2>Saved</h2><p>SpoolSense Dryer is restarting...</p></body></html>"
    );

    restartPending_ = true;
    restartAtMs_ = millis() + 1500;
}
