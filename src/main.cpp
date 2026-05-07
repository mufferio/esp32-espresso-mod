#include <Arduino.h>
#include <Adafruit_MAX31855.h>
#include <PID_v1.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

// ── PIN MAP ──────────────────────────────
#define MAXCLK   18
#define MAXCS     5
#define MAXDO    19
#define SSR_PIN  26
#define OLED_SDA 21
#define OLED_SCL 22
// ─────────────────────────────────────────

// ── OLED ─────────────────────────────────
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
// ─────────────────────────────────────────

// ── PID SETTINGS ─────────────────────────
#define TARGET_TEMP  93.0
#define KP           25.0
#define KI           0.05
#define KD           1.0
// ─────────────────────────────────────────

unsigned long windowSize = 2000;
unsigned long windowStartTime;

Adafruit_MAX31855 thermocouple(MAXCLK, MAXCS, MAXDO);

double currentTemp, pidOutput, setpoint = TARGET_TEMP;
PID myPID(&currentTemp, &pidOutput, &setpoint, KP, KI, KD, DIRECT);

enum MachineState { HEATING, READY };
MachineState state = HEATING;

unsigned long lastTempRead = 0;
unsigned long lastSerialPrint = 0;
unsigned long lastDisplayUpdate = 0;

void updateDisplay() {
  display.clearDisplay();

  // ── current temp — big font ───────────
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(currentTemp, 1);
  display.print((char)247); // degree symbol
  display.print("C");

  // ── target temp ───────────────────────
  display.setTextSize(1);
  display.setCursor(0, 28);
  display.print("TARGET: ");
  display.print(setpoint, 1);
  display.print("C");

  // ── PID power bar ────────────────────
  display.setCursor(0, 40);
  display.print("PWR:");
  int barWidth = map(pidOutput, 0, windowSize, 0, 90);
  display.fillRect(28, 41, barWidth, 6, SSD1306_WHITE);

  // ── state ─────────────────────────────
  display.setCursor(0, 54);
  if (state == READY) {
    display.print("** READY **");
  } else {
    display.print("HEATING...");
  }

  display.display();
}

void setup() {
  Serial.begin(115200);
  pinMode(SSR_PIN, OUTPUT);
  digitalWrite(SSR_PIN, LOW);

  // OLED init
  Wire.begin(OLED_SDA, OLED_SCL);
  delay(1000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found — check wiring");
    while (true);
  }
  display.clearDisplay();
  display.display();

  // PID init
myPID.SetOutputLimits(0, windowSize);
  myPID.SetSampleTime(250);
  myPID.SetMode(AUTOMATIC);
  windowStartTime = millis();

  Serial.println("ESP32 Espresso PID starting...");
}

void loop() {
  unsigned long now = millis();

  // ── READ TEMP every 250ms ─────────────
  if (now - lastTempRead >= 250) {
    lastTempRead = now;
    double reading = thermocouple.readCelsius();

    if (isnan(reading)) {
      digitalWrite(SSR_PIN, LOW);
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("THERMOCOUPLE");
      display.println("FAULT!");
      display.println("Boiler off.");
      display.display();
      Serial.println("THERMOCOUPLE FAULT - boiler off");
      return;
    }

    currentTemp = reading;
    myPID.Compute();
  }

  // ── SSR CONTROL ───────────────────────
  if (now - windowStartTime > windowSize) {
    windowStartTime += windowSize;
  }
  bool ssrOn = (pidOutput > (now - windowStartTime));
  digitalWrite(SSR_PIN, ssrOn ? HIGH : LOW);

  // ── STATE MACHINE ─────────────────────
  state = (currentTemp >= setpoint - 1.0) ? READY : HEATING;

  // ── UPDATE DISPLAY every 200ms ────────
  if (now - lastDisplayUpdate >= 200) {
    lastDisplayUpdate = now;
    updateDisplay();
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