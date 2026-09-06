#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

constexpr uint8_t PIN_TEMP = 27;

OneWire oneWire(PIN_TEMP);
DallasTemperature temperatureSensor(&oneWire);

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("SpoolSense Dryer");
    Serial.println("Firmware " FIRMWARE_VERSION);

    temperatureSensor.begin();

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

    delay(2000);
}