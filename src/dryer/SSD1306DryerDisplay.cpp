#include "SSD1306DryerDisplay.h"

#include <Wire.h>
#include <Adafruit_GFX.h>

namespace {
constexpr int DISPLAY_WIDTH = 128;
constexpr int DISPLAY_HEIGHT = 64;
constexpr float SETPOINT_TOLERANCE_C = 1.0f;
}

SSD1306DryerDisplay::SSD1306DryerDisplay(
    uint8_t sdaPin,
    uint8_t sclPin,
    uint8_t address
) :
    sdaPin_(sdaPin),
    sclPin_(sclPin),
    address_(address),
    display_(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, -1) {}

bool SSD1306DryerDisplay::begin() {
    available_ = false;

    if (!Wire.begin(sdaPin_, sclPin_, 100000)) {
        Serial.println("SSD1306: I2C controller failed to initialize.");
        return false;
    }

    Wire.beginTransmission(address_);
    if (Wire.endTransmission() != 0) {
        Serial.print("SSD1306: display not detected at 0x");
        Serial.println(address_, HEX);
        return false;
    }

    if (!display_.begin(SSD1306_SWITCHCAPVCC, address_, true, false)) {
        Serial.println("SSD1306: initialization failed.");
        return false;
    }

    available_ = true;
    Serial.print("SSD1306 ready at 0x");
    Serial.println(address_, HEX);
    return true;
}

bool SSD1306DryerDisplay::isAvailable() const {
    return available_;
}

void SSD1306DryerDisplay::getDisplayInfo(char* buf, size_t len) const {
    if (!buf || len == 0) {
        return;
    }

    snprintf(buf, len, "SSD1306 128x64 I2C");
}

String SSD1306DryerDisplay::stationSpoolLabel(const DryerStationView& state) {
    if (!state.occupied) {
        return "Empty";
    }

    if (state.spool.found) {
        if (!state.spool.name.isEmpty()) {
            return state.spool.name;
        }
        if (!state.spool.material.isEmpty()) {
            return state.spool.material;
        }
    }

    if (!state.lookupError.isEmpty()) {
        return "Unknown";
    }

    return state.uid;
}

String SSD1306DryerDisplay::sessionCompact(const DryerStationView& state) {
    if (state.sessionStatus == "Waiting for temp") {
        return "WAIT";
    }
    if (state.sessionStatus == "Complete") {
        return "DONE";
    }
    if (state.sessionStatus != "Drying") {
        return "--";
    }

    uint32_t hours = state.remainingSeconds / 3600UL;
    uint32_t minutes = (state.remainingSeconds % 3600UL) / 60UL;
    uint32_t seconds = state.remainingSeconds % 60UL;

    char buffer[12];
    if (hours > 0) {
        snprintf(
            buffer,
            sizeof(buffer),
            "%lu:%02lu",
            static_cast<unsigned long>(hours),
            static_cast<unsigned long>(minutes)
        );
    } else {
        snprintf(
            buffer,
            sizeof(buffer),
            "%02lu:%02lu",
            static_cast<unsigned long>(minutes),
            static_cast<unsigned long>(seconds)
        );
    }

    return String(buffer);
}

void SSD1306DryerDisplay::drawStationLine(
    int y,
    const DryerStationView& state,
    DryerStationId selectedStation
) {
    display_.setCursor(0, y);
    display_.print(selectedStation == state.id ? ">" : " ");

    String stationName = state.label;
    if (stationName.length() > 6) {
        stationName = stationName.substring(0, 6);
    }
    display_.print(stationName);
    display_.print(":");

    String label = stationSpoolLabel(state);
    constexpr size_t MAX_LABEL_CHARS = 7;
    if (label.length() > MAX_LABEL_CHARS) {
        label = label.substring(0, MAX_LABEL_CHARS);
    }
    display_.print(label);

    if (state.occupied) {
        display_.print(" ");
        display_.print(sessionCompact(state));
    }
}

void SSD1306DryerDisplay::render(const DryerRuntimeStatus& status) {
    if (!available_ || status.selectedStation >= DRYER_STATION_COUNT) {
        return;
    }

    display_.clearDisplay();
    display_.setTextColor(SSD1306_WHITE);

    const DryerStationView& selected = status.stations[status.selectedStation];
    const DryerZoneView& zone = status.zones[selected.zone];

    bool zoneHasActiveSession = false;
    for (DryerStationId i = 0; i < DRYER_STATION_COUNT; ++i) {
        const DryerStationView& station = status.stations[i];
        if (station.zone != selected.zone) {
            continue;
        }
        if (
            station.sessionStatus == "Waiting for temp" ||
            station.sessionStatus == "Drying"
        ) {
            zoneHasActiveSession = true;
            break;
        }
    }

    bool showLargeSetpoint = false;
    if (
        zoneHasActiveSession &&
        zone.temperaturePlan.automaticPlanUsable &&
        zone.temperaturePlan.recommendedTargetC > 0
    ) {
        if (!zone.temperatureValid) {
            showLargeSetpoint = true;
        } else {
            float difference =
                zone.chamberTempC -
                static_cast<float>(zone.temperaturePlan.recommendedTargetC);
            if (difference < 0.0f) {
                difference = -difference;
            }
            showLargeSetpoint = difference > SETPOINT_TOLERANCE_C;
        }
    }

    if (showLargeSetpoint) {
        String target = String(zone.temperaturePlan.recommendedTargetC) + "C";

        display_.setTextSize(5);
        int16_t x1 = 0;
        int16_t y1 = 0;
        uint16_t width = 0;
        uint16_t height = 0;
        display_.getTextBounds(target, 0, 0, &x1, &y1, &width, &height);

        int16_t x = static_cast<int16_t>((DISPLAY_WIDTH - width) / 2);
        int16_t y = static_cast<int16_t>((DISPLAY_HEIGHT - height) / 2);
        if (x < 0) {
            x = 0;
        }
        if (y < 0) {
            y = 0;
        }

        display_.setCursor(x, y);
        display_.print(target);
        display_.display();
        return;
    }

    display_.setTextSize(1);
    display_.setCursor(0, 0);
    display_.print("SpoolSense ");
    display_.print(status.selectedStation + 1);
    display_.print("/");
    display_.println(DRYER_STATION_COUNT);

    if (DRYER_STATION_COUNT <= 2) {
        for (DryerStationId i = 0; i < DRYER_STATION_COUNT; ++i) {
            drawStationLine(16 + (12 * i), status.stations[i], status.selectedStation);
        }
    } else {
        drawStationLine(16, selected, status.selectedStation);
        DryerStationId next =
            static_cast<DryerStationId>((status.selectedStation + 1) % DRYER_STATION_COUNT);
        drawStationLine(28, status.stations[next], status.selectedStation);
    }

    display_.setCursor(0, 44);
    if (!status.nfcAvailable) {
        display_.print("NFC: ERROR");
    } else if (status.setupPortalActive) {
        display_.print("SETUP AP ACTIVE");
    } else if (status.wifiConnected && !status.ipAddress.isEmpty()) {
        display_.print("IP: ");
        display_.print(status.ipAddress);
    } else {
        display_.print("WiFi: Offline");
    }

    display_.setCursor(0, 54);
    if (DRYER_ZONE_COUNT > 1) {
        display_.print("Z");
        display_.print(selected.zone + 1);
        display_.print(" ");
    }
    display_.print("Temp: ");
    if (!zone.temperatureValid) {
        display_.print("--.- C");
    } else {
        display_.print(zone.chamberTempC, 1);
        display_.print(" C");
    }

    display_.display();
}
