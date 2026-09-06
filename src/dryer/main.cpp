#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

constexpr uint8_t PIN_TEMP = 27;

constexpr uint8_t PIN_BUTTON_TOP = 32;
constexpr uint8_t PIN_BUTTON_BOTTOM = 33;

constexpr uint8_t OLED_SDA = 21;
constexpr uint8_t OLED_SCL = 22;
constexpr uint8_t OLED_ADDRESS = 0x3C;

constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;

constexpr unsigned long BUTTON_DEBOUNCE_MS = 40;

enum class DryerStation {
    TOP,
    BOTTOM
};

DryerStation selectedStation = DryerStation::TOP;

OneWire oneWire(PIN_TEMP);
DallasTemperature temperatureSensor(&oneWire);

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

bool lastTopReading = HIGH;
bool lastBottomReading = HIGH;

bool stableTopState = HIGH;
bool stableBottomState = HIGH;

unsigned long topDebounceTime = 0;
unsigned long bottomDebounceTime = 0;

void drawDisplay(float tempC) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.println("SpoolSense Dryer");

    display.setCursor(0, 16);
    display.print(selectedStation == DryerStation::TOP ? ">" : " ");
    display.println("TOP:    Empty");

    display.setCursor(0, 28);
    display.print(selectedStation == DryerStation::BOTTOM ? ">" : " ");
    display.println("BOTTOM: Empty");

    display.setCursor(0, 48);
    display.print("Chamber: ");

    if (tempC == DEVICE_DISCONNECTED_C) {
        display.print("--.- C");
    } else {
        display.print(tempC, 1);
        display.print(" C");
    }

    display.display();
}

void handleButtons() {
    unsigned long now = millis();

    bool topReading = digitalRead(PIN_BUTTON_TOP);
    bool bottomReading = digitalRead(PIN_BUTTON_BOTTOM);

    if (topReading != lastTopReading) {
        topDebounceTime = now;
        lastTopReading = topReading;
    }

    if (now - topDebounceTime >= BUTTON_DEBOUNCE_MS) {
        if (topReading != stableTopState) {
            stableTopState = topReading;

            if (stableTopState == LOW) {
                selectedStation = DryerStation::TOP;
                Serial.println("Selected station: TOP");
            }
        }
    }

    if (bottomReading != lastBottomReading) {
        bottomDebounceTime = now;
        lastBottomReading = bottomReading;
    }

    if (now - bottomDebounceTime >= BUTTON_DEBOUNCE_MS) {
        if (bottomReading != stableBottomState) {
            stableBottomState = bottomReading;

            if (stableBottomState == LOW) {
                selectedStation = DryerStation::BOTTOM;
                Serial.println("Selected station: BOTTOM");
            }
        }
    }
}

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

    Serial.println("Dryer controller booted.");

    drawDisplay(DEVICE_DISCONNECTED_C);
}

void loop() {
    handleButtons();

    static unsigned long lastTemperatureRead = 0;

    if (millis() - lastTemperatureRead >= 2000) {
        lastTemperatureRead = millis();

        temperatureSensor.requestTemperatures();
        float tempC = temperatureSensor.getTempCByIndex(0);

        Serial.print("Chamber temperature: ");
        Serial.print(tempC);
        Serial.println(" C");

        drawDisplay(tempC);
    }

    delay(5);
}