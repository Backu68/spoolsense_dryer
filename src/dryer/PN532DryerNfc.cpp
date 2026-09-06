#include "PN532DryerNfc.h"

#include <SPI.h>
#include <Arduino.h>

PN532DryerNfc::PN532DryerNfc(
    uint8_t sckPin,
    uint8_t misoPin,
    uint8_t mosiPin,
    uint8_t csPin
) :
    sckPin_(sckPin),
    misoPin_(misoPin),
    mosiPin_(mosiPin),
    csPin_(csPin),
    nfc_(csPin, &SPI) {}

bool PN532DryerNfc::begin() {
    available_ = false;

    SPI.begin(sckPin_, misoPin_, mosiPin_, csPin_);
    nfc_.begin();

    uint32_t version = nfc_.getFirmwareVersion();
    if (!version) {
        Serial.println("PN532: reader not detected.");
        return false;
    }

    firmwareMajor_ = static_cast<uint8_t>((version >> 16) & 0xFF);
    firmwareMinor_ = static_cast<uint8_t>((version >> 8) & 0xFF);

    nfc_.SAMConfig();
    available_ = true;

    Serial.print("PN532 ready. Firmware ");
    Serial.print(firmwareMajor_);
    Serial.print(".");
    Serial.println(firmwareMinor_);
    return true;
}

bool PN532DryerNfc::detectTag(
    uint8_t* uid,
    uint8_t* uidLength,
    uint16_t timeoutMs
) {
    if (!available_ || !uid || !uidLength) {
        return false;
    }

    return nfc_.readPassiveTargetID(
        PN532_MIFARE_ISO14443A,
        uid,
        uidLength,
        timeoutMs
    );
}

void PN532DryerNfc::getReaderInfo(char* buf, size_t len) const {
    if (!buf || len == 0) {
        return;
    }

    if (!available_) {
        snprintf(buf, len, "PN532 unavailable");
        return;
    }

    snprintf(
        buf,
        len,
        "PN532 v%u.%u",
        static_cast<unsigned>(firmwareMajor_),
        static_cast<unsigned>(firmwareMinor_)
    );
}
