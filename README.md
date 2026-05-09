# Frank — ESP32 Espresso Machine PID Mod

An open-source PID temperature controller mod for cheap espresso machines, with an OLED display and self-hosted web UI accessible at `frank.local`.

---

## What is this?

Frank is a hardware mod that turns a budget espresso machine (like the Tru) into a properly temperature-controlled brewer. Most entry-level machines use a bimetal thermostat that overshoots wildly and never stabilizes — Frank replaces that with real PID control, holding the boiler within ±0.5°C of your target.

The physical thermostat is bypassed and replaced by a solid-state relay (SSR) driven by an ESP32. A MAX31855 thermocouple reads the boiler temperature every 100ms, a PID loop decides how much power to send, and the SSR switches mains current to the heating element in a time-proportioned window.

On top of that, Frank runs a self-hosted web UI that lets you tune targets, switch modes, enable milk-drink automation, and view a shot log — all from your phone or laptop over your local network.

---

## Features

- PID temperature control with ±0.5°C stability at target
- Separate tuning profiles for espresso (gentle) and steam (aggressive)
- Steam boost mode — full power when more than 3°C below steam target for fast recovery
- OLED display with a custom Frankenstein startup animation
- Self-hosted web UI accessible at `http://frank.local` (mDNS) or the device IP
- Milk drink mode — automatically switches to steam mode after detecting a shot pull
- Last shot temperature log (avg, min, max)

---

## Hardware Required

| Component | Notes |
|-----------|-------|
| ESP32 dev board (30-pin) | Any standard ESP32 DevKit |
| MAX31855 thermocouple amplifier + K-type thermocouple | Adafruit or clone |
| 40A Solid State Relay (SSR-40DA) | DC control side driven by ESP32 |
| 0.96" SSD1306 OLED display | I2C, 128×64 |
| HLK-PM01 AC-to-5V power supply | For permanent install inside the machine |
| 14–16 AWG wire, heat shrink, soldering supplies | 600V-rated for mains connections |

---

## Wiring

| ESP32 Pin | Component |
|-----------|-----------|
| 3.3V | MAX31855 VIN, OLED VCC |
| GND | MAX31855 GND, OLED GND, SSR DC− |
| GPIO 5 | MAX31855 CS |
| GPIO 18 | MAX31855 CLK |
| GPIO 19 | MAX31855 DO |
| GPIO 21 | OLED SDA |
| GPIO 22 | OLED SCL |
| GPIO 26 | SSR DC+ |

---

## Software Setup

1. Clone this repo
2. Open the folder in VS Code with the [PlatformIO](https://platformio.org/) extension installed
3. Copy `include/secrets.h.example` to `include/secrets.h`
4. Edit `include/secrets.h` and fill in your WiFi network name and password
5. Open `platformio.ini` and uncomment the `upload_port` / `monitor_port` lines, setting them to your serial port (e.g. `/dev/cu.usbserial-XXXX` on macOS, `COM3` on Windows)
6. Click **Build** then **Upload** in PlatformIO, or run `pio run --target upload`

---

## Web UI

After flashing and connecting to WiFi, open a browser and navigate to:

```
http://frank.local
```

or use the device's IP address printed to the serial monitor on boot. The UI updates every second and lets you adjust targets, switch between espresso and steam modes, and enable milk drink automation.

---

## ⚠️ Safety Warning — Mains Voltage

**This project involves mains voltage wiring (120/240V AC) inside the espresso machine. Mistakes can cause electric shock, fire, or death.**

- Always unplug the machine before touching any internal wiring
- Use 600V-rated wire (14–16 AWG) for all mains connections
- Verify all connections with a multimeter before powering on
- If you don't have experience with mains wiring, get help from someone who does
- This project is shared as-is. **You are responsible for your own safety.**

---

## License

MIT — do whatever you want, no warranty implied.

---
