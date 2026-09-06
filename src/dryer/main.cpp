#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_PN532.h>

#include "DryerConfig.h"
#include "SetupPortal.h"
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

constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool displayAvailable = false;
bool i2cReady = false;
uint8_t detectedOledAddress = 0;

// ---------- Temperature ----------

OneWire oneWire(PIN_TEMP);
DallasTemperature temperatureSensor(&oneWire);

// ---------- NFC ----------

Adafruit_PN532 nfc(PN532_CS, &SPI);
bool nfcAvailable = false;

// ---------- Connectivity ----------

DryerConfig dryerConfig;
SetupPortal setupPortal;
SpoolmanClient spoolman;

bool wifiConnected = false;

// ---------- Dryer state ----------

enum class DryerStation {
    TOP,
    BOTTOM
};

struct StationState {
    bool occupied = false;
    String uid;
    DryerSpoolInfo spool;
    String lookupError;
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

String getStationDisplayName(const StationState& state) {
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
        return "Unknown spool";
    }

    return state.uid;
}

void drawStationLine(
    int y,
    const char* name,
    DryerStation station,
    const StationState& state
) {
    display.setCursor(0, y);
    display.print(selectedStation == station ? ">" : " ");
    display.print(name);
    display.print(": ");

    String label = getStationDisplayName(state);
    constexpr size_t MAX_LABEL_CHARS = 14;

    if (label.length() > MAX_LABEL_CHARS) {
        label = label.substring(0, MAX_LABEL_CHARS);
    }

    display.print(label);
}

void drawDisplay() {
    if (!displayAvailable) {
        return;
    }

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.println("SpoolSense Dryer");

    drawStationLine(16, "TOP", DryerStation::TOP, topStation);
    drawStationLine(28, "BOT", DryerStation::BOTTOM, bottomStation);

    display.setCursor(0, 44);

    if (!nfcAvailable) {
        display.print("NFC: ERROR");
    } else if (setupPortal.isActive()) {
        display.print("SETUP AP ACTIVE");
    } else if (wifiConnected) {
        display.print("WiFi: Connected");
    } else {
        display.print("WiFi: Offline");
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

void printSpoolDetails(const DryerSpoolInfo& spool) {
    Serial.println("Spoolman match:");
    Serial.print("  Spool ID: ");
    Serial.println(spool.spoolId);
    Serial.print("  Vendor: ");
    Serial.println(spool.vendor);
    Serial.print("  Name: ");
    Serial.println(spool.name);
    Serial.print("  Material: ");
    Serial.println(spool.material);
    Serial.print("  Dry temp: ");

    if (spool.dryTempC > 0) {
        Serial.print(spool.dryTempC);
        Serial.println(" C");
    } else {
        Serial.println("not set");
    }

    Serial.print("  Dry time: ");

    if (spool.dryTimeHours > 0) {
        Serial.print(spool.dryTimeHours);
        Serial.println(" h");
    } else {
        Serial.println("not set");
    }
}

void assignUidToSelectedStation(const String& uid) {
    StationState& station = getSelectedStation();

    station.occupied = true;
    station.uid = uid;
    station.spool = DryerSpoolInfo{};
    station.lookupError = "";

    Serial.print("Assigned UID ");
    Serial.print(uid);
    Serial.print(" to ");
    Serial.println(selectedStation == DryerStation::TOP ? "TOP" : "BOTTOM");

    drawDisplay();

    if (!wifiConnected) {
        station.lookupError = "WiFi offline";
        Serial.println("Spoolman lookup skipped: WiFi offline.");
        drawDisplay();
        return;
    }

    if (!spoolman.isConfigured()) {
        station.lookupError = "Spoolman not configured";
        Serial.println("Spoolman lookup skipped: URL not configured.");
        drawDisplay();
        return;
    }

    String error;
    DryerSpoolInfo spoolInfo;

    Serial.print("Looking up NFC UID in Spoolman: ");
    Serial.println(uid);

    if (spoolman.lookupByUid(uid, spoolInfo, error)) {
        station.spool = spoolInfo;
        station.lookupError = "";
        printSpoolDetails(station.spool);
    } else {
        station.lookupError = error;
        Serial.print("Spoolman lookup failed: ");
        Serial.println(error);
    }

    drawDisplay();
}

// ---------- I2C / OLED ----------

void initOled() {
    Serial.print("I2C begin SDA=");
    Serial.print(OLED_SDA);
    Serial.print(" SCL=");
    Serial.println(OLED_SCL);

    i2cReady = Wire.begin(OLED_SDA, OLED_SCL, 100000);

    Serial.print("I2C begin result: ");
    Serial.println(i2cReady ? "SUCCESS" : "FAILED");

    if (!i2cReady) {
        Serial.println("OLED disabled: I2C controller did not initialize.");
        return;
    }

    Serial.println("I2C scan starting...");

    for (uint8_t address = 1; address < 127; ++address) {
        Wire.beginTransmission(address);
        uint8_t error = Wire.endTransmission();

        if (error == 0) {
            Serial.print("I2C device found at 0x");
            if (address < 0x10) {
                Serial.print("0");
            }
            Serial.println(address, HEX);

            if (address == 0x3C || address == 0x3D) {
                detectedOledAddress = address;
            }
        }

        delay(1);
    }

    if (detectedOledAddress == 0) {
        Serial.println("OLED not found at 0x3C or 0x3D. Display disabled.");
        return;
    }

    Serial.print("OLED detected at 0x");
    Serial.println(detectedOledAddress, HEX);

    // Wire is already initialized above. periphBegin=false prevents the
    // SSD1306 library from reinitializing the ESP32 I2C peripheral.
    if (!display.begin(
            SSD1306_SWITCHCAPVCC,
            detectedOledAddress,
            true,
            false
        )) {
        Serial.println("SSD1306 initialization failed.");
        return;
    }

    displayAvailable = true;
    Serial.println("SSD1306 initialized.");
}

// ---------- Wi-Fi ----------

String makeHostname() {
    uint64_t chipId = ESP.getEfuseMac();
    char hostname[32];

    snprintf(
        hostname,
        sizeof(hostname),
        "spoolsense-dryer-%06llx",
        static_cast<unsigned long long>(chipId & 0xFFFFFFULL)
    );

    return String(hostname);
}

bool connectWiFi() {
    if (!dryerConfig.hasWiFi()) {
        Serial.println("WiFi not configured.");
        wifiConnected = false;
        return false;
    }

    String hostname = makeHostname();

    Serial.print("WiFi hostname: ");
    Serial.println(hostname);

    WiFi.mode(WIFI_MODE_NULL);
    delay(100);
    WiFi.setHostname(hostname.c_str());
    WiFi.mode(WIFI_STA);

    Serial.print("Connecting to WiFi: ");
    Serial.println(dryerConfig.getSSID());

    WiFi.begin(dryerConfig.getSSID(), dryerConfig.getPassword());

    unsigned long start = millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - start < 15000
    ) {
        delay(250);
        Serial.print(".");
    }

    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi connection failed.");
        wifiConnected = false;
        return false;
    }

    wifiConnected = true;

    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());

    return true;
}

void configureSpoolman() {
    if (!dryerConfig.hasSpoolman()) {
        Serial.println("Spoolman URL not configured.");
        return;
    }

    spoolman.setBaseUrl(dryerConfig.getSpoolmanURL());

    Serial.print("Spoolman: ");
    Serial.println(dryerConfig.getSpoolmanURL());
}

void startSetupPortal() {
    if (setupPortal.begin(dryerConfig)) {
        Serial.println();
        Serial.println("=== SpoolSense Dryer Setup ===");
        Serial.print("Connect to WiFi network: ");
        Serial.println(setupPortal.getApSsid());
        Serial.println("Then open: http://192.168.4.1");
        Serial.println("===============================");
        Serial.println();
    }
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
    SPI.begin(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_CS);

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

    dryerConfig.begin();
    configureSpoolman();

    pinMode(PIN_BUTTON_TOP, INPUT_PULLUP);
    pinMode(PIN_BUTTON_BOTTOM, INPUT_PULLUP);

    temperatureSensor.begin();

    initOled();

    Serial.print("DS18B20 devices found: ");
    Serial.println(temperatureSensor.getDeviceCount());

    initNfc();

    drawDisplay();

    if (!connectWiFi()) {
        startSetupPortal();
    }

    Serial.println("Dryer controller booted.");

    drawDisplay();
}

// ---------- Main loop ----------

void loop() {
    setupPortal.loop();
    handleButtons();
    handleNfc();

    unsigned long now = millis();

    if ((now - lastTemperatureRead) >= 2000) {
        lastTemperatureRead = now;

        temperatureSensor.requestTemperatures();
        chamberTempC = temperatureSensor.getTempCByIndex(0);

        if (!setupPortal.isActive()) {
            wifiConnected = WiFi.status() == WL_CONNECTED;
        }

        Serial.print("Chamber temperature: ");
        Serial.print(chamberTempC);
        Serial.println(" C");

        drawDisplay();
    }

    delay(5);
}
