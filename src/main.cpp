#include <Arduino.h>
#include <Adafruit_MAX31855.h>

#define MAXCLK  18
#define MAXCS    5
#define MAXDO   19

Adafruit_MAX31855 thermocouple(MAXCLK, MAXCS, MAXDO);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("MAX31855 test starting...");
}

void loop() {
  double temp = thermocouple.readCelsius();


  if (isnan(temp)) {
    Serial.println("Thermocouple fault!");
    uint8_t e = thermocouple.readError();
    if (e & MAX31855_FAULT_OPEN)      Serial.println("FAULT: Open circuit - check thermocouple connections");
    if (e & MAX31855_FAULT_SHORT_GND) Serial.println("FAULT: Short to GND");
    if (e & MAX31855_FAULT_SHORT_VCC) Serial.println("FAULT: Short to VCC");
  } else {
    Serial.print("Temp: ");
    Serial.print(temp);
    Serial.println(" C");
  }

  delay(1000);
}