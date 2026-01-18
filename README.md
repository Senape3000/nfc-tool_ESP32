# ESP32 NFC Tool
![MainPage](https://github.com/Senape3000/nfc-tool_ESP32/blob/main/docs/screenshot/main_page.PNG)
> Firmware for **ESP32 DevKit V1** that turns the board into a Wi‑Fi enabled NFC tag reader/writer with a web UI and a serial console.
![PlatformIO](https://img.shields.io/badge/PlatformIO-ready-blue)
![ESP32](https://img.shields.io/badge/ESP32-supported-success)
![LittleFS](https://img.shields.io/badge/LittleFS-enabled-informational)

## Table of Contents

- [Overview](#overview)
- [Highlights](#highlights)
- [Hardware](#hardware)
- [Build & Flash (PlatformIO)](#build--flash-platformio)
- [Quick Usage](#quick-usage)
- [Runtime Data (LittleFS)](#runtime-data-littlefs)
- [To Do](#to-do)
- [Security Notes](#security-notes)

## Overview
ESP32 NFC Tool is a multi‑protocol NFC framework built around a **PN532** module, exposing high‑level operations for **SRIX** and **Mifare Classic** tags via HTTP (web UI) and Serial interfaces.

It uses **LittleFS** to store tag dumps, Wi‑Fi credentials, and key databases, and includes structured logging to simplify debugging on embedded targets.

  

## Highlights

- 📶 Wi‑Fi manager with saved credentials plus fallback AP mode.

- 🌐 Async web UI (ESPAsyncWebServer/AsyncTCP) with session/login support. [Screenshots](https://github.com/Senape3000/nfc-tool_ESP32/tree/main/docs/screenshot)

- 🧾 Serial console commands (type `help`) running in a dedicated FreeRTOS task.

- 🏷️ SRIX read/write flows with `.srix` dumps.

- 🏷️ Mifare Classic dump/read/write + key database support `.mfc` dumps.

- 💡 Non‑blocking LED patterns + structured log levels for diagnostics.

  

## Hardware

### Board

- MCU/Board: **ESP32 DevKit V1** (PlatformIO env: `esp32doit-devkit-v1`).

- Serial: **115200 baud**, 8N1.

  

### PN532 (I²C mode)

- I²C: SDA = **GPIO 21**, SCL = **GPIO 22** (recommended 100 kHz for stability).

- Optional IRQ = **GPIO 18**, RST = **GPIO 19** (when using hardware IRQ mode).

### Wiring schematic

[ESP32 DevKit V1 (or similar)](https://github.com/Senape3000/nfc-tool_ESP32/blob/main/docs/hw/devkit_wiring.png)

### Status LED

- Status LED: **GPIO 2** (active high/low configurable).


### Supported tags (quick view)


| Protocol | Tag types | Notes |

| SRIX (ISO 15693) | SRIX4K, SRIX512 | Read/write, dump to `.srix`. |

| Mifare Classic | 1K, 4K, “magic” cards | Full dump, UID read/cloning, `.mfc`. |

  

## Build & Flash (PlatformIO)

### Requirements

- PlatformIO project targeting `esp32doit-devkit-v1` with Arduino framework.

- LittleFS enabled (`board_build.filesystem = littlefs`) and custom partitions via `default.csv`.

  

### Dependencies (as used in the project)

-  `AsyncTCP@3.3.2`, `ESPAsyncWebServer@3.6.0`.

-  `ArduinoJson@^7.0.0`, `Adafruit PN532@^1.3.1`.

  

### Commands

- Build:

-  `pio run`

- Flash:

-  `pio run -t upload`

- Monitor (115200):

-  `pio device monitor -b 115200`

### WebServer

- Webserver files can be edited in [THIS directory](https://github.com/Senape3000/nfc-tool_ESP32/tree/main/web_server)
- **After every mod** you need to launch `gen_webfiles.py` to rebuild "*include/webFiles.h*"

  

## Quick Usage

  

- On boot, the firmware initializes filesystem, Wi‑Fi, web server, serial task, and NFC services, then prints access info (IP or `nfctool.local`) and available serial commands.

- Networking flow:

- Tries saved credentials from `/wifi_db.json`, otherwise prompts via Serial; if it fails, it starts AP mode `ESP32-NFCTool` / `nfctool123`.

- Typical NFC workflow:

- Read a tag → save dump to LittleFS → load an existing dump → write back / clone UID (where supported).


## Runtime Data (LittleFS)

- Tag dumps:

-  `/DUMPS/SRIX/` and `/DUMPS/MIFARE/` under `/DUMPS/`.

- Wi‑Fi credentials:

-  `/wifi_db.json`.

- Mifare keys database:

-  `/mifare_keys.txt`.

  

## To Do

- Better docs for wiring, compiling, rebuild web-files.h

- Port project to Esp32-MINI-D1

- Data Parser

- Upgrade AP Mode in Settings

  

## Security Notes

- Default web credentials are `admin` / `admin` and the default AP password is `nfctool123` (change before any real deployment).

- For production, lower `LOG_LEVEL`, disable `DEBUG_SKIP_AUTH`, and review filesystem usage to avoid exposing NFC dumps/keys.

- **This software is provided as is; there is no warranty on its functionality, and the user is solely responsible for any damage or corruption of data.**
**Any help is appreciated; you are welcome to contribute.**