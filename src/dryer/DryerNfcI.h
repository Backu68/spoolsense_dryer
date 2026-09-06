#pragma once

#include <cstddef>
#include <cstdint>

// Minimal dryer NFC abstraction. It intentionally mirrors the official
// SpoolSense NFCConnectionI pattern without pulling scanner-only tag-writing
// dependencies into the dryer application.
class DryerNfcI {
public:
    virtual ~DryerNfcI() = default;

    virtual bool begin() = 0;
    virtual bool detectTag(
        uint8_t* uid,
        uint8_t* uidLength,
        uint16_t timeoutMs = 20
    ) = 0;
    virtual void getReaderInfo(char* buf, size_t len) const = 0;
};
