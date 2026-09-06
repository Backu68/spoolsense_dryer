#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_PN532.h>
#include <WiFi.h>

#include "DryerConfig.h"
#include "SpoolmanClient.h"

// ---------- Pins ----------

constexpr uint8_t PIN_TEMP = 27;

constexpr uint8_t PIN_BUTTON_TOP = 32;
constexpr uint8_t PIN_BUTTON_BOTTOM = 33;

constexpr uint8_t OLED_SDA = 21;
constexpr uint8_t OLED_SCL = 22;

constexpr uint8_t PN532_SCK  = 18;
constexpr uint8_t PN532_MISO = 19;
constexpr uint8_t PN532_MOSI = 23;
constexpr uint8_t PN532_CS   = 25;

// ---------- OLED ----------

constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// ---------- Temperature ----------

OneWire oneWire(PIN_TEMP);
DallasTemperature temperatureSensor(&oneWire);

// ---------- NFC ----------

Adafruit_PN532 nfc(PN532_CS, &SPI);

bool nfcAvailable = false;

// ---------- Dryer state ----------

enum class DryerStation {
    TOP,
    BOTTOM
};

struct StationState {
    bool occupied = false;
    String uid;
};

DryerStation selectedStation = DryerStation::TOP;

StationState topStation;
StationState bottomStation;

// ---------- Buttons ----------

constexpr unsigned long BUTTON_DEBOUNCE_MS = 40;

bool lastTopReading = HIGH;
bool lastBottomReading = HIGH;

bool stableTopState = HIGH;
bool stableBottomState = HIGH;

unsigned long topDebounceTime = 0;
unsigned long bottomDebounceTime = 0;

// ---------- Timing ----------

unsigned long lastTemperatureRead = 0;
float chamberTempC = DEVICE_DISCONNECTED_C;

// Prevent the same tag from firing continuously while held on reader
String lastSeenUid;
unsigned long lastTagSeenMs = 0;
constexpr unsigned long TAG_REPEAT_BLOCK_MS = 2000;

// ---------- Helpers ----------

String uidToString(const uint8_t* uid, uint8_t uidLength) {
    String result;

    for (uint8_t i = 0; i < uidLength; i++) {
        if (uid[i] < 0x10) {
            result += "0";
        }

        result += String(uid[i], HEX);
    }

    result.toUpperCase();
    return result;
}

StationState& getSelectedStation() {
    if (selectedStation == DryerStation::TOP) {
        return topStation;
    }

    return bottomStation;
}

void drawStationLine(
    int y,
    const char* name,
    DryerStation station,
    const StationState& state
) {
    display.setCursor(0, y);

    if (selectedStation == station) {
        display.print(">");
    } else {
        display.print(" ");
    }

    display.print(name);
    display.print(": ");

    if (!state.occupied) {
        display.print("Empty");
        return;
    }

    // Temporary display until Spoolman gives us the actual spool name.
    if (state.uid.length() <= 10) {
        display.print(state.uid);
    } else {
        display.print(state.uid.substring(0, 10));
    }
}

void drawDisplay() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.println("SpoolSense Dryer");

    drawStationLine(
        16,
        "TOP",
        DryerStation::TOP,
        topStation
    );

    drawStationLine(
        28,
        "BOT",
        DryerStation::BOTTOM,
        bottomStation
    );

    display.setCursor(0, 44);

    if (nfcAvailable) {
        display.print("NFC: Ready");
    } else {
        display.print("NFC: ERROR");
    }

    display.setCursor(0, 54);
    display.print("Temp: ");

    if (chamberTempC == DEVICE_DISCONNECTED_C) {
        display.print("--.- C");
    } else {
        display.print(chamberTempC, 1);
        display.print(" C");
    }

    display.display();
}

void assignUidToSelectedStation(const String& uid) {
    StationState& station = getSelectedStation();

    station.occupied = true;
    station.uid = uid;

    Serial.print("Assigned UID ");
    Serial.print(uid);
    Serial.print(" to ");

    if (selectedStation == DryerStation::TOP) {
        Serial.println("TOP");
    } else {
        Serial.println("BOTTOM");
    }

    drawDisplay();
}

// ---------- Buttons ----------

void handleButtons() {
    unsigned long now = millis();

    bool topReading = digitalRead(PIN_BUTTON_TOP);
    bool bottomReading = digitalRead(PIN_BUTTON_BOTTOM);

    if (topReading != lastTopReading) {
        topDebounceTime = now;
        lastTopReading = topReading;
    }

    if ((now - topDebounceTime) >= BUTTON_DEBOUNCE_MS) {
        if (topReading != stableTopState) {
            stableTopState = topReading;

            if (stableTopState == LOW) {
                selectedStation = DryerStation::TOP;
                Serial.println("Selected station: TOP");
                drawDisplay();
            }
        }
    }

    if (bottomReading != lastBottomReading) {
        bottomDebounceTime = now;
        lastBottomReading = bottomReading;
    }

    if ((now - bottomDebounceTime) >= BUTTON_DEBOUNCE_MS) {
        if (bottomReading != stableBottomState) {
            stableBottomState = bottomReading;

            if (stableBottomState == LOW) {
                selectedStation = DryerStation::BOTTOM;
                Serial.println("Selected station: BOTTOM");
                drawDisplay();
            }
        }
    }
}

// ---------- NFC ----------

void initNfc() {
    SPI.begin(
        PN532_SCK,
        PN532_MISO,
        PN532_MOSI,
        PN532_CS
    );

    nfc.begin();

    uint32_t version = nfc.getFirmwareVersion();

    if (!version) {
        Serial.println("PN532 not detected.");
        nfcAvailable = false;
        return;
    }

    Serial.print("PN532 detected. Firmware ");
    Serial.print((version >> 16) & 0xFF);
    Serial.print(".");
    Serial.println((version >> 8) & 0xFF);

    nfc.SAMConfig();

    nfcAvailable = true;

    Serial.println("PN532 ready for ISO14443A tags.");
}

void handleNfc() {
    if (!nfcAvailable) {
        return;
    }

    uint8_t uid[7];
    uint8_t uidLength = 0;

    bool found = nfc.readPassiveTargetID(
        PN532_MIFARE_ISO14443A,
        uid,
        &uidLength,
        20
    );

    if (!found) {
        return;
    }

    String uidString = uidToString(uid, uidLength);

    unsigned long now = millis();

    if (
        uidString == lastSeenUid &&
        (now - lastTagSeenMs) < TAG_REPEAT_BLOCK_MS
    ) {
        return;
    }

    lastSeenUid = uidString;
    lastTagSeenMs = now;

    Serial.print("NFC tag detected: ");
    Serial.println(uidString);

    assignUidToSelectedStation(uidString);
}

// ---------- Setup ----------

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("SpoolSense Dryer");
    Serial.println("Firmware " FIRMWARE_VERSION);

    pinMode(PIN_BUTTON_TOP, INPUT_PULLUP);
    pinMode(PIN_BUTTON_BOTTOM, INPUT_PULLUP);

    temperatureSensor.begin();

    Wire.begin(OLED_SDA, OLED_SCL);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println("SSD1306 initialization failed.");
    } else {
        Serial.println("SSD1306 initialized.");
    }

    Serial.print("DS18B20 devices found: ");
    Serial.println(temperatureSensor.getDeviceCount());

    initNfc();

    Serial.println("Dryer controller booted.");

    drawDisplay();
}

// ---------- Main loop ----------

void loop() {
    handleButtons();
    handleNfc();

    unsigned long now = millis();

    if ((now - lastTemperatureRead) >= 2000) {
        lastTemperatureRead = now;

        temperatureSensor.requestTemperatures();
        chamberTempC = temperatureSensor.getTempCByIndex(0);

        Serial.print("Chamber temperature: ");
        Serial.print(chamberTempC);
        Serial.println(" C");

        drawDisplay();
    }

    delay(5);
}