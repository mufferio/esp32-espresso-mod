#include <Arduino.h>
#include <Adafruit_MAX31855.h>
#include <PID_v1.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include "secrets.h"

// ── PIN MAP ──────────────────────────────
#define MAXCLK   18
#define MAXCS     5
#define MAXDO    19
#define SSR_PIN  26
#define OLED_SDA 21
#define OLED_SCL 22
// ─────────────────────────────────────────

// ── OLED ─────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
// ─────────────────────────────────────────

// ── TEMPERATURE TARGETS ──────────────────
#define ESPRESSO_TEMP  97.0
#define STEAM_TEMP    140.0
// ─────────────────────────────────────────

// ── PID ──────────────────────────────────
#define KP  20.0
#define KI   0.02
#define KD   8.0
#define STEAM_KP  35.0
#define STEAM_KI   0.05
#define STEAM_KD   3.0
unsigned long windowSize = 1000;
unsigned long windowStartTime;
double currentTemp, pidOutput, setpoint = ESPRESSO_TEMP;
double espressoSetpoint = ESPRESSO_TEMP;
double steamSetpoint    = STEAM_TEMP;
PID myPID(&currentTemp, &pidOutput, &setpoint, KP, KI, KD, DIRECT);
// ─────────────────────────────────────────

// ── MACHINE STATE ─────────────────────────
enum MachineMode { MODE_ESPRESSO, MODE_STEAM };
MachineMode currentMode    = MODE_ESPRESSO;
MachineMode previousMode   = MODE_ESPRESSO;
bool milkModeEnabled       = false;
bool steamActivated        = false;

// Shot logging
struct ShotLog {
  float avgTemp;
  float minTemp;
  float maxTemp;
  unsigned long duration;
  bool  valid;
};
ShotLog lastShot = {0, 0, 0, 0, false};

// Temp accumulator for shot logging
float shotTempSum   = 0;
float shotTempMin   = 999;
float shotTempMax   = 0;
int   shotTempCount = 0;
unsigned long shotStartTime = 0;
bool  trackingShot  = false;
// ─────────────────────────────────────────

Adafruit_MAX31855 thermocouple(MAXCLK, MAXCS, MAXDO);
AsyncWebServer server(80);

unsigned long lastTempRead      = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastSerialPrint   = 0;

// ── OLED STARTUP ANIMATION ───────────────

static void drawFaceStructure() {
  // Head outline with Frankenstein flat top
  display.drawRect(16, 4, 96, 57, SSD1306_WHITE);
  display.fillRect(17, 5, 94, 6, SSD1306_WHITE);    // flat-top fill

  // Left neck bolt — cap + body
  display.fillRect(5, 41, 8, 5,  SSD1306_WHITE);    // cap (wider)
  display.fillRect(2, 44, 14, 14, SSD1306_WHITE);   // body

  // Right neck bolt
  display.fillRect(115, 41, 8,  5,  SSD1306_WHITE);
  display.fillRect(112, 44, 14, 14, SSD1306_WHITE);
}

static void drawBrows() {
  // Heavy furrowed brows — 3 px thick, inside corners lower (menacing)
  for (int t = 0; t < 3; t++) {
    display.drawLine(28, 20 + t, 51, 16 + t, SSD1306_WHITE);  // left
    display.drawLine(77, 16 + t, 100, 20 + t, SSD1306_WHITE); // right (mirror)
  }
}

// Eyes: 24 wide × 16 tall rectangular sockets, fill rises from bottom
void drawEye(int cx, int cy, int openHeight, bool showPupil) {
  const int W = 24, H = 16;
  int x0 = cx - W / 2;
  int y0 = cy - H / 2;

  display.drawRect(x0, y0, W, H, SSD1306_WHITE);

  if (openHeight > 0) {
    int fh = min(openHeight, H - 2);
    display.fillRect(x0 + 1, y0 + H - 1 - fh, W - 2, fh, SSD1306_WHITE);
  }

  if (showPupil) {
    display.fillRect(cx - 4, cy - 3, 8, 7, SSD1306_BLACK); // pupil
    display.fillRect(cx - 3, cy - 2, 2, 2, SSD1306_WHITE); // glint
  }
}

static void drawStitchedMouth() {
  display.drawLine(26, 50, 102, 50, SSD1306_WHITE);
  for (int sx = 33; sx <= 97; sx += 8) {
    display.drawLine(sx, 47, sx, 53, SSD1306_WHITE);
  }
}

void frankStartupAnimation() {
  const int LX = 42, RX = 86, EY = 33, EH = 16;

  // Phase 1 — head emerges out of the dark
  display.clearDisplay();
  drawFaceStructure();
  display.display();
  delay(500);

  // Phase 2 — brows descend into place
  drawBrows();
  display.display();
  delay(400);

  // Phase 3 — eyes grind open, slow and heavy
  for (int h = 0; h <= EH; h += 2) {
    display.clearDisplay();
    drawFaceStructure();
    drawBrows();
    drawEye(LX, EY, h, false);
    drawEye(RX, EY, h, false);
    display.display();
    delay(80);
  }

  // Phase 4 — pupils lock in
  delay(250);
  display.clearDisplay();
  drawFaceStructure();
  drawBrows();
  drawEye(LX, EY, EH, true);
  drawEye(RX, EY, EH, true);
  display.display();
  delay(500);

  // Phase 5 — single heavy blink
  for (int h = EH; h >= 0; h -= 4) {
    display.clearDisplay();
    drawFaceStructure();
    drawBrows();
    drawEye(LX, EY, h, false);
    drawEye(RX, EY, h, false);
    display.display();
    delay(25);
  }
  delay(100);
  for (int h = 0; h <= EH; h += 4) {
    display.clearDisplay();
    drawFaceStructure();
    drawBrows();
    drawEye(LX, EY, h, h >= EH);
    drawEye(RX, EY, h, h >= EH);
    display.display();
    delay(25);
  }

  // Phase 6 — stitched mouth drags into view
  delay(300);
  display.clearDisplay();
  drawFaceStructure();
  drawBrows();
  drawEye(LX, EY, EH, true);
  drawEye(RX, EY, EH, true);
  drawStitchedMouth();
  display.display();
  delay(700);

  // Phase 7 — loading bar
  // Yellow zone (y 0-15)  : "FRANK IS AWAKENING" text
  // Blue zone  (y 16-63)  : face inner elements + progress bar
  for (int progress = 0; progress <= 100; progress += 2) {
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(5, 4);
    display.print("FRANK IS AWAKENING");

    drawBrows();
    drawEye(LX, EY, EH, true);
    drawEye(RX, EY, EH, true);
    drawStitchedMouth();

    display.drawRect(18, 55, 92, 8, SSD1306_WHITE);
    int barFill = map(progress, 0, 100, 0, 88);
    display.fillRect(20, 57, barFill, 4, SSD1306_WHITE);

    display.display();
    delay(30);
  }
  delay(500);
}
// ─────────────────────────────────────────

// ── OLED MAIN DISPLAY ────────────────────
void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ── YELLOW ZONE (y 0-15): Mode left, State right ─────────
  display.setTextSize(1);

  display.setCursor(0, 4);
  if (currentMode == MODE_STEAM) {
    display.print("~ STEAM ~");
  } else {
    display.print("ESPRESSO");
  }

  bool atTarget = abs(currentTemp - setpoint) < 1.5;
  const char* stateStr = atTarget ? "READY" : "HEAT";
  display.setCursor(128 - (int)(strlen(stateStr) * 6), 4);
  display.print(stateStr);

  if (milkModeEnabled) {
    display.setCursor(61, 4);
    display.print("M");
  }

  display.drawLine(0, 15, 127, 15, SSD1306_WHITE); // zone separator

  // ── BLUE ZONE (y 16-63): Temp, target, segmented bar, status ─

  // Large current temperature
  display.setTextSize(2);
  display.setCursor(0, 18);
  display.print(currentTemp, 1);
  display.print((char)247);
  display.print("C");

  // Target temperature
  display.setTextSize(1);
  display.setCursor(0, 37);
  display.print("TGT:");
  display.print(setpoint, 1);
  display.print((char)247);
  display.print("C");

  // Segmented power bar — 10 blocks of 9x7 px, 2 px gap, centred
  int filledSegs = (int)((pidOutput / (double)windowSize) * 10.0);
  filledSegs = constrain(filledSegs, 0, 10);
  for (int i = 0; i < 10; i++) {
    int sx = 10 + i * 11; // 9 px wide + 2 px gap
    if (i < filledSegs) {
      display.fillRect(sx, 46, 9, 7, SSD1306_WHITE);
    } else {
      display.drawRect(sx, 46, 9, 7, SSD1306_WHITE);
    }
  }

  // Status line
  display.setTextSize(1);
  display.setCursor(0, 56);
  if (currentMode == MODE_STEAM) {
    display.print(atTarget ? "STEAM READY" : "STEAMING...");
  } else {
    display.print(atTarget ? "** READY **" : "HEATING...");
  }

  display.display();
}
// ─────────────────────────────────────────

// ── WEB UI HTML ──────────────────────────
const char FRANK_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Frank</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Special+Elite&family=Share+Tech+Mono&display=swap');

  :root {
    --green:      #00ff41;
    --green-dim:  #00aa2a;
    --green-dark: #003a0f;
    --bg:         #030a04;
    --bg2:        #071209;
    --bg3:        #0c1e0e;
    --bolt:       #7fff00;
    --red:        #ff3a1a;
    --orange:     #ff8c00;
    --border:     #1a4d1f;
    --text-dim:   #4a7a4e;
  }

  * { margin: 0; padding: 0; box-sizing: border-box; }

  body {
    background: var(--bg);
    color: var(--green);
    font-family: 'Share Tech Mono', monospace;
    min-height: 100vh;
    padding: 20px;
    background-image:
      repeating-linear-gradient(
        0deg,
        transparent,
        transparent 2px,
        rgba(0,255,65,0.015) 2px,
        rgba(0,255,65,0.015) 4px
      );
  }

  /* scanline overlay */
  body::before {
    content: '';
    position: fixed;
    inset: 0;
    background: repeating-linear-gradient(
      0deg,
      rgba(0,0,0,0.15) 0px,
      rgba(0,0,0,0.15) 1px,
      transparent 1px,
      transparent 2px
    );
    pointer-events: none;
    z-index: 999;
  }

  header {
    text-align: center;
    margin-bottom: 28px;
    padding-bottom: 16px;
    border-bottom: 1px solid var(--border);
  }

  .frank-title {
    font-family: 'Special Elite', cursive;
    font-size: 42px;
    color: var(--green);
    text-shadow:
      0 0 10px var(--green),
      0 0 30px var(--green-dim),
      0 0 60px rgba(0,255,65,0.3);
    letter-spacing: 0.15em;
  }

  .frank-sub {
    font-size: 11px;
    color: var(--text-dim);
    letter-spacing: 0.3em;
    margin-top: 4px;
  }

  .bolts {
    font-size: 20px;
    color: var(--bolt);
    margin: 0 8px;
    text-shadow: 0 0 8px var(--bolt);
  }

  .grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 16px;
    max-width: 700px;
    margin: 0 auto;
  }

  @media (max-width: 600px) {
    .grid { grid-template-columns: 1fr; }
  }

  .panel {
    background: var(--bg2);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 20px;
    position: relative;
    overflow: hidden;
  }

  .panel::before {
    content: '';
    position: absolute;
    top: 0; left: 0; right: 0;
    height: 2px;
    background: var(--green);
    box-shadow: 0 0 8px var(--green);
  }

  .panel.steam-panel::before {
    background: var(--orange);
    box-shadow: 0 0 8px var(--orange);
  }

  .panel-title {
    font-size: 10px;
    letter-spacing: 0.25em;
    color: var(--text-dim);
    margin-bottom: 16px;
  }

  .temp-display {
    font-size: 48px;
    font-weight: bold;
    color: var(--green);
    text-shadow: 0 0 20px rgba(0,255,65,0.5);
    line-height: 1;
    margin-bottom: 4px;
  }

  .temp-display.steam-temp {
    color: var(--orange);
    text-shadow: 0 0 20px rgba(255,140,0,0.5);
  }

  .temp-unit {
    font-size: 18px;
    color: var(--text-dim);
  }

  .target-row {
    display: flex;
    align-items: center;
    gap: 8px;
    margin: 12px 0;
  }

  .target-label {
    font-size: 11px;
    color: var(--text-dim);
    flex: 1;
  }

  .target-val {
    font-size: 16px;
    color: var(--green);
    min-width: 50px;
    text-align: center;
  }

  .adj-btn {
    background: var(--bg3);
    border: 1px solid var(--border);
    color: var(--green);
    width: 28px;
    height: 28px;
    border-radius: 3px;
    cursor: pointer;
    font-size: 16px;
    display: flex;
    align-items: center;
    justify-content: center;
    transition: all 0.1s;
    font-family: 'Share Tech Mono', monospace;
  }

  .adj-btn:hover {
    background: var(--green-dark);
    border-color: var(--green);
    box-shadow: 0 0 8px rgba(0,255,65,0.3);
  }

  .adj-btn:active { transform: scale(0.95); }

  .pwr-bar-wrap {
    margin: 12px 0 8px;
  }

  .pwr-label {
    font-size: 10px;
    color: var(--text-dim);
    margin-bottom: 4px;
    letter-spacing: 0.1em;
  }

  .pwr-bar-bg {
    background: var(--bg3);
    border: 1px solid var(--border);
    height: 8px;
    border-radius: 2px;
    overflow: hidden;
  }

  .pwr-bar-fill {
    height: 100%;
    background: var(--green);
    box-shadow: 0 0 6px var(--green);
    transition: width 0.3s ease;
    border-radius: 2px;
  }

  .pwr-bar-fill.steam-fill {
    background: var(--orange);
    box-shadow: 0 0 6px var(--orange);
  }

  .state-badge {
    display: inline-block;
    padding: 3px 10px;
    border-radius: 2px;
    font-size: 10px;
    letter-spacing: 0.2em;
    margin-top: 8px;
  }

  .state-ready {
    background: rgba(0,255,65,0.1);
    border: 1px solid var(--green);
    color: var(--green);
    text-shadow: 0 0 6px var(--green);
  }

  .state-heating {
    background: rgba(255,60,26,0.1);
    border: 1px solid var(--red);
    color: var(--red);
    animation: pulse 1s ease-in-out infinite;
  }

  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.5; }
  }

  .big-btn {
    width: 100%;
    padding: 12px;
    border-radius: 3px;
    font-family: 'Share Tech Mono', monospace;
    font-size: 12px;
    letter-spacing: 0.2em;
    cursor: pointer;
    transition: all 0.15s;
    margin-top: 10px;
  }

  .steam-btn {
    background: rgba(255,140,0,0.1);
    border: 1px solid var(--orange);
    color: var(--orange);
  }

  .steam-btn:hover {
    background: rgba(255,140,0,0.2);
    box-shadow: 0 0 12px rgba(255,140,0,0.3);
  }

  .steam-btn.active {
    background: rgba(255,140,0,0.25);
    box-shadow: 0 0 16px rgba(255,140,0,0.5);
  }

  .milk-btn {
    background: rgba(0,255,65,0.05);
    border: 1px solid var(--border);
    color: var(--text-dim);
  }

  .milk-btn:hover {
    border-color: var(--green-dim);
    color: var(--green);
  }

  .milk-btn.active {
    background: rgba(0,255,65,0.1);
    border-color: var(--green);
    color: var(--green);
    box-shadow: 0 0 8px rgba(0,255,65,0.2);
  }

  .espresso-btn {
    background: rgba(0,255,65,0.05);
    border: 1px solid var(--border);
    color: var(--text-dim);
  }

  .espresso-btn:hover {
    border-color: var(--green);
    color: var(--green);
  }

  /* Shot log panel — full width */
  .full-width {
    grid-column: 1 / -1;
  }

  .shot-grid {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 12px;
    margin-top: 8px;
  }

  .shot-stat {
    background: var(--bg3);
    border: 1px solid var(--border);
    border-radius: 3px;
    padding: 10px;
    text-align: center;
  }

  .shot-stat-val {
    font-size: 22px;
    color: var(--green);
  }

  .shot-stat-label {
    font-size: 9px;
    color: var(--text-dim);
    letter-spacing: 0.15em;
    margin-top: 2px;
  }

  .no-shot {
    color: var(--text-dim);
    font-size: 11px;
    text-align: center;
    padding: 16px;
    letter-spacing: 0.1em;
  }

  .divider {
    border: none;
    border-top: 1px solid var(--border);
    margin: 12px 0;
  }

  .wifi-status {
    text-align: center;
    font-size: 9px;
    color: var(--text-dim);
    margin-top: 20px;
    letter-spacing: 0.15em;
  }
</style>
</head>
<body>

<header>
  <div class="frank-title">
    <span class="bolts">⚡</span>FRANK<span class="bolts">⚡</span>
  </div>
  <div class="frank-sub">ESPRESSO MACHINE CONTROL SYSTEM</div>
</header>

<div class="grid">

  <!-- ESPRESSO PANEL -->
  <div class="panel" id="espresso-panel">
    <div class="panel-title">// ESPRESSO MODE</div>

    <div class="temp-display" id="current-temp">--.-</div>
    <div style="font-size:11px;color:var(--text-dim);margin-bottom:8px;">CURRENT °C</div>

    <hr class="divider">

    <div class="target-row">
      <span class="target-label">TARGET</span>
      <button class="adj-btn" onclick="adjustTemp('espresso', -0.5)">−</button>
      <span class="target-val" id="esp-target">97.0</span>
      <button class="adj-btn" onclick="adjustTemp('espresso', 0.5)">+</button>
    </div>

    <div class="pwr-bar-wrap">
      <div class="pwr-label">BOILER POWER</div>
      <div class="pwr-bar-bg">
        <div class="pwr-bar-fill" id="pwr-bar" style="width:0%"></div>
      </div>
    </div>

    <div id="esp-state" class="state-badge state-heating">HEATING...</div>

    <button class="big-btn espresso-btn" onclick="setMode('espresso')">
      ☕ ESPRESSO MODE
    </button>

  </div>

  <!-- STEAM PANEL -->
  <div class="panel steam-panel" id="steam-panel">
    <div class="panel-title">// STEAM MODE</div>

    <div class="temp-display steam-temp" id="steam-current">--.-</div>
    <div style="font-size:11px;color:var(--text-dim);margin-bottom:8px;">CURRENT °C</div>

    <hr class="divider">

    <div class="target-row">
      <span class="target-label">STEAM TARGET</span>
      <button class="adj-btn" onclick="adjustTemp('steam', -1)" style="color:var(--orange);border-color:var(--orange)">−</button>
      <span class="target-val" id="steam-target" style="color:var(--orange)">145.0</span>
      <button class="adj-btn" onclick="adjustTemp('steam', 1)" style="color:var(--orange);border-color:var(--orange)">+</button>
    </div>

    <div class="pwr-bar-wrap">
      <div class="pwr-label">BOILER POWER</div>
      <div class="pwr-bar-bg">
        <div class="pwr-bar-fill steam-fill" id="steam-pwr-bar" style="width:0%"></div>
      </div>
    </div>

    <div id="steam-state" class="state-badge state-heating">HEATING...</div>

    <button class="big-btn steam-btn" id="steam-btn" onclick="setMode('steam')">
      〜 ACTIVATE STEAM
    </button>

    <button class="big-btn milk-btn" id="milk-btn" onclick="toggleMilk()">
      ☕ MILK DRINK MODE: OFF
    </button>

  </div>

  <!-- SHOT LOG PANEL -->
  <div class="panel full-width">
    <div class="panel-title">// LAST SHOT LOG</div>
    <div id="shot-log">
      <div class="no-shot">— no shot recorded this session —</div>
    </div>
  </div>

</div>

<div class="wifi-status" id="wifi-status">connecting...</div>

<script>
  let espTarget  = 97.0;
  let steamTarget = 145.0;
  let milkMode   = false;
  let currentMode = 'espresso';

  async function fetchStatus() {
    try {
      const r = await fetch('/status');
      const d = await r.json();

      // temps
      document.getElementById('current-temp').textContent  = d.temp.toFixed(1);
      document.getElementById('steam-current').textContent = d.temp.toFixed(1);

      // pwr bars
      document.getElementById('pwr-bar').style.width       = d.power + '%';
      document.getElementById('steam-pwr-bar').style.width = d.power + '%';

      // targets
      espTarget   = d.espressoTarget;
      steamTarget = d.steamTarget;
      document.getElementById('esp-target').textContent   = espTarget.toFixed(1);
      document.getElementById('steam-target').textContent = steamTarget.toFixed(1);

      // milk mode
      milkMode = d.milkMode;
      const milkBtn = document.getElementById('milk-btn');
      milkBtn.textContent = '☕ MILK DRINK MODE: ' + (milkMode ? 'ON' : 'OFF');
      milkBtn.classList.toggle('active', milkMode);

      // mode
      currentMode = d.mode;
      document.getElementById('steam-btn').classList.toggle('active', currentMode === 'steam');

      // states
      const atEspTarget   = Math.abs(d.temp - espTarget) < 1.5;
      const atSteamTarget = Math.abs(d.temp - steamTarget) < 1.5;

      const espState = document.getElementById('esp-state');
      if (currentMode === 'espresso') {
        espState.textContent  = atEspTarget ? '✓ READY' : 'HEATING...';
        espState.className    = 'state-badge ' + (atEspTarget ? 'state-ready' : 'state-heating');
      } else {
        espState.textContent  = 'STEAM ACTIVE';
        espState.className    = 'state-badge state-heating';
      }

      const steamState = document.getElementById('steam-state');
      if (currentMode === 'steam') {
        steamState.textContent = atSteamTarget ? '✓ STEAM READY' : 'HEATING...';
        steamState.className   = 'state-badge ' + (atSteamTarget ? 'state-ready' : 'state-heating');
      } else {
        steamState.textContent = 'STANDBY';
        steamState.className   = 'state-badge state-heating';
      }

      // shot log
      if (d.shotValid) {
        document.getElementById('shot-log').innerHTML = `
          <div class="shot-grid">
            <div class="shot-stat">
              <div class="shot-stat-val">${d.shotAvg.toFixed(1)}°</div>
              <div class="shot-stat-label">AVG TEMP</div>
            </div>
            <div class="shot-stat">
              <div class="shot-stat-val">${d.shotMin.toFixed(1)}°</div>
              <div class="shot-stat-label">MIN TEMP</div>
            </div>
            <div class="shot-stat">
              <div class="shot-stat-val">${d.shotMax.toFixed(1)}°</div>
              <div class="shot-stat-label">MAX TEMP</div>
            </div>
          </div>`;
      }

      document.getElementById('wifi-status').textContent =
        'frank.local  ·  ' + d.ip + '  ·  ' + new Date().toLocaleTimeString();

    } catch(e) {
      document.getElementById('wifi-status').textContent = 'connection lost — retrying...';
    }
  }

  async function adjustTemp(mode, delta) {
    await fetch('/settemp', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({ mode, delta })
    });
    fetchStatus();
  }

  async function setMode(mode) {
    await fetch('/setmode', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({ mode })
    });
    fetchStatus();
  }

  async function toggleMilk() {
    await fetch('/togglemilk', { method: 'POST' });
    fetchStatus();
  }

  // Poll every second
  fetchStatus();
  setInterval(fetchStatus, 1000);
</script>
</body>
</html>
)rawliteral";
// ─────────────────────────────────────────

void setupWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    MDNS.begin("frank");
  }
}

void setupWebServer() {
  // Serve main page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send_P(200, "text/html", FRANK_HTML);
  });

  // Status endpoint
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    StaticJsonDocument<256> doc;
    doc["temp"]           = currentTemp;
    doc["power"]          = (int)((pidOutput / windowSize) * 100);
    doc["mode"]           = currentMode == MODE_STEAM ? "steam" : "espresso";
    doc["espressoTarget"] = espressoSetpoint;
    doc["steamTarget"]    = steamSetpoint;
    doc["milkMode"]      = milkModeEnabled;
    doc["shotValid"]     = lastShot.valid;
    doc["shotAvg"]       = lastShot.avgTemp;
    doc["shotMin"]       = lastShot.minTemp;
    doc["shotMax"]       = lastShot.maxTemp;
    doc["ip"]            = WiFi.localIP().toString();
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Set temperature
  server.on("/settemp", HTTP_POST, [](AsyncWebServerRequest *req){},
    NULL,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
      StaticJsonDocument<128> doc;
      deserializeJson(doc, data, len);
      String mode  = doc["mode"].as<String>();
      float  delta = doc["delta"].as<float>();
      if (mode == "espresso") {
        espressoSetpoint = constrain(espressoSetpoint + delta, 85.0, 105.0);
        if (currentMode == MODE_ESPRESSO) setpoint = espressoSetpoint;
      } else if (mode == "steam") {
        steamSetpoint = constrain(steamSetpoint + delta, 120.0, 144.0);
        if (currentMode == MODE_STEAM) setpoint = steamSetpoint;
      }
      req->send(200);
    }
  );

  // Set mode
  server.on("/setmode", HTTP_POST, [](AsyncWebServerRequest *req){},
    NULL,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t) {
      StaticJsonDocument<64> doc;
      deserializeJson(doc, data, len);
      String mode = doc["mode"].as<String>();
      if (mode == "steam") {
        previousMode = currentMode;
        currentMode  = MODE_STEAM;
        setpoint     = steamSetpoint;
        myPID.SetTunings(STEAM_KP, STEAM_KI, STEAM_KD);
      } else {
        currentMode  = MODE_ESPRESSO;
        setpoint     = espressoSetpoint;
        myPID.SetTunings(KP, KI, KD);
      }
      req->send(200);
    }
  );

  // Toggle milk mode
  server.on("/togglemilk", HTTP_POST, [](AsyncWebServerRequest *req) {
    milkModeEnabled = !milkModeEnabled;
    req->send(200);
  });

  server.begin();
}

void setup() {
  Serial.begin(115200);
  pinMode(SSR_PIN, OUTPUT);
  digitalWrite(SSR_PIN, LOW);

  // OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  delay(1000);
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    display.clearDisplay();
    display.display();
    frankStartupAnimation();
  }

  // PID
  myPID.SetOutputLimits(0, windowSize);
  myPID.SetSampleTime(100);
  myPID.SetMode(AUTOMATIC);
  windowStartTime = millis();

  // WiFi
  setupWiFi();
  setupWebServer();

  Serial.print("Frank online at: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  unsigned long now = millis();

  // Read temp every 100ms
  if (now - lastTempRead >= 100) {
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
      return;
    }

    currentTemp = reading;
    myPID.Compute();

    // Steam boost: full power when more than 3°C below setpoint
    if (currentMode == MODE_STEAM && (setpoint - currentTemp) > 3.0) {
      pidOutput = windowSize;
    }

    // Milk mode — auto switch to steam when espresso ready and temp drops
    // (temp drop after shot indicates cold water hit boiler)
    if (milkModeEnabled && currentMode == MODE_ESPRESSO) {
      static float prevTemp = 0;
      if (prevTemp - currentTemp > 3.0 && currentTemp > 80.0) {
        // Temperature dropped — shot likely pulled; auto-switch to steam
        currentMode = MODE_STEAM;
        setpoint    = steamSetpoint;
        myPID.SetTunings(STEAM_KP, STEAM_KI, STEAM_KD);
      }
      prevTemp = currentTemp;
    }

    // If steam mode and milk mode on — auto return to espresso after reaching steam temp
    // (user manually switches back via web UI button)
  }

  // SSR control
  if (now - windowStartTime > windowSize) {
    windowStartTime += windowSize;
  }
  digitalWrite(SSR_PIN, (pidOutput > (now - windowStartTime)) ? HIGH : LOW);

  // Update display every 200ms
  if (now - lastDisplayUpdate >= 200) {
    lastDisplayUpdate = now;
    updateDisplay();
  }

  // Serial telemetry every 2s
  if (now - lastSerialPrint >= 2000) {
    lastSerialPrint = now;
    Serial.print("Temp: "); Serial.print(currentTemp, 1);
    Serial.print("C | Target: "); Serial.print(setpoint, 1);
    Serial.print("C | PWR: "); Serial.print((int)((pidOutput/windowSize)*100));
    Serial.print("% | Mode: "); Serial.println(currentMode == MODE_STEAM ? "STEAM" : "ESPRESSO");
  }
}