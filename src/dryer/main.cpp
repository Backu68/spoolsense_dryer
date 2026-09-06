#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

constexpr uint8_t PIN_TEMP = 27;

constexpr uint8_t OLED_SDA = 21;
constexpr uint8_t OLED_SCL = 22;
constexpr uint8_t OLED_ADDRESS = 0x3C;

constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;

OneWire oneWire(PIN_TEMP);
DallasTemperature temperatureSensor(&oneWire);

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("SpoolSense Dryer");
    Serial.println("Firmware " FIRMWARE_VERSION);

    temperatureSensor.begin();

    Wire.begin(OLED_SDA, OLED_SCL);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println("SSD1306 initialization failed.");
    } else {
        Serial.println("SSD1306 initialized.");

        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(1);

        display.setCursor(0, 0);
        display.println("SpoolSense Dryer");

        display.setCursor(0, 16);
        display.println("TOP:    Empty");

        display.setCursor(0, 28);
        display.println("BOTTOM: Empty");

        display.display();
    }

    Serial.print("DS18B20 devices found: ");
    Serial.println(temperatureSensor.getDeviceCount());

    Serial.println("Dryer controller booted.");
}

void loop() {
    temperatureSensor.requestTemperatures();

    float tempC = temperatureSensor.getTempCByIndex(0);

    Serial.print("Chamber temperature: ");
    Serial.print(tempC);
    Serial.println(" C");

    display.fillRect(0, 44, 128, 20, SSD1306_BLACK);

    display.setCursor(0, 48);
    display.print("Chamber: ");

    if (tempC == DEVICE_DISCONNECTED_C) {
        display.print("--.- C");
    } else {
        display.print(tempC, 1);
        display.print(" C");
    }

    display.display();

    delay(2000);
}