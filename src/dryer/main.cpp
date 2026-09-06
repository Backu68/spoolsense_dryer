#include <Arduino.h>
#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "DryerConfig.h"
#include "DryerDisplayI.h"
#include "DryerNfcI.h"
#include "DryerRuntimeTypes.h"
#include "PN532DryerNfc.h"
#include "RuntimeWebUI.h"
#include "SSD1306DryerDisplay.h"
#include "SetupPortal.h"
#include "SpoolmanClient.h"

// ---------- Board pins ----------

constexpr uint8_t PIN_TEMP = 27;
constexpr uint8_t PIN_BUTTON_TOP = 32;
constexpr uint8_t PIN_BUTTON_BOTTOM = 33;

// Actual NodeMCU-32S silkscreen labels:
//   OLED SDA -> P21 (GPIO21) -- NOT the nearby RX pin
//   OLED SCL -> P22 (GPIO22) -- NOT the nearby TX pin
constexpr uint8_t OLED_SDA = 21;
constexpr uint8_t OLED_SCL = 22;
constexpr uint8_t OLED_ADDRESS = 0x3C;

constexpr uint8_t PN532_SCK  = 18;
constexpr uint8_t PN532_MISO = 19;
constexpr uint8_t PN532_MOSI = 23;
constexpr uint8_t PN532_CS   = 25;

// ---------- Hardware backends ----------
//
// Application code below talks only to DryerDisplayI / DryerNfcI. Additional
// SpoolSense-supported displays and NFC readers can be added as backends without
// changing the dryer session logic.

SSD1306DryerDisplay ssd1306Display(OLED_SDA, OLED_SCL, OLED_ADDRESS);
DryerDisplayI* activeDisplay = &ssd1306Display;

PN532DryerNfc pn532Reader(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_CS);
DryerNfcI* activeNfc = &pn532Reader;
bool nfcAvailable = false;

// ---------- Temperature ----------

OneWire oneWire(PIN_TEMP);
DallasTemperature temperatureSensor(&oneWire);

// ---------- Connectivity ----------

DryerConfig dryerConfig;
SetupPortal setupPortal;
RuntimeWebUI runtimeWebUI;
SpoolmanClient spoolman;

bool wifiConnected = false;

// ---------- Dryer state ----------

struct StationState {
    bool occupied = false;
    String uid;
    DryerSpoolInfo spool;
    String lookupError;

    DryingSessionState sessionState = DryingSessionState::INACTIVE;
    uint32_t totalSeconds = 0;
    uint32_t remainingSeconds = 0;
    int startThresholdC = 0;
    unsigned long lastSessionTickMs = 0;
};

// A single-spool dryer uses the lower station, so BOTTOM is the natural
// power-on/default target. TOP remains available when the second station is used.
DryerStation selectedStation = DryerStation::BOTTOM;
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

constexpr int SESSION_START_OFFSET_C = 5;

// ---------- Forward declarations ----------

DryerRuntimeStatus getRuntimeStatus();
void drawDisplay();

// ---------- Helpers ----------

bool chamberTemperatureValid() {
    return chamberTempC != DEVICE_DISCONNECTED_C &&
           chamberTempC >= -55.0f &&
           chamberTempC <= 125.0f;
}

String uidToString(const uint8_t* uid, uint8_t uidLength) {
    String result;

    for (uint8_t i = 0; i < uidLength; ++i) {
        if (uid[i] < 0x10) {
            result += "0";
        }
        result += String(uid[i], HEX);
    }

    result.toUpperCase();
    return result;
}

StationState& getSelectedStation() {
    return selectedStation == DryerStation::TOP
        ? topStation
        : bottomStation;
}

const char* sessionStateText(DryingSessionState state) {
    switch (state) {
        case DryingSessionState::WAITING_FOR_TEMP:
            return "Waiting for temp";
        case DryingSessionState::DRYING:
            return "Drying";
        case DryingSessionState::COMPLETE:
            return "Complete";
        case DryingSessionState::INACTIVE:
        default:
            return "Inactive";
    }
}

void configureStationSession(StationState& state, const char* stationName) {
    state.sessionState = DryingSessionState::INACTIVE;
    state.totalSeconds = 0;
    state.remainingSeconds = 0;
    state.startThresholdC = 0;
    state.lastSessionTickMs = 0;

    if (!state.spool.found ||
        state.spool.dryTempC <= 0 ||
        state.spool.dryTimeHours <= 0) {
        Serial.print(stationName);
        Serial.println(" drying session inactive: dry profile is incomplete.");
        return;
    }

    state.totalSeconds =
        static_cast<uint32_t>(state.spool.dryTimeHours) * 3600UL;
    state.remainingSeconds = state.totalSeconds;
    state.startThresholdC = state.spool.dryTempC - SESSION_START_OFFSET_C;
    if (state.startThresholdC < 0) {
        state.startThresholdC = 0;
    }
    state.sessionState = DryingSessionState::WAITING_FOR_TEMP;

    Serial.print(stationName);
    Serial.print(" session waiting for ");
    Serial.print(state.startThresholdC);
    Serial.print(" C; duration ");
    Serial.print(state.spool.dryTimeHours);
    Serial.println(" h.");
}

void updateStationSession(
    StationState& state,
    const char* stationName,
    unsigned long now
) {
    if (state.sessionState == DryingSessionState::WAITING_FOR_TEMP) {
        if (
            chamberTemperatureValid() &&
            chamberTempC >= static_cast<float>(state.startThresholdC)
        ) {
            state.sessionState = DryingSessionState::DRYING;
            state.lastSessionTickMs = now;

            Serial.print(stationName);
            Serial.print(" drying started at chamber temperature ");
            Serial.print(chamberTempC, 2);
            Serial.println(" C.");

            drawDisplay();
        }
        return;
    }

    if (state.sessionState != DryingSessionState::DRYING) {
        return;
    }

    unsigned long elapsedMs = now - state.lastSessionTickMs;
    if (elapsedMs < 1000UL) {
        return;
    }

    uint32_t elapsedSeconds = elapsedMs / 1000UL;
    state.lastSessionTickMs += elapsedSeconds * 1000UL;

    if (elapsedSeconds >= state.remainingSeconds) {
        state.remainingSeconds = 0;
        state.sessionState = DryingSessionState::COMPLETE;

        Serial.print(stationName);
        Serial.println(" drying complete.");
        drawDisplay();
        return;
    }

    state.remainingSeconds -= elapsedSeconds;
}

DryerStationView makeRuntimeStationView(const StationState& state) {
    DryerStationView view;
    view.occupied = state.occupied;
    view.uid = state.uid;
    view.spool = state.spool;
    view.lookupError = state.lookupError;
    view.sessionStatus = sessionStateText(state.sessionState);
    view.remainingSeconds = state.remainingSeconds;
    view.startThresholdC = state.startThresholdC;
    return view;
}

DryerRuntimeStatus getRuntimeStatus() {
    DryerRuntimeStatus status;
    status.chamberTempC = chamberTempC;
    status.temperatureValid = chamberTemperatureValid();
    status.wifiConnected = wifiConnected;
    if (wifiConnected) {
        status.ipAddress = WiFi.localIP().toString();
    }
    status.spoolmanConfigured = dryerConfig.hasSpoolman();
    status.nfcAvailable = nfcAvailable;
    status.setupPortalActive = setupPortal.isActive();
    status.selectedStation = selectedStation;
    status.top = makeRuntimeStationView(topStation);
    status.bottom = makeRuntimeStationView(bottomStation);
    return status;
}

void drawDisplay() {
    if (activeDisplay != nullptr && activeDisplay->isAvailable()) {
        activeDisplay->render(getRuntimeStatus());
    }
}

void clearRuntimeStation(bool top) {
    StationState& station = top ? topStation : bottomStation;
    station = StationState{};

    // Allow the same tag to be rescanned immediately after a manual clear.
    lastSeenUid = "";
    lastTagSeenMs = 0;

    Serial.print("Cleared station: ");
    Serial.println(top ? "TOP" : "BOTTOM");
    drawDisplay();
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
    const char* stationName =
        selectedStation == DryerStation::TOP ? "TOP" : "BOTTOM";

    // A new scan replaces the prior station assignment and session.
    station = StationState{};
    station.occupied = true;
    station.uid = uid;

    Serial.print("Assigned UID ");
    Serial.print(uid);
    Serial.print(" to ");
    Serial.println(stationName);

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
        configureStationSession(station, stationName);
    } else {
        station.lookupError = error;
        Serial.print("Spoolman lookup failed: ");
        Serial.println(error);
    }

    drawDisplay();
}

// ---------- Display ----------

void initDisplay() {
    if (activeDisplay == nullptr || !activeDisplay->begin()) {
        Serial.println("Dryer display unavailable.");
        return;
    }

    char info[48];
    activeDisplay->getDisplayInfo(info, sizeof(info));
    Serial.print("Dryer display: ");
    Serial.println(info);
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

    // Known-good SpoolSense DHCP hostname startup order.
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
    if (!setupPortal.begin(dryerConfig)) {
        return;
    }

    Serial.println();
    Serial.println("=== SpoolSense Dryer Setup ===");
    Serial.print("Connect to WiFi network: ");
    Serial.println(setupPortal.getApSsid());
    Serial.println("Then open: http://192.168.4.1");
    Serial.println("===============================");
    Serial.println();
}

void startRuntimeWebUI() {
    if (!runtimeWebUI.begin(getRuntimeStatus, clearRuntimeStation)) {
        Serial.println("RuntimeWebUI: failed to start.");
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
    nfcAvailable = activeNfc != nullptr && activeNfc->begin();

    if (!nfcAvailable) {
        Serial.println("Dryer NFC reader unavailable.");
        return;
    }

    char info[48];
    activeNfc->getReaderInfo(info, sizeof(info));
    Serial.print("Dryer NFC reader: ");
    Serial.println(info);
}

void handleNfc() {
    if (!nfcAvailable || activeNfc == nullptr) {
        return;
    }

    uint8_t uid[10];
    uint8_t uidLength = 0;

    if (!activeNfc->detectTag(uid, &uidLength, 20)) {
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
    initDisplay();

    Serial.print("DS18B20 devices found: ");
    Serial.println(temperatureSensor.getDeviceCount());

    initNfc();
    drawDisplay();

    if (!connectWiFi()) {
        startSetupPortal();
    } else {
        startRuntimeWebUI();
    }

    Serial.println("Dryer controller booted.");
    drawDisplay();
}

// ---------- Main loop ----------

void loop() {
    setupPortal.loop();
    runtimeWebUI.loop();
    handleButtons();
    handleNfc();

    unsigned long now = millis();

    updateStationSession(topStation, "TOP", now);
    updateStationSession(bottomStation, "BOTTOM", now);

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
