# ESP32-NFC-Tool - Development Status & Resume Guide

**Last Updated**: February 3, 2026  
**Status**: ✅ **Stable Build** - BLE Communication Fully Functional  
**Build**: Release Mode, Exit Code 0

---

## 🎯 Current Status

### ✅ Completed Features

#### BLE Communication Layer
- **Nordic UART Service (NUS)** implementation completa
- **Multi-client support** (fino a 2 client simultanei)
- **MTU negotiation** automatica fino a 512 bytes
- **Thread-safe operations** con mutex per connection/stats tracking
- **Connection timeout handling** con gestione corretta degli handle
- **Auto-reconnection** con exponential backoff
- **SHA-256 checksum** per trasferimenti > 1024 bytes

#### Command Routing
- **Command categories**: wifi, nfc, system, files, bt
- **Dual format support**: 
  - Legacy: `{"cmd":"wifi_status"}`
  - Bruce format: `{"cmd":"wifi","params":{"action":"status"}}`
- **Validation completa** di tutti i comandi in ingresso
- **Error handling robusto** con codici di errore standardizzati

#### Hardware Integration
- **PN532 NFC Reader** via I2C/SPI
- **SRIX tag support** (lettura/scrittura)
- **Mifare Classic** support (UID, full dump, write)
- **LED status indicator** con pattern per diversi stati
- **WiFi Manager** con AP fallback mode
- **SD Card** support per storage tag dumps

---

## 🔧 Recent Fixes (Feb 3, 2026)

### Critical Bug Fixes

1. **Watchdog Timer Panic** ✅ FIXED
   - **Issue**: `checkConnectionTimeouts()` chiamava `server->disconnect()` dentro critical section
   - **Symptom**: Deadlock → watchdog timeout → ESP32 crash
   - **Fix**: Raccolta handles timeout prima, disconnect fuori dalla critical section
   - **File**: `src/modules/bluetooth/ble_handler.cpp` line ~733

2. **Connection Timeout Loop** ✅ FIXED
   - **Issue**: `updateConnectionActivity(0)` usava handle hardcoded invece del reale
   - **Symptom**: Activity non tracciata → timeout spurio ogni 30s → disconnessione continua
   - **Fix**: Passaggio `conn_handle` reale da `connInfo.getConnHandle()` in `onWrite`
   - **Files**: 
     - `src/modules/bluetooth/ble_handler.cpp` line ~564
     - `src/modules/bluetooth/ble_handler.h` (signature update)

3. **Command Routing for Bruce** ✅ FIXED
   - **Issue**: `getCommandType()` riconosceva solo comandi prefissati ("wifi_status")
   - **Symptom**: Comandi Bruce ("wifi" + params.action) classificati come "unknown"
   - **Fix**: Aggiunto riconoscimento diretto di categorie ("wifi", "system", "bt", etc.)
   - **File**: `src/modules/bluetooth/bluetooth_manager.cpp` line ~747

4. **Compilation Errors** ✅ FIXED
   - ESP_LOG_LEVEL macro redefinition → Fixed guard in `src/logger.h`
   - Duplicate function implementations → Removed from `ble_handler.cpp`
   - Invalid NimBLE API calls → Removed non-existent methods
   - StaticJsonDocument deprecation → Replaced with JsonDocument
   - Private method visibility → Moved `checkConnectionTimeouts()` to public

---

## 📊 Architecture Overview

### BLE Handler (`src/modules/bluetooth/ble_handler.cpp`)
```
┌─────────────────────────────────────────┐
│         BLE Handler Layer               │
│                                         │
│  ┌───────────────────────────────────┐ │
│  │  NimBLE Server                    │ │
│  │  - Nordic UART Service (NUS)     │ │
│  │  - TX/RX Characteristics         │ │
│  │  - Connection Management          │ │
│  └───────────────────────────────────┘ │
│                                         │
│  ┌───────────────────────────────────┐ │
│  │  Connection Tracking              │ │
│  │  - Per-connection handle/MTU      │ │
│  │  - Activity timestamps            │ │
│  │  - Timeout monitoring (30s)       │ │
│  └───────────────────────────────────┘ │
│                                         │
│  ┌───────────────────────────────────┐ │
│  │  Command Buffer                   │ │
│  │  - JSON parsing                   │ │
│  │  - Brace counting validation      │ │
│  │  - Max 2048 bytes                 │ │
│  └───────────────────────────────────┘ │
└─────────────────────────────────────────┘
```

### Bluetooth Manager (`src/modules/bluetooth/bluetooth_manager.cpp`)
```
┌─────────────────────────────────────────┐
│      Command Router & Handler           │
│                                         │
│  processCommand()                       │
│       ↓                                 │
│  getCommandType()                       │
│       ↓                                 │
│  ┌──────────┬──────────┬──────────┐   │
│  │  WiFi    │   NFC    │  System  │   │
│  │ Handler  │ Handler  │ Handler  │   │
│  └──────────┴──────────┴──────────┘   │
│  ┌──────────┬──────────┐              │
│  │  Files   │    BT    │              │
│  │ Handler  │ Handler  │              │
│  └──────────┴──────────┘              │
│                                         │
│  sendResponse() / sendError()          │
│       ↓                                 │
│  BLE Handler → TX Characteristic       │
└─────────────────────────────────────────┘
```

---

## 🐛 Known Issues & Next Steps

### 🔍 Under Investigation

1. **Bruce "System Info Failed" / "BT Status Failed"**
   - **Symptom**: Bruce riceve JSON ma report "failed"
   - **Status**: Debugging in corso con LOG_VERBOSE per JSON content
   - **Next**: Verificare formato response vs aspettative Bruce
   - **Log Added**: `LOG_VERBOSE("BLE", "JSON: %s", responseStr.c_str());`

### 📋 Planned Improvements

1. **NFC Operations Timeout Tuning**
   - Current: 5s default timeout per tag read
   - Issue: Potrebbe essere troppo breve per alcuni tag
   - Action: Monitorare e aumentare se necessario

2. **File Transfer Optimization**
   - Current: MTU up to 512 bytes
   - Enhancement: Chunk-based transfer per file > MTU
   - Priority: Medium (file list funziona)

3. **Connection Parameter Negotiation**
   - Current: Parameters impostati ma NimBLE non espone API diretti
   - Enhancement: Verificare se connection interval può essere ottimizzato
   - Priority: Low (performance accettabili)

---

## 🚀 How to Resume Development

### Quick Start
```bash
cd c:\Users\Andrea\VSCode_project\nfc-tool_ESP32
C:\Users\Andrea\.platformio\penv\Scripts\platformio.exe run --environment esp32doit-devkit-v1
```

### Upload to Device
```bash
C:\Users\Andrea\.platformio\penv\Scripts\platformio.exe run --environment esp32doit-devkit-v1 --target upload --upload-port COM9
```

### Monitor Serial Output
```bash
C:\Users\Andrea\.platformio\penv\Scripts\platformio.exe device monitor --port COM9 --baud 115200
```

### Key Configuration Files
- **BLE Settings**: `src/config.h` lines 95-140
- **Command Routing**: `src/modules/bluetooth/bluetooth_manager.cpp` line 133+
- **Connection Handling**: `src/modules/bluetooth/ble_handler.cpp` line 733+

### Debug Log Levels
Edit `src/logger.h` line 46:
```cpp
#ifndef ESP_LOG_LEVEL
    #define ESP_LOG_LEVEL 5  // 0=NONE, 5=DEBUG, 6=VERBOSE
#endif
```

---

## 📦 Dependencies

### Installed Libraries
- **NimBLE-Arduino** v2.3.7 - BLE stack
- **ArduinoJson** v7.4.2 - JSON parsing
- **Adafruit PN532** v1.3.4 - NFC reader
- **ESPAsyncWebServer** v3.6.0 - Web interface
- **AsyncTCP** v3.3.2 - TCP async operations

### Platform
- **ESP32 Dev Module** (esp32doit-devkit-v1)
- **Framework**: Arduino ESP32 v3.3.6
- **Toolchain**: xtensa-esp-elf v14.2.0

---

## 🧪 Testing with Bruce Device

### Connection Flow
1. Bruce scans for device with NUS UUID or name "ESP32-NFC-Tool"
2. Connection established → MTU negotiation
3. Bruce subscribes to TX characteristic
4. Commands sent via RX characteristic
5. Responses received via TX notifications

### Test Commands
```json
// System Info
{"cmd":"system","params":{"action":"info"},"id":"sys_info"}

// BT Status
{"cmd":"bt","params":{"action":"status"},"id":"bt_status"}

// Read Mifare UID
{"cmd":"nfc","params":{"action":"mifare_uid","timeout":5},"id":"nfc_mf_uid"}

// Read SRIX
{"cmd":"nfc","params":{"action":"read_srix","timeout":10},"id":"nfc_srix"}

// List Files
{"cmd":"files","params":{"action":"list","protocol":"srix"},"id":"files_list"}
```

### Expected Behavior
- ✅ Connection stable (no timeout loops)
- ✅ Commands routed correctly
- ✅ JSON responses complete and valid
- ⚠️ Bruce displays need verification (formatting issue suspected)

---

## 💾 Critical Files Reference

### Core Implementation
- `src/main.cpp` - Setup & main loop
- `src/config.h` - All configuration constants
- `src/logger.h` - Logging macros

### BLE Stack
- `src/modules/bluetooth/ble_handler.h/cpp` - BLE layer
- `src/modules/bluetooth/bluetooth_manager.h/cpp` - Command routing
- `src/modules/bluetooth/bt_security.h/cpp` - Checksum validation

### NFC Operations
- `src/modules/rfid/nfc_manager.h/cpp` - NFC abstraction
- `src/modules/rfid/mifare_tool.h/cpp` - Mifare operations
- `src/modules/rfid/srix_tool.h/cpp` - SRIX operations

### Utilities
- `src/modules/led/led_manager.h/cpp` - LED patterns
- `src/modules/wifi/wifi_manager.h/cpp` - WiFi management
- `src/modules/webserver/webserver_handler.h/cpp` - Web UI

---

## 🔗 External Documentation

- **Bruce Firmware Integration**: `docs/BRUCE_ESP32_INTEGRATION.md` (archived)
- **BLE API Specification**: `docs/ESP32_NFC_TOOL_BLE_API.md` (questo documento)
- **Original Fixes Summary**: Vedi attachment `BLE_FIXES_SUMMARY.md` da root

---

**Note**: Questo documento è pensato per resume rapido del progetto. Per dettagli sull'API BLE consultare `ESP32_NFC_TOOL_BLE_API.md`.
