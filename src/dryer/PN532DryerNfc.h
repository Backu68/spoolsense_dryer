#pragma once

#include <Adafruit_PN532.h>

#include "DryerNfcI.h"

class PN532DryerNfc : public DryerNfcI {
public:
    PN532DryerNfc(
        uint8_t sckPin,
        uint8_t misoPin,
        uint8_t mosiPin,
        uint8_t csPin
    );

    bool begin() override;
    bool detectTag(
        uint8_t* uid,
        uint8_t* uidLength,
        uint16_t timeoutMs = 20
    ) override;
    void getReaderInfo(char* buf, size_t len) const override;

private:
    uint8_t sckPin_;
    uint8_t misoPin_;
    uint8_t mosiPin_;
    uint8_t csPin_;
    uint8_t firmwareMajor_ = 0;
    uint8_t firmwareMinor_ = 0;
    bool available_ = false;
    Adafruit_PN532 nfc_;
};
