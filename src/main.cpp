#include <Arduino.h>
// ── PIN MAP ──────────────────────────────
#define MAXCLK   18
#define MAXCS     5
#define MAXDO    19
#define SSR_PIN  26
#define OLED_SDA 21
#define OLED_SCL 22
// Phase 2 (not wired yet, reserved)
// #define TRIAC_ZC    4
// #define TRIAC_DIM   2
// #define PRESSURE_ADC 34
// Phase 3 (not wired yet, reserved)
// #define SOLENOID_RELAY 25
// ─────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Coffee Machine - Ready!");
}

void loop() {
  Serial.println("ESP32 alive - " + String(millis()) + "ms");
  delay(1000);
}
