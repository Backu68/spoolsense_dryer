#include <Arduino.h>
#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "DryerConfig.h"
#include "DryerDisplayI.h"
#include "DryerNfcI.h"
#include "DryerRuntimeTypes.h"
#include "DryerSessionStore.h"
#include "DryerTemperaturePlanner.h"
#include "DryerTimeService.h"
#include "PN532DryerNfc.h"
#include "RuntimeWebUI.h"
#include "SSD1306DryerDisplay.h"
#include "SetupPortal.h"
#include "SpoolmanClient.h"

// ---------- Board pins ----------

constexpr uint8_t PIN_TEMP = 27;
constexpr uint8_t PIN_BUTTON_SELECT = 32;

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

// ---------- Connectivity / persistence ----------

DryerConfig dryerConfig;
DryerSessionStore sessionStore;
DryerTimeService timeService;
SetupPortal setupPortal;
RuntimeWebUI runtimeWebUI;
SpoolmanClient spoolman;

bool wifiConnected = false;

// ---------- Dryer state ----------

// A single-spool dryer uses the lower station, so BOTTOM is the natural
// power-on/default target. The one selector button cycles through stations.
DryerStation selectedStation = DryerStation::BOTTOM;
DryerStationState topStation;
DryerStationState bottomStation;

// Current hardware is one thermal zone shared by both spool stations. The
// planner accepts an arbitrary number of profiles so it can survive the later
// station/zone-count refactor without changing its math.
DryerTemperaturePlan temperaturePlan;

// ---------- Button ----------

constexpr unsigned long BUTTON_DEBOUNCE_MS = 40;

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
unsigned long buttonDebounceTime = 0;

// ---------- Timing ----------

unsigned long lastTemperatureRead = 0;
float chamberTempC = DEVICE_DISCONNECTED_C;

String lastSeenUid;
unsigned long lastTagSeenMs = 0;
constexpr unsigned long TAG_REPEAT_BLOCK_MS = 2000;

constexpr int SESSION_START_OFFSET_C = 5;
constexpr unsigned long SESSION_PERSIST_INTERVAL_MS = 60000UL;

// ---------- Forward declarations ----------

DryerRuntimeStatus getRuntimeStatus();
void drawDisplay();
void recalculateTemperaturePlan();

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

DryerStationState& getSelectedStation() {
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

void persistStation(DryerStation station, DryerStationState& state) {
    if (sessionStore.save(station, state)) {
        state.lastPersistMs = millis();
    }
}

uint8_t occupiedStationCount() {
    uint8_t count = 0;
    if (topStation.occupied) {
        ++count;
    }
    if (bottomStation.occupied) {
        ++count;
    }
    return count;
}

bool temperaturePlanBlocksNewStarts() {
    return occupiedStationCount() > 1 && !temperaturePlan.automaticPlanUsable;
}

void updateWaitingThreshold(
    DryerStation station,
    DryerStationState& state,
    int thresholdC
) {
    if (
        state.sessionState != DryingSessionState::WAITING_FOR_TEMP ||
        state.startThresholdC == thresholdC
    ) {
        return;
    }

    state.startThresholdC = thresholdC;
    persistStation(station, state);
}

void printTemperaturePlan() {
    Serial.print("Temperature plan: ");
    Serial.println(DryerTemperaturePlanner::stateText(temperaturePlan.state));

    if (temperaturePlan.spoolCount == 0) {
        return;
    }

    if (temperaturePlan.automaticPlanUsable) {
        Serial.print("  Shared target: ");
        Serial.print(temperaturePlan.recommendedTargetC);
        Serial.println(" C");

        Serial.print("  Shared range: ");
        Serial.print(temperaturePlan.commonMinC);
        Serial.print("-");
        Serial.print(temperaturePlan.commonMaxC);
        Serial.println(" C");
        return;
    }

    if (temperaturePlan.state == DryerTemperaturePlanState::COMPROMISE_REQUIRED) {
        Serial.println("  WARNING: loaded drying ranges do not overlap.");
        Serial.print("  Lowest required minimum: ");
        Serial.print(temperaturePlan.commonMinC);
        Serial.println(" C");
        Serial.print("  Highest universally non-overtemperature candidate: ");
        Serial.print(temperaturePlan.recommendedTargetC);
        Serial.println(" C");
        Serial.print("  Range gap: ");
        Serial.print(temperaturePlan.compromiseGapC);
        Serial.println(" C");
        Serial.println("  Automatic compromise is disabled until time compensation is validated.");
    } else if (temperaturePlan.state == DryerTemperaturePlanState::INVALID_PROFILE) {
        Serial.println("  WARNING: a loaded spool is missing a usable drying profile.");
    }
}

void recalculateTemperaturePlan() {
    // An occupied station whose spool cannot be resolved is intentionally a
    // blocker. We cannot safely recommend a shared chamber temperature when one
    // loaded filament's limits are unknown.
    bool unresolvedLoadedSpool =
        (topStation.occupied && !topStation.spool.found) ||
        (bottomStation.occupied && !bottomStation.spool.found);

    const DryerSpoolInfo* profiles[2] = {
        topStation.occupied ? &topStation.spool : nullptr,
        bottomStation.occupied ? &bottomStation.spool : nullptr
    };

    temperaturePlan = DryerTemperaturePlanner::calculate(profiles, 2);

    if (unresolvedLoadedSpool) {
        temperaturePlan.state = DryerTemperaturePlanState::INVALID_PROFILE;
        temperaturePlan.automaticPlanUsable = false;
    }

    printTemperaturePlan();

    if (!temperaturePlan.automaticPlanUsable ||
        temperaturePlan.recommendedTargetC <= 0) {
        return;
    }

    int sharedThresholdC =
        temperaturePlan.recommendedTargetC - SESSION_START_OFFSET_C;
    if (sharedThresholdC < 0) {
        sharedThresholdC = 0;
    }

    // A newly loaded spool can change the shared target at any time. Waiting
    // stations adopt that new target immediately. Already-running sessions keep
    // their original wall-clock deadline because the new target is still inside
    // their common valid drying range.
    updateWaitingThreshold(
        DryerStation::TOP,
        topStation,
        sharedThresholdC
    );
    updateWaitingThreshold(
        DryerStation::BOTTOM,
        bottomStation,
        sharedThresholdC
    );
}

void restoreSessions() {
    if (sessionStore.load(DryerStation::TOP, topStation)) {
        Serial.print("Restored TOP session: ");
        Serial.print(sessionStateText(topStation.sessionState));
        Serial.print(", remaining ");
        Serial.print(topStation.remainingSeconds);
        Serial.print(" s, finish epoch ");
        Serial.println(topStation.finishEpoch);
    }

    if (sessionStore.load(DryerStation::BOTTOM, bottomStation)) {
        Serial.print("Restored BOTTOM session: ");
        Serial.print(sessionStateText(bottomStation.sessionState));
        Serial.print(", remaining ");
        Serial.print(bottomStation.remainingSeconds);
        Serial.print(" s, finish epoch ");
        Serial.println(bottomStation.finishEpoch);
    }
}

void completeStationSession(
    DryerStation station,
    DryerStationState& state,
    const char* stationName
) {
    state.remainingSeconds = 0;
    state.finishEpoch = 0;
    state.sessionState = DryingSessionState::COMPLETE;

    Serial.print(stationName);
    Serial.println(" drying complete.");
    persistStation(station, state);
    drawDisplay();
}

void configureStationSession(
    DryerStation station,
    DryerStationState& state,
    const char* stationName
) {
    state.sessionState = DryingSessionState::INACTIVE;
    state.totalSeconds = 0;
    state.remainingSeconds = 0;
    state.startThresholdC = 0;
    state.finishEpoch = 0;
    state.lastSessionTickMs = 0;

    if (!state.spool.found ||
        state.spool.dryTempC <= 0 ||
        state.spool.dryTimeHours <= 0) {
        Serial.print(stationName);
        Serial.println(" drying session inactive: dry profile is incomplete.");
        persistStation(station, state);
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

    persistStation(station, state);
}

void updateStationSession(
    DryerStation station,
    DryerStationState& state,
    const char* stationName,
    unsigned long now
) {
    if (state.sessionState == DryingSessionState::WAITING_FOR_TEMP) {
        if (temperaturePlanBlocksNewStarts()) {
            return;
        }

        // The lower bound preserves the locked target-5 C rule. The upper
        // bound prevents a newly-added lower-temperature spool from starting
        // immediately while the chamber is still sitting at the old hotter
        // setpoint. It must first settle near the newly calculated target.
        int activeTargetC = state.spool.dryTempC;
        if (
            temperaturePlan.automaticPlanUsable &&
            temperaturePlan.recommendedTargetC > 0
        ) {
            activeTargetC = temperaturePlan.recommendedTargetC;
        }
        int upperStartC = activeTargetC + SESSION_START_OFFSET_C;

        if (
            chamberTemperatureValid() &&
            chamberTempC >= static_cast<float>(state.startThresholdC) &&
            chamberTempC <= static_cast<float>(upperStartC)
        ) {
            state.sessionState = DryingSessionState::DRYING;
            state.lastSessionTickMs = now;

            if (timeService.isSynced()) {
                state.finishEpoch =
                    timeService.nowEpoch() + state.remainingSeconds;
            } else {
                state.finishEpoch = 0;
            }

            Serial.print(stationName);
            Serial.print(" drying started at chamber temperature ");
            Serial.print(chamberTempC, 2);
            Serial.print(" C");
            if (state.finishEpoch > 0) {
                Serial.print("; finish epoch ");
                Serial.print(state.finishEpoch);
            }
            Serial.println(".");

            persistStation(station, state);
            drawDisplay();
        }
        return;
    }

    if (state.sessionState != DryingSessionState::DRYING) {
        return;
    }

    if (state.remainingSeconds == 0) {
        completeStationSession(station, state, stationName);
        return;
    }

    // Preferred path: absolute UTC deadline. This lets the ESP32 reconstruct
    // remaining time after a reboot or controller-only power outage.
    if (timeService.isSynced()) {
        uint32_t epochNow = timeService.nowEpoch();

        // Sessions created before NTP was available (or before the wall-clock
        // persistence upgrade) are migrated in-place without losing progress.
        if (state.finishEpoch == 0) {
            state.finishEpoch = epochNow + state.remainingSeconds;
            Serial.print(stationName);
            Serial.print(" session adopted wall-clock deadline ");
            Serial.println(state.finishEpoch);
            persistStation(station, state);
        }

        if (epochNow >= state.finishEpoch) {
            completeStationSession(station, state, stationName);
            return;
        }

        state.remainingSeconds = state.finishEpoch - epochNow;
        state.lastSessionTickMs = now;

        if ((now - state.lastPersistMs) >= SESSION_PERSIST_INTERVAL_MS) {
            persistStation(station, state);
        }
        return;
    }

    // Fallback while SNTP is unavailable. Once wall-clock time becomes valid,
    // the session automatically switches to the absolute deadline path above.
    unsigned long elapsedMs = now - state.lastSessionTickMs;
    if (elapsedMs < 1000UL) {
        return;
    }

    uint32_t elapsedSeconds = elapsedMs / 1000UL;
    state.lastSessionTickMs += elapsedSeconds * 1000UL;

    if (elapsedSeconds >= state.remainingSeconds) {
        completeStationSession(station, state, stationName);
        return;
    }

    state.remainingSeconds -= elapsedSeconds;

    if ((now - state.lastPersistMs) >= SESSION_PERSIST_INTERVAL_MS) {
        persistStation(station, state);
    }
}

DryerStationView makeRuntimeStationView(const DryerStationState& state) {
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

    status.temperaturePlan.state = temperaturePlan.state;
    status.temperaturePlan.status =
        DryerTemperaturePlanner::stateText(temperaturePlan.state);
    status.temperaturePlan.spoolCount = temperaturePlan.spoolCount;
    status.temperaturePlan.commonMinC = temperaturePlan.commonMinC;
    status.temperaturePlan.commonMaxC = temperaturePlan.commonMaxC;
    status.temperaturePlan.recommendedTargetC =
        temperaturePlan.recommendedTargetC;
    status.temperaturePlan.compromiseGapC = temperaturePlan.compromiseGapC;
    status.temperaturePlan.automaticPlanUsable =
        temperaturePlan.automaticPlanUsable;

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
    DryerStation stationId = top ? DryerStation::TOP : DryerStation::BOTTOM;
    DryerStationState& station = top ? topStation : bottomStation;
    station = DryerStationState{};
    sessionStore.clear(stationId);

    // Allow the same tag to be rescanned immediately after a manual clear.
    lastSeenUid = "";
    lastTagSeenMs = 0;

    Serial.print("Cleared station: ");
    Serial.println(top ? "TOP" : "BOTTOM");
    recalculateTemperaturePlan();
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
        if (
            spool.dryTempMinC > 0 &&
            spool.dryTempMaxC > 0 &&
            spool.dryTempMinC != spool.dryTempMaxC
        ) {
            Serial.print(spool.dryTempMinC);
            Serial.print("-");
            Serial.print(spool.dryTempMaxC);
            Serial.print(" C; preferred ");
            Serial.print(spool.dryTempC);
            Serial.println(" C");
        } else {
            Serial.print(spool.dryTempC);
            Serial.println(" C");
        }
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
    DryerStationState& station = getSelectedStation();
    const DryerStation stationId = selectedStation;
    const char* stationName =
        selectedStation == DryerStation::TOP ? "TOP" : "BOTTOM";

    // A new scan replaces the prior station assignment and session.
    station = DryerStationState{};
    station.occupied = true;
    station.uid = uid;
    persistStation(stationId, station);

    Serial.print("Assigned UID ");
    Serial.print(uid);
    Serial.print(" to ");
    Serial.println(stationName);

    drawDisplay();

    if (!wifiConnected) {
        station.lookupError = "WiFi offline";
        Serial.println("Spoolman lookup skipped: WiFi offline.");
        persistStation(stationId, station);
        recalculateTemperaturePlan();
        drawDisplay();
        return;
    }

    if (!spoolman.isConfigured()) {
        station.lookupError = "Spoolman not configured";
        Serial.println("Spoolman lookup skipped: URL not configured.");
        persistStation(stationId, station);
        recalculateTemperaturePlan();
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
        configureStationSession(stationId, station, stationName);
    } else {
        station.lookupError = error;
        Serial.print("Spoolman lookup failed: ");
        Serial.println(error);
        persistStation(stationId, station);
    }

    recalculateTemperaturePlan();
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

// ---------- Button ----------

void handleButtons() {
    unsigned long now = millis();
    bool reading = digitalRead(PIN_BUTTON_SELECT);

    if (reading != lastButtonReading) {
        buttonDebounceTime = now;
        lastButtonReading = reading;
    }

    if ((now - buttonDebounceTime) < BUTTON_DEBOUNCE_MS) {
        return;
    }

    if (reading == stableButtonState) {
        return;
    }

    stableButtonState = reading;

    if (stableButtonState != LOW) {
        return;
    }

    // Current prototype has two stations. This becomes index = (index + 1) %
    // stationCount when the variable-station refactor lands; the hardware stays
    // one button regardless of how many shelves/stations are compiled in.
    selectedStation = selectedStation == DryerStation::BOTTOM
        ? DryerStation::TOP
        : DryerStation::BOTTOM;

    Serial.print("Selected station: ");
    Serial.println(selectedStation == DryerStation::TOP ? "TOP" : "BOTTOM");
    drawDisplay();
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

    if (sessionStore.begin()) {
        restoreSessions();
        recalculateTemperaturePlan();
    }

    pinMode(PIN_BUTTON_SELECT, INPUT_PULLUP);

    temperatureSensor.begin();
    initDisplay();

    Serial.print("DS18B20 devices found: ");
    Serial.println(temperatureSensor.getDeviceCount());

    initNfc();
    drawDisplay();

    if (!connectWiFi()) {
        startSetupPortal();
    } else {
        timeService.begin();
        timeService.waitForSync(5000);
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

    updateStationSession(DryerStation::TOP, topStation, "TOP", now);
    updateStationSession(DryerStation::BOTTOM, bottomStation, "BOTTOM", now);

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
