# ESP32 Coffee Machine

A PlatformIO project for controlling a coffee machine using an **Espressif ESP32 Dev Module**.

## Setup

- **Board:** Espressif ESP32 Dev Module (`esp32dev`)
- **Framework:** Arduino
- **Monitor Speed:** 115200 baud

## Getting Started

1. Install [PlatformIO](https://platformio.org/) (VS Code extension recommended).
2. Open this folder in VS Code with PlatformIO installed.
3. Build: `pio run`
4. Upload: `pio run --target upload`
5. Monitor serial output: `pio device monitor`

## Project Structure

```
ESP32_COFFEE_MACHINE/
├── platformio.ini   # PlatformIO configuration
├── src/
│   └── main.cpp     # Main application entry point
└── README.md
```
