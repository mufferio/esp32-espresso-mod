#include <Arduino.h>
#include <Adafruit_MAX31855.h>
#include <PID_v1.h>

// ── PIN MAP ──────────────────────────────
#define MAXCLK   18
#define MAXCS     5
#define MAXDO    19
#define SSR_PIN  26
// Phase 2 (reserved, not wired yet)
// #define TRIAC_ZC    4
// #define TRIAC_DIM   2
// #define PRESSURE_ADC 34
// Phase 3 (reserved, not wired yet)
// #define SOLENOID_RELAY 25
// ─────────────────────────────────────────

// ── PID SETTINGS ─────────────────────────
#define TARGET_TEMP  93.0
#define KP           25.0
#define KI           0.05
#define KD           1.0
// ─────────────────────────────────────────

// ── PID WINDOW ───────────────────────────
// SSR cycles on/off within a 2 second window
// e.g. 70% output = SSR on for 1.4s, off for 0.6s
unsigned long windowSize = 2000;
unsigned long windowStartTime;
// ─────────────────────────────────────────

Adafruit_MAX31855 thermocouple(MAXCLK, MAXCS, MAXDO);

double currentTemp, pidOutput, setpoint = TARGET_TEMP;
PID myPID(&currentTemp, &pidOutput, &setpoint, KP, KI, KD, DIRECT);

// ── SHOT STATES ──────────────────────────
enum MachineState { IDLE, HEATING, READY };
MachineState state = HEATING;
// ─────────────────────────────────────────

unsigned long lastTempRead = 0;
unsigned long lastSerialPrint = 0;

void setup() {
  Serial.begin(115200);
  pinMode(SSR_PIN, OUTPUT);
  digitalWrite(SSR_PIN, LOW); // boiler off on startup

  // PID setup
  myPID.SetOutputLimits(0, windowSize);
  myPID.SetMode(AUTOMATIC);
  myPID.SetOutputLimits(0, windowSize);
  myPID.SetMode(AUTOMATIC);
  myPID.SetSampleTime(250); // match our read interval
  windowStartTime = millis();

  Serial.println("ESP32 Espresso PID starting...");
  Serial.print("Target: ");
  Serial.print(setpoint);
  Serial.println("C");
}

void loop() {
  unsigned long now = millis();

  // ── READ TEMPERATURE every 250ms ─────
  if (now - lastTempRead >= 250) {
    lastTempRead = now;
    double reading = thermocouple.readCelsius();

    if (isnan(reading)) {
      // Fault — kill boiler immediately, safe state
      digitalWrite(SSR_PIN, LOW);
      Serial.println("THERMOCOUPLE FAULT - boiler off");
      return;
    }

    currentTemp = reading;
    myPID.Compute();
  }

  // ── SSR CONTROL (time-proportional) ──
  // Rolls a 2 second window continuously
  // PID output determines how much of that window SSR is ON
  if (now - windowStartTime > windowSize) {
    windowStartTime += windowSize;
  }
  bool ssrOn = (pidOutput > (now - windowStartTime));
  digitalWrite(SSR_PIN, ssrOn ? HIGH : LOW);

  // ── STATE MACHINE ─────────────────────
  if (currentTemp >= setpoint - 1.0) {
    state = READY;
  } else {
    state = HEATING;
  }

  // ── SERIAL TELEMETRY every 1s ─────────
  if (now - lastSerialPrint >= 1000) {
    lastSerialPrint = now;
    Serial.print("Temp: ");
    Serial.print(currentTemp, 1);
    Serial.print("C | Target: ");
    Serial.print(setpoint, 1);
    Serial.print("C | PID out: ");
    Serial.print((pidOutput / windowSize) * 100, 0);
    Serial.print("% | State: ");
    Serial.println(state == READY ? "READY" : "HEATING");
  }
}