#pragma once

#include <Arduino.h>
#include <Adafruit_SSD1306.h>

#include "DryerDisplayI.h"

class SSD1306DryerDisplay : public DryerDisplayI {
public:
    SSD1306DryerDisplay(uint8_t sdaPin, uint8_t sclPin, uint8_t address = 0x3C);

    bool begin() override;
    bool isAvailable() const override;
    void render(const DryerRuntimeStatus& status) override;
    void getDisplayInfo(char* buf, size_t len) const override;

private:
    void drawStationLine(
        int y,
        const DryerStationView& state,
        DryerStationId selectedStation
    );

    static String stationSpoolLabel(const DryerStationView& state);
    static String sessionCompact(const DryerStationView& state);

    uint8_t sdaPin_;
    uint8_t sclPin_;
    uint8_t address_;
    bool available_ = false;
    Adafruit_SSD1306 display_;
};
