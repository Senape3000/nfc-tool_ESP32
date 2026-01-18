# 📝 LOGGER SYSTEM - Complete Guide

## 📋 OVERVIEW

Professional structured logging system for ESP32 projects. Replace scattered `Serial.println()` calls with leveled, timestamped, color-coded logging that can be filtered at compile-time.

## ✨ FEATURES

✅ **6 Log Levels:** CRITICAL, ERROR, WARN, INFO, DEBUG, VERBOSE  
✅ **Compile-Time Filtering:** Zero overhead for disabled levels  
✅ **Automatic Timestamps:** `[00123.456]` millisecond precision  
✅ **ANSI Colors:** Red errors, yellow warnings, green info  
✅ **Module Prefixes:** `[NFC]`, `[WIFI]`, `[I2C]` automatic formatting  
✅ **Printf-Style Formatting:** `LOG_INFO("MODULE", "Value: %d", x)`  
✅ **Hex Dump Utility:** `LOG_HEX_DUMP()` for binary data  
✅ **Assert Macro:** `LOG_ASSERT()` with automatic crash  
✅ **Production Ready:** Disable all logs with single flag  

## 🚀 QUICK START

### 1. Include Header

\`\`\`cpp
#include "logger.h"
\`\`\`

### 2. Initialize in setup()

\`\`\`cpp
void setup() {
    Serial.begin(115200);
    Logger::begin();  // Optional but recommended

    LOG_INFO("SETUP", "System starting...");
}
\`\`\`

### 3. Use Anywhere

\`\`\`cpp
LOG_INFO("WIFI", "Connected to %s", WiFi.SSID().c_str());
LOG_ERROR("NFC", "PN532 timeout after %d ms", timeout);
LOG_DEBUG("I2C", "Read register 0x%02X: 0x%02X", reg, value);
\`\`\`

## 📊 LOG LEVELS

### Visual Reference

\`\`\`
CRITICAL  🔴🔴🔴  [CRIT]  System non-operational, imminent crash
ERROR     🔴      [ERR ]  Operation failed but system operational
WARN      🟡      [WARN]  Non-blocking anomaly, unexpected behavior
INFO      🟢      [INFO]  Normal events, system status
DEBUG     🔵      [DBG ]  Detailed debugging information
VERBOSE   ⚪      [VERB]  Everything including data dumps
\`\`\`

### Usage Matrix

| Level      | Production | Development | Use Case                           |
|-----------|------------|-------------|-----------------------------------|
| CRITICAL  | ✅ YES     | ✅ YES      | Out of memory, hardware failure   |
| ERROR     | ✅ YES     | ✅ YES      | I2C timeout, file open failed     |
| WARN      | ✅ YES     | ✅ YES      | Low memory, weak WiFi signal      |
| INFO      | ⚠️ MAYBE   | ✅ YES      | WiFi connected, tag detected      |
| DEBUG     | ❌ NO      | ✅ YES      | Register values, state changes    |
| VERBOSE   | ❌ NO      | ⚠️ MAYBE    | Complete buffer dumps             |

## 🎯 COMMON EXAMPLES

### System Initialization

\`\`\`cpp
void setup() {
    Serial.begin(115200);
    Logger::begin();

    LOG_INFO("SETUP", "ESP32 NFC Tool v1.0");
    LOG_INFO("SETUP", "Chip: %s, CPU: %d MHz", 
             ESP.getChipModel(), ESP.getCpuFreqMHz());
    LOG_INFO("SETUP", "Free Heap: %d bytes", ESP.getFreeHeap());
}
\`\`\`

**Output:**
\`\`\`
[00000.123] [INFO] [LOGGER  ] Logging system initialized
[00000.124] [INFO] [LOGGER  ] Level: DEBUG
[00000.500] [INFO] [SETUP   ] ESP32 NFC Tool v1.0
[00000.501] [INFO] [SETUP   ] Chip: ESP32, CPU: 240 MHz
[00000.502] [INFO] [SETUP   ] Free Heap: 298456 bytes
\`\`\`

### WiFi Connection

\`\`\`cpp
LOG_INFO("WIFI", "Connecting to %s...", ssid);

if (WiFi.status() == WL_CONNECTED) {
    LOG_INFO("WIFI", "Connected! IP: %s", WiFi.localIP().toString().c_str());
    LOG_INFO("WIFI", "RSSI: %d dBm", WiFi.RSSI());
} else {
    LOG_ERROR("WIFI", "Connection failed after %d attempts", attempts);
}
\`\`\`

### NFC Operations

\`\`\`cpp
LOG_INFO("NFC", "Waiting for tag...");

if (nfc.readTag(uid, timeout)) {
    LOG_INFO("NFC", "Tag detected - UID: %s", uidToHex(uid));
    LOG_DEBUG("NFC", "Tag type: %s, size: %d bytes", tagType, size);

    // Hex dump of tag data
    LOG_HEX_DUMP("NFC", buffer, 64, "Tag content");
} else {
    LOG_WARN("NFC", "Timeout - no tag detected after %d seconds", timeout);
}
\`\`\`

### Error Handling

\`\`\`cpp
if (!file.open("/config.json", "r")) {
    LOG_ERROR("FLASH", "Failed to open config file");
    LOG_WARN("FLASH", "Using default configuration");
    return false;
}

if (ESP.getFreeHeap() < 10000) {
    LOG_WARN("MEMORY", "Low memory: %d bytes free", ESP.getFreeHeap());
}

if (ptr == nullptr) {
    LOG_CRITICAL("MEMORY", "Null pointer detected in critical section");
    ESP.restart();
}
\`\`\`

### Debug Tracing

\`\`\`cpp
void processData(uint8_t* data, size_t len) {
    LOG_DEBUG("PROCESS", "Processing %d bytes", len);

    for (int i = 0; i < len; i++) {
        LOG_VERBOSE("PROCESS", "Byte[%d] = 0x%02X", i, data[i]);
    }

    LOG_DEBUG("PROCESS", "Processing complete");
}
\`\`\`

### Assert for Invariants

\`\`\`cpp
void initHardware() {
    LOG_ASSERT(nfc != nullptr, "NFC", "NFC object is null");
    LOG_ASSERT(bufferSize > 0, "BUFFER", "Invalid buffer size: %d", bufferSize);

    // Code here only runs if assertions pass
}
\`\`\`

## 🎨 HEX DUMP UTILITY

### Basic Usage

\`\`\`cpp
uint8_t buffer[64];
// ... fill buffer ...

LOG_HEX_DUMP("NFC", buffer, sizeof(buffer), "Tag data");
\`\`\`

### Output Example

\`\`\`
[00012.345] [DBG ] [NFC     ] Tag data (64 bytes):
  0000: E0 07 A1 B2 C3 D4 E5 F6 00 00 00 00 00 00 00 00  | ................
  0010: FF FF FF FF FF FF FF FF 00 00 00 00 00 00 00 00  | ................
  0020: 12 34 56 78 9A BC DE F0 01 23 45 67 89 AB CD EF  | .4Vx.....#Eg....
  0030: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  | ................
\`\`\`

### Practical Examples

\`\`\`cpp
// Dump I2C register contents
uint8_t regs[16];
i2c_read(address, regs, 16);
LOG_HEX_DUMP("I2C", regs, 16, "Device registers");

// Dump NFC UID
uint8_t uid[7];
nfc.readUID(uid);
LOG_HEX_DUMP("NFC", uid, 7, "Card UID");

// Dump packet data
LOG_HEX_DUMP("NET", packet, packet_len, "Received packet");
\`\`\`

## ⚙️ CONFIGURATION

### platformio.ini Settings

#### Development (All logs + colors)

\`\`\`ini
[env:development]
build_flags = 
    -DLOG_LEVEL=LOG_LEVEL_DEBUG
    -DLOG_COLORS_ENABLED=1
    -DLOG_TIMESTAMP_ENABLED=1

monitor_filters = 
    colorize
    esp32_exception_decoder
\`\`\`

#### Production (Warnings + Errors only)

\`\`\`ini
[env:production]
build_flags = 
    -DLOG_LEVEL=LOG_LEVEL_WARN
    -DLOG_COLORS_ENABLED=0
    -DLOG_TIMESTAMP_ENABLED=0
\`\`\`

#### Release (Zero logging)

\`\`\`ini
[env:release]
build_flags = 
    -DLOG_LEVEL=LOG_LEVEL_NONE
\`\`\`

### Configuration Options

| Flag                     | Values                          | Default      | Description                |
|-------------------------|--------------------------------|--------------|---------------------------|
| LOG_LEVEL               | LOG_LEVEL_NONE to VERBOSE      | DEBUG (dev)  | Minimum level to print    |
| LOG_COLORS_ENABLED      | 0 or 1                         | 1 (dev)      | Enable ANSI colors        |
| LOG_TIMESTAMP_ENABLED   | 0 or 1                         | 1            | Show millisecond timer    |
| LOG_BUFFER_SIZE         | 64-512                         | 256          | Message buffer size       |

### Level Constants

\`\`\`cpp
LOG_LEVEL_NONE     = 0  // Disable all logging
LOG_LEVEL_CRITICAL = 1  // Only critical errors
LOG_LEVEL_ERROR    = 2  // Errors and critical
LOG_LEVEL_WARN     = 3  // Warnings, errors, critical
LOG_LEVEL_INFO     = 4  // Info and above (production)
LOG_LEVEL_DEBUG    = 5  // Debug and above (development)
LOG_LEVEL_VERBOSE  = 6  // Everything (very noisy)
\`\`\`

## 📝 MODULE NAMING GUIDE

Use descriptive 8-character module names:

| Module Name | Purpose                      |
|------------|------------------------------|
| SETUP      | setup() initialization       |
| LOOP       | loop() main execution        |
| NFC        | NFC Manager (generic)        |
| SRIX       | SRIX4K specific operations   |
| MIFARE     | Mifare Classic operations    |
| PN532      | PN532 hardware driver        |
| I2C        | I2C bus operations           |
| WIFI       | WiFi Manager                 |
| API        | WebServer API endpoints      |
| WEB        | WebServer core               |
| SERIAL     | Serial Commander             |
| FLASH      | LittleFS/SPIFFS operations   |
| LED        | LED Manager                  |
| KEYS       | Mifare Keys Manager          |
| AUTH       | Authentication/Login         |
| MEMORY     | Memory allocation/heap       |
| TASK       | FreeRTOS task management     |
| SYSTEM     | System-level operations      |

## 🔄 MIGRATION FROM Serial.println()

### Before (Manual formatting)

\`\`\`cpp
Serial.println("[NFC] Reading tag...");
Serial.printf("[NFC] UID: %s\n", uid);

if (error) {
    Serial.println("[ERROR] PN532 timeout!");
}

for (int i = 0; i < len; i++) {
    Serial.printf("%02X ", buffer[i]);
}
Serial.println();
\`\`\`

**Problems:**
- ❌ No level filtering
- ❌ Manual prefix formatting
- ❌ No timestamps
- ❌ Always enabled (overhead)
- ❌ Manual hex formatting

### After (Structured logging)

\`\`\`cpp
LOG_INFO("NFC", "Reading tag...");
LOG_INFO("NFC", "UID: %s", uid);

if (error) {
    LOG_ERROR("NFC", "PN532 timeout after %d ms", timeout);
}

LOG_HEX_DUMP("NFC", buffer, len, "Tag data");
\`\`\`

**Benefits:**
- ✅ Automatic level filtering
- ✅ Consistent formatting
- ✅ Automatic timestamps
- ✅ Zero overhead when disabled
- ✅ Professional hex dump

### Conversion Rules

1. **Remove `\n`** - Added automatically
2. **Use `.c_str()`** for Arduino String: `myString.c_str()`
3. **Choose appropriate level** - INFO for normal, DEBUG for details
4. **Pick module name** - Max 8 characters, descriptive
5. **Printf-style formatting** - No String concatenation

## 🎓 BEST PRACTICES

### DO ✅

\`\`\`cpp
// Good: Printf-style formatting
LOG_INFO("WIFI", "Connected to %s", WiFi.SSID().c_str());

// Good: Descriptive context
LOG_ERROR("I2C", "Bus timeout after %d ms (addr: 0x%02X)", timeout, addr);

// Good: Appropriate level
LOG_DEBUG("SRIX", "Reading block %d", blockNum);  // DEBUG for details
LOG_INFO("SRIX", "Tag read successfully");         // INFO for results

// Good: Hex dump for binary data
LOG_HEX_DUMP("NFC", buffer, len, "Tag UID");

// Good: Assert for critical invariants
LOG_ASSERT(ptr != nullptr, "MEMORY", "Null pointer");
\`\`\`

### DON'T ❌

\`\`\`cpp
// Bad: String concatenation
LOG_INFO("WIFI", "Connected to " + WiFi.SSID());  // ❌ Won't compile

// Bad: Manual newline
LOG_INFO("WIFI", "Connected\n");  // ❌ Double newline

// Bad: Missing .c_str()
String msg = "Test";
LOG_INFO("TEST", msg);  // ❌ Wrong type

// Bad: Wrong level for production
LOG_DEBUG("SETUP", "System starting");  // ❌ Use INFO for important events

// Bad: Overly generic module name
LOG_INFO("MAIN", "Error occurred");  // ❌ Not descriptive enough

// Bad: Logging in tight loops
for (int i = 0; i < 10000; i++) {
    LOG_DEBUG("LOOP", "i = %d", i);  // ❌ Too much output
}
\`\`\`

### Correct String Handling

\`\`\`cpp
// Arduino String to char*
String ssid = WiFi.SSID();
LOG_INFO("WIFI", "SSID: %s", ssid.c_str());

// IPAddress to char*
IPAddress ip = WiFi.localIP();
LOG_INFO("WIFI", "IP: %s", ip.toString().c_str());

// Formatted strings
char buffer[64];
snprintf(buffer, sizeof(buffer), "Tag_%02X%02X", uid[0], uid[1]);
LOG_INFO("NFC", "Tag name: %s", buffer);
\`\`\`

## 📊 OUTPUT EXAMPLES

### Development Build (LOG_LEVEL_DEBUG)

\`\`\`
[00000.123] [INFO] [LOGGER  ] Logging system initialized
[00000.124] [INFO] [LOGGER  ] Level: DEBUG
[00000.125] [INFO] [LOGGER  ] Colors: enabled

[00000.500] [INFO] [SETUP   ] ESP32 NFC Tool v1.0
[00001.234] [INFO] [FLASH   ] Filesystem mounted (12KB used)
[00002.456] [INFO] [WIFI    ] Connecting...
[00005.789] [INFO] [WIFI    ] Connected to MyNetwork
[00005.790] [INFO] [WIFI    ] IP: 192.168.1.100, RSSI: -45 dBm
[00006.123] [DBG ] [I2C     ] Bus initialized (SDA=21, SCL=22)
[00007.456] [INFO] [NFC     ] PN532 ready - FW v1.6
[00007.789] [INFO] [SETUP   ] ✅ Initialization complete

[00015.123] [INFO] [NFC     ] Waiting for tag...
[00018.456] [INFO] [NFC     ] Tag detected - UID: E007A1B2C3D4
[00018.457] [DBG ] [NFC     ] Tag data (16 bytes):
  0000: E0 07 A1 B2 C3 D4 E5 F6 01 23 45 67 89 AB CD EF  | .........#Eg....
[00018.789] [INFO] [NFC     ] Read successful
\`\`\`

### Production Build (LOG_LEVEL_WARN)

\`\`\`
[WARN] [WIFI    ] Weak signal: -85 dBm
[ERROR] [NFC     ] PN532 timeout after 5000 ms
[WARN] [MEMORY  ] Low heap: 8234 bytes
\`\`\`

### Release Build (LOG_LEVEL_NONE)

\`\`\`
(no output - completely silent)
\`\`\`

## 🐛 TROUBLESHOOTING

### Issue: "LOG_INFO not declared"

**Cause:** LOG_LEVEL not defined  
**Solution:** Add to platformio.ini:
\`\`\`ini
build_flags = -DLOG_LEVEL=LOG_LEVEL_DEBUG
\`\`\`

### Issue: Colors not showing

**Cause:** Monitor doesn't support ANSI colors  
**Solution 1:** Add to platformio.ini:
\`\`\`ini
monitor_filters = colorize
\`\`\`
**Solution 2:** Disable colors:
\`\`\`ini
build_flags = -DLOG_COLORS_ENABLED=0
\`\`\`

### Issue: Compilation error with String

**Cause:** Forgot `.c_str()`  
**Wrong:**
\`\`\`cpp
String msg = "test";
LOG_INFO("MODULE", msg);  // ❌ Error
\`\`\`
**Correct:**
\`\`\`cpp
String msg = "test";
LOG_INFO("MODULE", "%s", msg.c_str());  // ✅ Works
\`\`\`

### Issue: LOG_DEBUG not printing

**Cause:** LOG_LEVEL too low  
**Solution:** Increase level:
\`\`\`ini
build_flags = -DLOG_LEVEL=LOG_LEVEL_DEBUG
\`\`\`

### Issue: Too much output

**Cause:** Too many debug logs  
**Solution:** Filter by level or use less verbose logging

## 📈 PERFORMANCE

### Binary Size Impact

| Configuration    | Binary Size | Savings vs Always-On |
|-----------------|-------------|---------------------|
| LOG_LEVEL_NONE  | Baseline    | ~70 KB              |
| LOG_LEVEL_WARN  | +15 KB      | ~55 KB              |
| LOG_LEVEL_INFO  | +35 KB      | ~35 KB              |
| LOG_LEVEL_DEBUG | +70 KB      | 0 KB                |

### Runtime Overhead

| Operation       | Time (µs) | Notes                    |
|----------------|-----------|--------------------------|
| LOG_INFO       | ~150      | With timestamp + color   |
| LOG_INFO       | ~50       | No timestamp, no color   |
| LOG_DISABLED   | 0         | Compiled out completely  |
| LOG_HEX_DUMP   | ~50/line  | Depends on data size     |

### Memory Usage

- Stack per log call: ~260 bytes (LOG_BUFFER_SIZE + overhead)
- Heap: 0 bytes (no dynamic allocation)
- Global: ~100 bytes (color strings)

## 🎯 RECOMMENDED CONFIGURATIONS

### Development / Testing

\`\`\`ini
[env:dev]
build_flags = 
    -DDEBUG=1
    -DLOG_LEVEL=LOG_LEVEL_DEBUG
    -DLOG_COLORS_ENABLED=1
    -DLOG_TIMESTAMP_ENABLED=1
monitor_filters = colorize
\`\`\`

### Beta / Staging

\`\`\`ini
[env:beta]
build_flags = 
    -DLOG_LEVEL=LOG_LEVEL_INFO
    -DLOG_COLORS_ENABLED=0
    -DLOG_TIMESTAMP_ENABLED=1
\`\`\`

### Production

\`\`\`ini
[env:prod]
build_flags = 
    -DLOG_LEVEL=LOG_LEVEL_WARN
    -DLOG_COLORS_ENABLED=0
    -DLOG_TIMESTAMP_ENABLED=0
\`\`\`

### Release (Minimal)

\`\`\`ini
[env:release]
build_flags = 
    -DLOG_LEVEL=LOG_LEVEL_NONE
\`\`\`

## 📦 FILES

- **logger.h** (367 lines) - Single header, no .cpp needed
- **LOGGER_USAGE.md** (this file) - Complete documentation
- **LOGGER_QUICK_REFERENCE.md** - Quick cheat sheet

## ✅ COMPLETE EXAMPLE

\`\`\`cpp
#include <Arduino.h>
#include <WiFi.h>
#include "logger.h"

void setup() {
    Serial.begin(115200);
    Logger::begin();

    LOG_INFO("SETUP", "System starting - v1.0");
    LOG_INFO("SETUP", "Chip: %s", ESP.getChipModel());

    // WiFi connection
    LOG_INFO("WIFI", "Connecting to WiFi...");
    WiFi.begin("MySSID", "password");

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        LOG_DEBUG("WIFI", "Attempt %d...", attempts);
        delay(500);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        LOG_INFO("WIFI", "Connected!");
        LOG_INFO("WIFI", "IP: %s", WiFi.localIP().toString().c_str());
        LOG_INFO("WIFI", "RSSI: %d dBm", WiFi.RSSI());

        if (WiFi.RSSI() < -80) {
            LOG_WARN("WIFI", "Weak signal detected");
        }
    } else {
        LOG_ERROR("WIFI", "Connection failed after %d attempts", attempts);
    }

    LOG_INFO("SETUP", "Initialization complete");
}

void loop() {
    // Check memory every 10 seconds
    static uint32_t lastCheck = 0;
    if (millis() - lastCheck > 10000) {
        uint32_t freeHeap = ESP.getFreeHeap();
        LOG_DEBUG("MEMORY", "Free heap: %d bytes", freeHeap);

        if (freeHeap < 10000) {
            LOG_WARN("MEMORY", "Low memory: %d bytes", freeHeap);
        }

        lastCheck = millis();
    }

    delay(100);
}
\`\`\`

---

**Logger System v1.0** | Production-ready, zero-overhead when disabled  
Compatible with ESP32, Arduino, PlatformIO
╔════════════════════════════════════════════════════════════╗
║                  Lib by Senape3000    - 2026               ║
╚════════════════════════════════════════════════════════════╝