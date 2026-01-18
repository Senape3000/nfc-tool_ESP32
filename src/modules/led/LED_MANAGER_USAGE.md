# ========================================
# LED MANAGER - USAGE GUIDE
# ========================================

## 📋 OVERVIEW

Enhanced LED manager with 11 non-blocking blink patterns. All patterns use Ticker for non-blocking operation - no delays, no CPU blocking.

## 🎯 FEATURES

✅ **11 Blink Patterns:**
- OFF: LED off
- ON: Solid on (connected state)
- SLOW_BLINK: 1 Hz (1000ms period) - idle/waiting
- NORMAL_BLINK: 2 Hz (500ms period) - default activity
- FAST_BLINK: 5 Hz (200ms period) - busy
- VERY_FAST_BLINK: 10 Hz (100ms period) - critical activity
- DOUBLE_BLINK: Quick double blink + pause - confirmation
- TRIPLE_BLINK: Quick triple blink + pause - special event
- HEARTBEAT: Double pulse + long pause - alive indicator
- PULSE: Smooth PWM fade in/out - breathing effect
- SOS: Morse code SOS pattern - error/help signal

✅ **100% Non-Blocking:** Uses Ticker library, zero blocking delays  
✅ **PWM Support:** Smooth pulse/breathing effect  
✅ **Active LOW Support:** For LEDs connected to VCC  
✅ **Logger Integration:** Debug logging for pattern changes  
✅ **Backward Compatible:** Works with existing code (blinking(), connected())


## 🚀 QUICK START

### Basic Setup

```cpp
#include "led_manager.h"

LedManager ledMgr;

void setup() {
    // Initialize with LED pin (active HIGH by default)
    ledMgr.begin(LED_PIN);

    // Or for active LOW LED:
    // ledMgr.begin(LED_PIN, true);
}
```

### Using Patterns

```cpp
// Method 1: Direct pattern setting
ledMgr.setPattern(LedPattern::FAST_BLINK);

// Method 2: Convenience methods
ledMgr.fastBlink();  // Same as above
```


## 📖 PATTERN EXAMPLES

### 1. System Boot / Initialization
```cpp
void setup() {
    ledMgr.begin(LED_PIN);
    ledMgr.slowBlink();  // Slow blink during boot

    // ... initialization code ...
}
```

### 2. WiFi Connection States
```cpp
// Connecting to WiFi
ledMgr.normalBlink();  // Normal blink while connecting

// Connected successfully
if (WiFi.status() == WL_CONNECTED) {
    ledMgr.connected();  // Solid ON
}

// Connection failed
else {
    ledMgr.fastBlink();  // Fast blink = error
}
```

### 3. NFC Operations
```cpp
// Waiting for tag
ledMgr.slowBlink();

// Tag detected, reading
ledMgr.fastBlink();

// Read successful - double blink confirmation
ledMgr.doubleBlink();
delay(2000);
ledMgr.slowBlink();  // Back to waiting

// Read error - SOS pattern
ledMgr.sos();
```

### 4. Status Indicators
```cpp
// System idle
ledMgr.heartbeat();  // Heartbeat = alive

// Processing data
ledMgr.fastBlink();

// Waiting for user input
ledMgr.pulse();  // Breathing effect

// Critical error
ledMgr.sos();  // SOS morse code
```

### 5. Confirmation Patterns
```cpp
// Single operation complete
ledMgr.doubleBlink();

// Multiple operations complete
ledMgr.tripleBlink();

// Special event (e.g., firmware update available)
ledMgr.tripleBlink();
```


## 🎨 PATTERN VISUAL REFERENCE

```
OFF:              ________________

ON:               ████████████████

SLOW_BLINK:       ██__██__██__██__  (1 Hz)

NORMAL_BLINK:     █_█_█_█_█_█_█_█_  (2 Hz)

FAST_BLINK:       ▀▄▀▄▀▄▀▄▀▄▀▄▀▄▀▄  (5 Hz)

VERY_FAST_BLINK:  ▀▀▄▄▀▀▄▄▀▀▄▄▀▀▄▄  (10 Hz)

DOUBLE_BLINK:     █_█____█_█____  (pulse-pulse-pause)

TRIPLE_BLINK:     █_█_█______█_█_█______  (3 pulses + pause)

HEARTBEAT:        █_█_______█_█_______  (lub-dub pattern)

PULSE:            ▁▂▃▄▅▆▇█▇▆▅▄▃▂▁  (smooth fade)

SOS:              ▀▀▀ ▀▀▀ ▀▀▀ ███ ███ ███ ▀▀▀ ▀▀▀ ▀▀▀
                  (... --- ... in morse)
```


## 🔧 ADVANCED USAGE

### Check Current State
```cpp
// Get current pattern
LedPattern current = ledMgr.getCurrentPattern();
if (current == LedPattern::FAST_BLINK) {
    // LED is fast blinking
}

// Check if LED is currently on
if (ledMgr.isOn()) {
    // LED is physically on right now
}
```

### Active LOW LEDs
```cpp
// For LEDs connected to VCC (cathode to GPIO)
ledMgr.begin(LED_PIN, true);  // true = inverted logic
```

### Integration with Logger
```cpp
// The LED manager automatically logs pattern changes:
ledMgr.setPattern(LedPattern::FAST_BLINK);
// Log output: [DBG] [LED] Pattern changed to: 4
```


## 📊 PATTERN TIMING REFERENCE

| Pattern          | Frequency | Period  | Use Case                    |
|------------------|-----------|---------|----------------------------|
| OFF              | -         | -       | LED disabled               |
| ON               | -         | -       | Connected/Ready            |
| SLOW_BLINK       | 1 Hz      | 1000ms  | Idle, waiting              |
| NORMAL_BLINK     | 2 Hz      | 500ms   | Default activity           |
| FAST_BLINK       | 5 Hz      | 200ms   | Busy, processing           |
| VERY_FAST_BLINK  | 10 Hz     | 100ms   | Critical activity          |
| DOUBLE_BLINK     | ~1 Hz     | ~1050ms | Confirmation               |
| TRIPLE_BLINK     | ~0.67 Hz  | ~1500ms | Special event              |
| HEARTBEAT        | ~1 Hz     | ~1000ms | Alive indicator            |
| PULSE            | ~1 Hz     | ~1000ms | Breathing (smooth)         |
| SOS              | ~0.27 Hz  | ~3700ms | Emergency/Error            |


## 🎯 RECOMMENDED USAGE PATTERNS

### Typical System States

```cpp
// ===== Boot Sequence =====
ledMgr.slowBlink();          // Booting
// ... init code ...
ledMgr.normalBlink();        // Connecting WiFi
// ... WiFi connection ...
ledMgr.connected();          // Connected

// ===== Operational States =====
ledMgr.heartbeat();          // Idle, alive
ledMgr.normalBlink();        // Normal activity
ledMgr.fastBlink();          // Busy processing
ledMgr.doubleBlink();        // Operation complete

// ===== Error States =====
ledMgr.veryFastBlink();      // Critical error
ledMgr.sos();                // Emergency/help needed
```


## ⚠️ IMPORTANT NOTES

### Non-Blocking Operation
All patterns are non-blocking. You can call them anytime without blocking your main loop:

```cpp
void loop() {
    // This is OK - LED continues blinking in background
    ledMgr.fastBlink();

    // Do other work
    handleNFC();
    processSerial();
    // LED keeps blinking the whole time
}
```

### Pattern Switching
You can switch patterns instantly:

```cpp
ledMgr.slowBlink();
delay(1000);
ledMgr.fastBlink();  // Immediately switches, no need to stop first
```

### Memory Usage
- RAM: ~20 bytes per instance
- No heap allocation
- Single Ticker instance


## 🐛 TROUBLESHOOTING

### LED not working
```cpp
// Check pin is correct
ledMgr.begin(LED_PIN);  // Verify LED_PIN value

// Try toggling manually
ledMgr.on();   // Should turn on
ledMgr.off();  // Should turn off
```

### LED is inverted (on when should be off)
```cpp
// Use inverted logic parameter
ledMgr.begin(LED_PIN, true);  // Active LOW
```

### Pulse pattern not smooth
```cpp
// Ensure pin supports PWM (DAC/LEDC capable)
// On ESP32: pins 0-19, 21-23, 25-27, 32-33 support PWM
ledMgr.pulse();
```

### Pattern seems slow/fast
```cpp
// Check you're using correct pattern
ledMgr.slowBlink();      // 1 Hz
ledMgr.normalBlink();    // 2 Hz (default)
ledMgr.fastBlink();      // 5 Hz
ledMgr.veryFastBlink();  // 10 Hz
```


## 📝 MIGRATION FROM OLD CODE

### Old Code
```cpp
LedManager ledMgr;
ledMgr.begin(LED_PIN);
ledMgr.blinking();   // Start blinking
ledMgr.connected();  // Solid on
ledMgr.off();        // Turn off
```

### New Code (100% compatible!)
```cpp
LedManager ledMgr;
ledMgr.begin(LED_PIN);
ledMgr.blinking();   // Still works! (normal blink)
ledMgr.connected();  // Still works! (solid on)
ledMgr.off();        // Still works!

// Plus new features:
ledMgr.fastBlink();     // NEW!
ledMgr.doubleBlink();   // NEW!
ledMgr.pulse();         // NEW!
// ... etc
```

**No code changes required!** Old methods still work.


## ✅ COMPLETE EXAMPLE

```cpp
#include <Arduino.h>
#include "led_manager.h"
#include "logger.h"

LedManager ledMgr;

void setup() {
    Serial.begin(115200);
    Logger::begin();

    // Initialize LED on pin 2
    ledMgr.begin(2);
    LOG_INFO("SETUP", "LED Manager initialized");

    // Boot sequence
    ledMgr.slowBlink();
    delay(2000);

    // Simulate WiFi connection
    LOG_INFO("WIFI", "Connecting...");
    ledMgr.normalBlink();
    delay(3000);

    // Connected
    LOG_INFO("WIFI", "Connected!");
    ledMgr.connected();
    delay(2000);

    // Show heartbeat
    LOG_INFO("SETUP", "Ready");
    ledMgr.heartbeat();
}

void loop() {
    // LED continues heartbeat pattern in background
    // Your code here...

    delay(100);
}
```


## 🎓 PATTERN SELECTION GUIDE

**Choose the right pattern for your use case:**

| Situation                      | Recommended Pattern    |
|-------------------------------|------------------------|
| System idle, waiting          | `slowBlink()`          |
| Connecting to WiFi            | `normalBlink()`        |
| WiFi connected                | `connected()` or `on()`|
| Processing NFC tag            | `fastBlink()`          |
| Critical operation            | `veryFastBlink()`      |
| Operation successful          | `doubleBlink()`        |
| Special event                 | `tripleBlink()`        |
| System alive indicator        | `heartbeat()`          |
| Waiting for user              | `pulse()`              |
| Error / Emergency             | `sos()`                |
| System off / disabled         | `off()`                |


## 📦 FILES

- `led_manager.h` - Header with pattern enum and class definition
- `led_manager.cpp` - Implementation with all pattern logic
- `LED_MANAGER_USAGE.md` - This guide


---

**LED Manager v2.0** - Enhanced, non-blocking, production-ready  
All patterns tested and verified on ESP32

╔════════════════════════════════════════════════════════════╗
║                  Lib by Senape3000    - 2026               ║
╚════════════════════════════════════════════════════════════╝