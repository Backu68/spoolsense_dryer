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
constexpr uint8_t PIN_BUTTON_CLEAR = 33;

constexpr uint8_t OLED_SDA = 21;
constexpr uint8_t OLED_SCL = 22;
constexpr uint8_t OLED_ADDRESS = 0x3C;

constexpr uint8_t PN532_SCK  = 18;
constexpr uint8_t PN532_MISO = 19;
constexpr uint8_t PN532_MOSI = 23;
constexpr uint8_t PN532_CS   = 25;

// ---------- Hardware backends ----------

SSD1306DryerDisplay ssd1306Display(OLED_SDA, OLED_SCL, OLED_ADDRESS);
DryerDisplayI* activeDisplay = &ssd1306Display;

PN532DryerNfc pn532Reader(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_CS);
DryerNfcI* activeNfc = &pn532Reader;
bool nfcAvailable = false;

// ---------- Temperature ----------

OneWire oneWire(PIN_TEMP);
DallasTemperature temperatureSensor(&oneWire);
float chamberTempsC[DRYER_ZONE_COUNT];

// ---------- Connectivity / persistence ----------

DryerConfig dryerConfig;
DryerSessionStore sessionStore;
DryerTimeService timeService;
SetupPortal setupPortal;
RuntimeWebUI runtimeWebUI;
SpoolmanClient spoolman;

bool wifiConnected = false;

// ---------- Dryer state ----------

DryerStationId selectedStation = DRYER_DEFAULT_STATION;
DryerStationState stations[DRYER_STATION_COUNT];
DryerTemperaturePlan temperaturePlans[DRYER_ZONE_COUNT];

// ---------- Buttons ----------

constexpr unsigned long BUTTON_DEBOUNCE_MS = 40;

struct DebouncedButton {
    uint8_t pin;
    bool lastReading = HIGH;
    bool stableState = HIGH;
    unsigned long changedAt = 0;
};

DebouncedButton selectButton{PIN_BUTTON_SELECT};
DebouncedButton clearButton{PIN_BUTTON_CLEAR};

// ---------- Timing ----------

unsigned long lastTemperatureRead = 0;

String lastSeenUid;
unsigned long lastTagSeenMs = 0;
constexpr unsigned long TAG_REPEAT_BLOCK_MS = 2000;

constexpr int SESSION_START_OFFSET_C = 5;
constexpr unsigned long SESSION_PERSIST_INTERVAL_MS = 60000UL;

// ---------- Forward declarations ----------

DryerRuntimeStatus getRuntimeStatus();
void drawDisplay();
void recalculateTemperaturePlans();
void clearRuntimeStation(DryerStationId station);

// ---------- Helpers ----------

bool chamberTemperatureValid(DryerZoneId zone) {
    if (zone >= DRYER_ZONE_COUNT) {
        return false;
    }

    float temperature = chamberTempsC[zone];
    return temperature != DEVICE_DISCONNECTED_C &&
           temperature >= -55.0f &&
           temperature <= 125.0f;
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
    return stations[selectedStation];
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

void persistStation(DryerStationId station, DryerStationState& state) {
    if (sessionStore.save(station, state)) {
        state.lastPersistMs = millis();
    }
}

uint8_t occupiedStationCount(DryerZoneId zone) {
    uint8_t count = 0;
    for (DryerStationId i = 0; i < DRYER_STATION_COUNT; ++i) {
        if (dryerZoneForStation(i) == zone && stations[i].occupied) {
            ++count;
        }
    }
    return count;
}

bool temperaturePlanBlocksNewStarts(DryerStationId station) {
    DryerZoneId zone = dryerZoneForStation(station);
    return occupiedStationCount(zone) > 1 &&
           !temperaturePlans[zone].automaticPlanUsable;
}

void updateWaitingThreshold(
    DryerStationId station,
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

void printTemperaturePlan(DryerZoneId zone) {
    const DryerTemperaturePlan& plan = temperaturePlans[zone];

    Serial.print(dryerZoneLabel(zone));
    Serial.print(" temperature plan: ");
    Serial.println(DryerTemperaturePlanner::stateText(plan.state));

    if (plan.spoolCount == 0) {
        return;
    }

    if (plan.automaticPlanUsable) {
        Serial.print("  Shared target: ");
        Serial.print(plan.recommendedTargetC);
        Serial.println(" C");
        Serial.print("  Shared range: ");
        Serial.print(plan.commonMinC);
        Serial.print("-");
        Serial.print(plan.commonMaxC);
        Serial.println(" C");
        return;
    }

    if (plan.state == DryerTemperaturePlanState::COMPROMISE_REQUIRED) {
        Serial.println("  WARNING: loaded drying ranges do not overlap.");
        Serial.print("  Highest non-overtemperature candidate: ");
        Serial.print(plan.recommendedTargetC);
        Serial.println(" C");
        Serial.print("  Range gap: ");
        Serial.print(plan.compromiseGapC);
        Serial.println(" C");
        Serial.println("  Automatic compromise remains disabled until time compensation is validated.");
    } else if (plan.state == DryerTemperaturePlanState::INVALID_PROFILE) {
        Serial.println("  WARNING: a loaded spool is missing a usable drying profile.");
    }
}

void recalculateTemperaturePlans() {
    for (DryerZoneId zone = 0; zone < DRYER_ZONE_COUNT; ++zone) {
        const DryerSpoolInfo* profiles[DRYER_STATION_COUNT] = {};
        size_t profileCount = 0;
        bool unresolvedLoadedSpool = false;

        for (DryerStationId station = 0; station < DRYER_STATION_COUNT; ++station) {
            if (dryerZoneForStation(station) != zone || !stations[station].occupied) {
                continue;
            }

            if (!stations[station].spool.found) {
                unresolvedLoadedSpool = true;
            }

            profiles[profileCount++] = &stations[station].spool;
        }

        temperaturePlans[zone] =
            DryerTemperaturePlanner::calculate(profiles, profileCount);

        if (unresolvedLoadedSpool) {
            temperaturePlans[zone].state = DryerTemperaturePlanState::INVALID_PROFILE;
            temperaturePlans[zone].automaticPlanUsable = false;
        }

        printTemperaturePlan(zone);

        const DryerTemperaturePlan& plan = temperaturePlans[zone];
        if (!plan.automaticPlanUsable || plan.recommendedTargetC <= 0) {
            continue;
        }

        int sharedThresholdC = plan.recommendedTargetC - SESSION_START_OFFSET_C;
        if (sharedThresholdC < 0) {
            sharedThresholdC = 0;
        }

        for (DryerStationId station = 0; station < DRYER_STATION_COUNT; ++station) {
            if (dryerZoneForStation(station) == zone) {
                updateWaitingThreshold(station, stations[station], sharedThresholdC);
            }
        }
    }
}

void restoreSessions() {
    for (DryerStationId station = 0; station < DRYER_STATION_COUNT; ++station) {
        if (!sessionStore.load(station, stations[station])) {
            continue;
        }

        Serial.print("Restored ");
        Serial.print(dryerStationLabel(station));
        Serial.print(" session: ");
        Serial.print(sessionStateText(stations[station].sessionState));
        Serial.print(", remaining ");
        Serial.print(stations[station].remainingSeconds);
        Serial.print(" s, finish epoch ");
        Serial.println(stations[station].finishEpoch);
    }
}

void completeStationSession(
    DryerStationId station,
    DryerStationState& state
) {
    state.remainingSeconds = 0;
    state.finishEpoch = 0;
    state.sessionState = DryingSessionState::COMPLETE;

    Serial.print(dryerStationLabel(station));
    Serial.println(" drying complete.");
    persistStation(station, state);
    drawDisplay();
}

void configureStationSession(
    DryerStationId station,
    DryerStationState& state
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
        Serial.print(dryerStationLabel(station));
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

    Serial.print(dryerStationLabel(station));
    Serial.print(" session waiting for ");
    Serial.print(state.startThresholdC);
    Serial.print(" C; duration ");
    Serial.print(state.spool.dryTimeHours);
    Serial.println(" h.");

    persistStation(station, state);
}

void updateStationSession(
    DryerStationId station,
    DryerStationState& state,
    unsigned long now
) {
    DryerZoneId zone = dryerZoneForStation(station);

    if (state.sessionState == DryingSessionState::WAITING_FOR_TEMP) {
        if (temperaturePlanBlocksNewStarts(station)) {
            return;
        }

        if (
            chamberTemperatureValid(zone) &&
            chamberTempsC[zone] >= static_cast<float>(state.startThresholdC)
        ) {
            state.sessionState = DryingSessionState::DRYING;
            state.lastSessionTickMs = now;

            if (timeService.isSynced()) {
                state.finishEpoch =
                    timeService.nowEpoch() + state.remainingSeconds;
            } else {
                state.finishEpoch = 0;
            }

            Serial.print(dryerStationLabel(station));
            Serial.print(" drying started at chamber temperature ");
            Serial.print(chamberTempsC[zone], 2);
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
        completeStationSession(station, state);
        return;
    }

    if (timeService.isSynced()) {
        uint32_t epochNow = timeService.nowEpoch();

        if (state.finishEpoch == 0) {
            state.finishEpoch = epochNow + state.remainingSeconds;
            Serial.print(dryerStationLabel(station));
            Serial.print(" session adopted wall-clock deadline ");
            Serial.println(state.finishEpoch);
            persistStation(station, state);
        }

        if (epochNow >= state.finishEpoch) {
            completeStationSession(station, state);
            return;
        }

        state.remainingSeconds = state.finishEpoch - epochNow;
        state.lastSessionTickMs = now;

        if ((now - state.lastPersistMs) >= SESSION_PERSIST_INTERVAL_MS) {
            persistStation(station, state);
        }
        return;
    }

    unsigned long elapsedMs = now - state.lastSessionTickMs;
    if (elapsedMs < 1000UL) {
        return;
    }

    uint32_t elapsedSeconds = elapsedMs / 1000UL;
    state.lastSessionTickMs += elapsedSeconds * 1000UL;

    if (elapsedSeconds >= state.remainingSeconds) {
        completeStationSession(station, state);
        return;
    }

    state.remainingSeconds -= elapsedSeconds;

    if ((now - state.lastPersistMs) >= SESSION_PERSIST_INTERVAL_MS) {
        persistStation(station, state);
    }
}

DryerStationView makeRuntimeStationView(
    DryerStationId station,
    const DryerStationState& state
) {
    DryerStationView view;
    view.id = station;
    view.zone = dryerZoneForStation(station);
    view.label = dryerStationLabel(station);
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
    status.wifiConnected = wifiConnected;
    if (wifiConnected) {
        status.ipAddress = WiFi.localIP().toString();
    }
    status.spoolmanConfigured = dryerConfig.hasSpoolman();
    status.nfcAvailable = nfcAvailable;
    status.setupPortalActive = setupPortal.isActive();
    status.selectedStation = selectedStation;

    for (DryerZoneId zone = 0; zone < DRYER_ZONE_COUNT; ++zone) {
        DryerZoneView& view = status.zones[zone];
        const DryerTemperaturePlan& plan = temperaturePlans[zone];
        view.id = zone;
        view.label = dryerZoneLabel(zone);
        view.chamberTempC = chamberTempsC[zone];
        view.temperatureValid = chamberTemperatureValid(zone);
        view.temperaturePlan.state = plan.state;
        view.temperaturePlan.status = DryerTemperaturePlanner::stateText(plan.state);
        view.temperaturePlan.spoolCount = plan.spoolCount;
        view.temperaturePlan.commonMinC = plan.commonMinC;
        view.temperaturePlan.commonMaxC = plan.commonMaxC;
        view.temperaturePlan.recommendedTargetC = plan.recommendedTargetC;
        view.temperaturePlan.compromiseGapC = plan.compromiseGapC;
        view.temperaturePlan.automaticPlanUsable = plan.automaticPlanUsable;
    }

    for (DryerStationId station = 0; station < DRYER_STATION_COUNT; ++station) {
        status.stations[station] = makeRuntimeStationView(station, stations[station]);
    }

    return status;
}

void drawDisplay() {
    if (activeDisplay != nullptr && activeDisplay->isAvailable()) {
        activeDisplay->render(getRuntimeStatus());
    }
}

void clearRuntimeStation(DryerStationId station) {
    if (station >= DRYER_STATION_COUNT) {
        return;
    }

    stations[station] = DryerStationState{};
    sessionStore.clear(station);

    lastSeenUid = "";
    lastTagSeenMs = 0;

    Serial.print("Cleared station: ");
    Serial.println(dryerStationLabel(station));

    recalculateTemperaturePlans();
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
    DryerStationId stationId = selectedStation;
    DryerStationState& station = getSelectedStation();

    station = DryerStationState{};
    station.occupied = true;
    station.uid = uid;
    persistStation(stationId, station);

    Serial.print("Assigned UID ");
    Serial.print(uid);
    Serial.print(" to ");
    Serial.println(dryerStationLabel(stationId));

    drawDisplay();

    if (!wifiConnected) {
        station.lookupError = "WiFi offline";
        Serial.println("Spoolman lookup skipped: WiFi offline.");
        persistStation(stationId, station);
        recalculateTemperaturePlans();
        drawDisplay();
        return;
    }

    if (!spoolman.isConfigured()) {
        station.lookupError = "Spoolman not configured";
        Serial.println("Spoolman lookup skipped: URL not configured.");
        persistStation(stationId, station);
        recalculateTemperaturePlans();
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
        configureStationSession(stationId, station);
    } else {
        station.lookupError = error;
        Serial.print("Spoolman lookup failed: ");
        Serial.println(error);
        persistStation(stationId, station);
    }

    recalculateTemperaturePlans();
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

bool buttonPressed(DebouncedButton& button, unsigned long now) {
    bool reading = digitalRead(button.pin);

    if (reading != button.lastReading) {
        button.changedAt = now;
        button.lastReading = reading;
    }

    if ((now - button.changedAt) < BUTTON_DEBOUNCE_MS) {
        return false;
    }

    if (reading == button.stableState) {
        return false;
    }

    button.stableState = reading;
    return button.stableState == LOW;
}

void handleButtons() {
    unsigned long now = millis();

    if (buttonPressed(selectButton, now)) {
        selectedStation = static_cast<DryerStationId>(
            (selectedStation + 1U) % DRYER_STATION_COUNT
        );

        Serial.print("Selected station: ");
        Serial.println(dryerStationLabel(selectedStation));
        drawDisplay();
    }

    if (buttonPressed(clearButton, now)) {
        clearRuntimeStation(selectedStation);
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

    for (DryerZoneId zone = 0; zone < DRYER_ZONE_COUNT; ++zone) {
        chamberTempsC[zone] = DEVICE_DISCONNECTED_C;
    }

    Serial.println();
    Serial.println("SpoolSense Dryer");
    Serial.println("Firmware " FIRMWARE_VERSION);
    Serial.print("Build shape: ");
    Serial.print(DRYER_STATION_COUNT);
    Serial.print(" station(s), ");
    Serial.print(DRYER_ZONE_COUNT);
    Serial.println(" thermal zone(s)");

    dryerConfig.begin();
    configureSpoolman();

    if (sessionStore.begin()) {
        restoreSessions();
        recalculateTemperaturePlans();
    }

    pinMode(PIN_BUTTON_SELECT, INPUT_PULLUP);
    pinMode(PIN_BUTTON_CLEAR, INPUT_PULLUP);

    temperatureSensor.begin();
    initDisplay();

    uint8_t sensorCount = temperatureSensor.getDeviceCount();
    Serial.print("DS18B20 devices found: ");
    Serial.println(sensorCount);
    if (sensorCount < DRYER_ZONE_COUNT) {
        Serial.println("WARNING: fewer temperature probes than configured thermal zones.");
    }

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

    for (DryerStationId station = 0; station < DRYER_STATION_COUNT; ++station) {
        updateStationSession(station, stations[station], now);
    }

    if ((now - lastTemperatureRead) >= 2000) {
        lastTemperatureRead = now;

        temperatureSensor.requestTemperatures();
        for (DryerZoneId zone = 0; zone < DRYER_ZONE_COUNT; ++zone) {
            chamberTempsC[zone] = temperatureSensor.getTempCByIndex(zone);

            if (DRYER_ZONE_COUNT == 1) {
                Serial.print("Chamber temperature: ");
            } else {
                Serial.print("Zone ");
                Serial.print(zone + 1);
                Serial.print(" temperature: ");
            }
            Serial.print(chamberTempsC[zone]);
            Serial.println(" C");
        }

        if (!setupPortal.isActive()) {
            wifiConnected = WiFi.status() == WL_CONNECTED;
        }

        drawDisplay();
    }

    delay(5);
}
