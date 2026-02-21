/**
 * @file nrf_jammer.cpp
 * @brief 2.4 GHz jammer — serial-only implementation for ESP32 NFC Tool.
 *
 * Rewritten from scratch for this project (no display/Bruce dependencies).
 * Runs the jamming loop in a dedicated FreeRTOS task on Core 0.
 *
 * Hardware: E01-ML01DP5 (nRF24L01+ PA+LNA, +20 dBm effective)
 * SPI Bus:  HSPI (SCK=14, MISO=12, MOSI=13, CSN=15, CE=4)
 */

#include "nrf_jammer.h"

// ── Garbage payload for data flooding (32 bytes max TX burst) ───
static const uint8_t JAM_FLOOD_DATA[32] = {
    0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA,
    0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
    0xFF, 0x00, 0xFF, 0x00, 0xA5, 0x5A, 0xA5, 0x5A,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF
};

// ── Mode name table ─────────────────────────────────────────────
static const char* MODE_NAMES[NRF_JAM_MODE_COUNT] = {
    "Full Spectrum",    // 0
    "WiFi 2.4GHz",      // 1
    "BLE Data",          // 2
    "BLE Advertising",   // 3
    "BT Classic",        // 4
    "USB Dongles",       // 5
    "Video/FPV",         // 6
    "RC Controllers",    // 7
    "Zigbee",            // 8
    "Drone FHSS"         // 9
};

// ── Mode short names (for parsing) ──────────────────────────────
static const char* MODE_SHORT[NRF_JAM_MODE_COUNT] = {
    "full", "wifi", "ble", "ble_adv", "bt", "usb", "video", "rc", "zigbee", "drone"
};

// ── Channel lists ───────────────────────────────────────────────

// WiFi ch 1,6,11: each spans 22MHz, sub-channels cover bandwidth
static const uint8_t CH_WIFI[] = {
    1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23,       // WiFi ch 1
    26, 28, 30, 32, 34, 36, 38, 40, 42,               // WiFi ch 6
    51, 53, 55, 57, 59, 61, 63, 65, 67, 69, 71, 73    // WiFi ch 11
};

// BLE data channels: nRF24 ch 2-80 (even numbers cover BLE ch 0-36)
static const uint8_t CH_BLE[] = {
    2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28,
    30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54,
    56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80
};

// BLE advertising: ch37=2402→nRF ch2, ch38=2426→nRF ch26, ch39=2480→nRF ch80
static const uint8_t CH_BLE_ADV[] = {2, 26, 80};

// Classic Bluetooth: all FHSS channels 2-80
static const uint8_t CH_BLUETOOTH[] = {
    2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
    34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
    66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80
};

// USB wireless dongles (typical channels)
static const uint8_t CH_USB[] = {40, 50, 60};

// Video streaming / FPV (upper ISM band)
static const uint8_t CH_VIDEO[] = {70, 75, 80};

// RC controllers (low channels)
static const uint8_t CH_RC[] = {1, 3, 5, 7};

// Zigbee ch 11-26: 3 nRF sub-channels per Zigbee channel (±1MHz)
static const uint8_t CH_ZIGBEE[] = {
    4, 5, 6,       // ch11
    9, 10, 11,     // ch12
    14, 15, 16,    // ch13
    19, 20, 21,    // ch14
    24, 25, 26,    // ch15
    29, 30, 31,    // ch16
    34, 35, 36,    // ch17
    39, 40, 41,    // ch18
    44, 45, 46,    // ch19
    49, 50, 51,    // ch20
    54, 55, 56,    // ch21
    59, 60, 61,    // ch22
    64, 65, 66,    // ch23
    69, 70, 71,    // ch24
    74, 75, 76,    // ch25
    79, 80, 81     // ch26
};

// ── Channel list accessor ───────────────────────────────────────
const uint8_t* NrfJammer::_getChannelList(NrfJamMode mode, size_t& count) {
    switch (mode) {
        case NRF_JAM_WIFI:      count = sizeof(CH_WIFI);      return CH_WIFI;
        case NRF_JAM_BLE:       count = sizeof(CH_BLE);       return CH_BLE;
        case NRF_JAM_BLE_ADV:   count = sizeof(CH_BLE_ADV);   return CH_BLE_ADV;
        case NRF_JAM_BLUETOOTH: count = sizeof(CH_BLUETOOTH);  return CH_BLUETOOTH;
        case NRF_JAM_USB:       count = sizeof(CH_USB);       return CH_USB;
        case NRF_JAM_VIDEO:     count = sizeof(CH_VIDEO);     return CH_VIDEO;
        case NRF_JAM_RC:        count = sizeof(CH_RC);        return CH_RC;
        case NRF_JAM_ZIGBEE:    count = sizeof(CH_ZIGBEE);    return CH_ZIGBEE;
        case NRF_JAM_DRONE:     count = 0; return nullptr; // Full sweep random
        default:                count = 0; return nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════
// ═══════════════ CONSTRUCTOR / DESTRUCTOR ══════════════════════
// ═══════════════════════════════════════════════════════════════

NrfJammer::NrfJammer()
    : _hspi(nullptr)
    , _radio(nullptr)
    , _radioReady(false)
    , _running(false)
    , _currentMode(NRF_JAM_FULL)
    , _jamTaskHandle(nullptr)
    , _currentChannel(0)
    , _hopIndex(0)
    , _packetsTotal(0)
    , _startTimeMs(0)
{
    // Default configs: CW mode, PA MAX, 2Mbps, turbo (0ms dwell)
    //                                PA  DR  dwell  flood
    _configs[NRF_JAM_FULL]      = {3, 1, 0, 0};
    _configs[NRF_JAM_WIFI]      = {3, 1, 0, 0};
    _configs[NRF_JAM_BLE]       = {3, 1, 0, 0};
    _configs[NRF_JAM_BLE_ADV]   = {3, 1, 0, 0};
    _configs[NRF_JAM_BLUETOOTH] = {3, 1, 0, 0};
    _configs[NRF_JAM_USB]       = {3, 1, 0, 0};
    _configs[NRF_JAM_VIDEO]     = {3, 1, 0, 0};
    _configs[NRF_JAM_RC]        = {3, 1, 0, 0};
    _configs[NRF_JAM_ZIGBEE]    = {3, 1, 0, 0};
    _configs[NRF_JAM_DRONE]     = {3, 1, 0, 0};
}

NrfJammer::~NrfJammer() {
    stop();
    if (_radio)  { delete _radio; _radio = nullptr; }
    if (_hspi)   { _hspi->end(); delete _hspi; _hspi = nullptr; }
}

// ═══════════════════════════════════════════════════════════════
// ═══════════════ INITIALIZATION ═══════════════════════════════
// ═══════════════════════════════════════════════════════════════

bool NrfJammer::begin() {
    LOG_INFO("NRF", "Initializing nRF24L01+ on HSPI...");
    LOG_INFO("NRF", "Pins: CE=%d CSN=%d SCK=%d MISO=%d MOSI=%d",
             NRF_CE_PIN, NRF_CSN_PIN, NRF_SCK_PIN, NRF_MISO_PIN, NRF_MOSI_PIN);

    // Initialize HSPI bus with custom pins
    _hspi = new SPIClass(HSPI);
    _hspi->begin(NRF_SCK_PIN, NRF_MISO_PIN, NRF_MOSI_PIN, NRF_CSN_PIN);

    // Create RF24 instance on HSPI
    _radio = new RF24(NRF_CE_PIN, NRF_CSN_PIN, 8000000); // 8MHz SPI clock

    if (!_radio->begin(_hspi)) {
        LOG_ERROR("NRF", "nRF24L01+ not detected! Check wiring.");
        Serial.println("[NRF] ERROR: Radio not detected. Check wiring!");
        _radioReady = false;
        return false;
    }

    // Verify communication by checking if chip responds
    if (!_radio->isChipConnected()) {
        LOG_ERROR("NRF", "nRF24L01+ chip not responding");
        Serial.println("[NRF] ERROR: Chip not responding. Check connections!");
        _radioReady = false;
        return false;
    }

    // Basic configuration
    _radio->setPALevel(RF24_PA_MAX);
    _radio->setDataRate(RF24_2MBPS);
    _radio->setAutoAck(false);
    _radio->setRetries(0, 0);
    _radio->disableCRC();
    _radio->setPayloadSize(32);
    _radio->powerDown();  // Start powered down until jamming requested

    _radioReady = true;
    
    LOG_INFO("NRF", "nRF24L01+ initialized successfully (PA+LNA module)");
    Serial.println("[NRF] Radio initialized OK (E01-ML01DP5)");
    
    return true;
}

// ═══════════════════════════════════════════════════════════════
// ═══════════════ RF CONFIGURATION ═════════════════════════════
// ═══════════════════════════════════════════════════════════════

void NrfJammer::_applyConfig(const NrfJamConfig& cfg, bool flooding) {
    rf24_pa_dbm_e paLevels[] = {RF24_PA_MIN, RF24_PA_LOW, RF24_PA_HIGH, RF24_PA_MAX};
    rf24_datarate_e dataRates[] = {RF24_1MBPS, RF24_2MBPS, RF24_250KBPS};

    _radio->setPALevel(paLevels[cfg.paLevel & 3]);
    _radio->setDataRate(dataRates[cfg.dataRate <= 2 ? cfg.dataRate : 1]);
    _radio->setAutoAck(false);
    _radio->setRetries(0, 0);
    _radio->disableCRC();

    if (flooding) {
        _radio->setPayloadSize(32);
        _radio->setAddressWidth(3);
        uint8_t txAddr[] = {0xE7, 0xE7, 0xE7};
        _radio->openWritingPipe(txAddr);
        _radio->stopListening();
    }
}

void NrfJammer::_initCW(int channel) {
    _radio->powerUp();
    delay(5);  // Tpd2stby: power-down → standby settle
    _radio->setPALevel(RF24_PA_MAX);
    _radio->startConstCarrier(RF24_PA_MAX, channel);
    _radio->setAddressWidth(5);
    _radio->setPayloadSize(2);
    _radio->setDataRate(RF24_2MBPS);
}

void NrfJammer::_cwChannel(uint8_t ch, uint16_t dwellMs) {
    _radio->setChannel(ch);
    if (dwellMs == 0) return;
    if (dwellMs <= 5) {
        delayMicroseconds((uint32_t)dwellMs * 1000);
    } else {
        delay(dwellMs);
    }
}

void NrfJammer::_floodChannel(uint8_t ch, uint16_t dwellMs) {
    // CE LOW to safely change channel
    digitalWrite(NRF_CE_PIN, LOW);
    _radio->flush_tx();
    _radio->setChannel(ch);

    if (dwellMs == 0) {
        // Turbo: fill FIFO (3 packets) and fire
        _radio->writeFast(JAM_FLOOD_DATA, 32, true);
        _radio->writeFast(JAM_FLOOD_DATA, 32, true);
        _radio->writeFast(JAM_FLOOD_DATA, 32, true);
        delayMicroseconds(500);
        _packetsTotal += 3;
        return;
    }

    unsigned long startMs = millis();
    while ((millis() - startMs) < dwellMs) {
        if (_radio->writeFast(JAM_FLOOD_DATA, 32, true)) {
            _packetsTotal++;
        } else {
            delayMicroseconds(10);
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// ═══════════════ START / STOP ═════════════════════════════════
// ═══════════════════════════════════════════════════════════════

bool NrfJammer::start(NrfJamMode mode) {
    if (!_radioReady) {
        Serial.println("[NRF] ERROR: Radio not initialized");
        return false;
    }
    if (_running) {
        Serial.println("[NRF] Jammer already running. Use 'nrf stop' first.");
        return false;
    }

    _currentMode = mode;
    _hopIndex = 0;
    _currentChannel = 0;
    _packetsTotal = 0;
    _running = true;
    _startTimeMs = millis();

    LOG_INFO("NRF", "Starting jammer: mode=%s", getModeName(mode));
    Serial.printf("[NRF] Starting jammer: %s\n", getModeName(mode));

    NrfJamConfig& cfg = _configs[(uint8_t)mode];
    Serial.printf("[NRF] Config: PA=%d, Rate=%d, Dwell=%dms, Strategy=%s\n",
                  cfg.paLevel, cfg.dataRate, cfg.dwellTimeMs,
                  cfg.useFlooding ? "FLOOD" : "CW");

    // Create jamming task on Core 0 (WiFi/main is on Core 0, serial on Core 1)
    // Use Core 0 for max RF throughput since it has fewer ISR handlers
    BaseType_t result = xTaskCreatePinnedToCore(
        _jamTaskFunc,       // Task function
        "NrfJam",           // Name
        4096,               // Stack size
        this,               // Parameter (this pointer)
        2,                  // Priority (higher than serial commander)
        &_jamTaskHandle,    // Handle
        0                   // Core 0
    );

    if (result != pdPASS) {
        _running = false;
        LOG_ERROR("NRF", "Failed to create jammer task");
        Serial.println("[NRF] ERROR: Failed to create jammer task");
        return false;
    }

    Serial.println("[NRF] Jammer ACTIVE - type 'nrf stop' to stop");
    return true;
}

void NrfJammer::stop() {
    if (!_running) {
        Serial.println("[NRF] Jammer is not running");
        return;
    }

    _running = false;

    // Wait for task to finish gracefully
    if (_jamTaskHandle != nullptr) {
        for (int i = 0; i < 20 && _jamTaskHandle != nullptr; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // Fallback force-stop
        if (_jamTaskHandle != nullptr) {
            vTaskDelete(_jamTaskHandle);
            _jamTaskHandle = nullptr;
        }
    }

    // Power down radio
    if (_radio && _radioReady) {
        _radio->stopConstCarrier();
        _radio->flush_tx();
        _radio->powerDown();
    }

    uint32_t elapsed = (millis() - _startTimeMs) / 1000;
    LOG_INFO("NRF", "Jammer stopped after %d seconds", elapsed);
    Serial.printf("[NRF] Jammer STOPPED (ran %ds, ~%u packets)\n", elapsed, _packetsTotal);
}

// ═══════════════════════════════════════════════════════════════
// ═══════════════ JAMMING TASK ═════════════════════════════════
// ═══════════════════════════════════════════════════════════════

void NrfJammer::_jamTaskFunc(void* param) {
    NrfJammer* self = static_cast<NrfJammer*>(param);
    self->_jamLoop();
    self->_jamTaskHandle = nullptr;
    vTaskDelete(NULL);
}

void NrfJammer::_jamLoop() {
    NrfJamConfig& cfg = _configs[(uint8_t)_currentMode];
    bool flooding = cfg.useFlooding;

    // Initialize radio for selected strategy  
    if (flooding) {
        _applyConfig(cfg, true);
    } else {
        _initCW(_currentChannel);
    }

    LOG_DEBUG("NRF", "Jam loop started on core %d", xPortGetCoreID());

    uint32_t loopCount = 0;

    while (_running) {
        NrfJamConfig& cfg = _configs[(uint8_t)_currentMode];
        bool flood = cfg.useFlooding;
        uint16_t dwellMs = cfg.dwellTimeMs;

        switch (_currentMode) {
            case NRF_JAM_FULL:
            case NRF_JAM_DRONE: {
                // Full sweep 0-124
                if (flood) _floodChannel(_hopIndex, dwellMs);
                else       _cwChannel(_hopIndex, dwellMs);
                _currentChannel = _hopIndex;
                _hopIndex = (_hopIndex + 1) % 125;
                _packetsTotal++;
                break;
            }

            default: {
                // Preset channel list modes
                size_t count;
                const uint8_t* channels = _getChannelList(_currentMode, count);
                if (count > 0 && channels) {
                    uint8_t ch = channels[_hopIndex % count];
                    if (flood) _floodChannel(ch, dwellMs);
                    else       _cwChannel(ch, dwellMs);
                    _currentChannel = ch;
                    _hopIndex++;
                    if (_hopIndex >= (int)count) _hopIndex = 0;
                    _packetsTotal++;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
                break;
            }
        }

        // Let IDLE0 run periodically, otherwise task_wdt triggers on CPU0.
        // taskYIELD() alone is not enough when this task has higher priority.
        loopCount++;
        if (dwellMs == 0) {
            if ((loopCount % 64U) == 0U) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        } else if ((loopCount % 256U) == 0U) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    // Cleanup
    if (_radio) {
        _radio->stopConstCarrier();
        _radio->flush_tx();
        _radio->powerDown();
    }
}

// ═══════════════════════════════════════════════════════════════
// ═══════════════ STATUS & INFO ════════════════════════════════
// ═══════════════════════════════════════════════════════════════

void NrfJammer::printStatus() {
    Serial.println("\n========================================");
    Serial.println("nRF24L01+ Jammer Status");
    Serial.println("========================================");
    Serial.printf("Radio:      %s\n", _radioReady ? "READY" : "NOT DETECTED");
    Serial.printf("Jammer:     %s\n", _running ? "ACTIVE" : "STOPPED");
    
    if (_running) {
        uint32_t elapsed = (millis() - _startTimeMs) / 1000;
        Serial.printf("Mode:       %s\n", getModeName(_currentMode));
        Serial.printf("Channel:    %d (%d MHz)\n", _currentChannel, 2400 + _currentChannel);
        Serial.printf("Strategy:   %s\n", _configs[(uint8_t)_currentMode].useFlooding ? "Data Flooding" : "Constant Carrier (CW)");
        Serial.printf("Runtime:    %ds\n", elapsed);
        Serial.printf("Hops/Pkts:  ~%u\n", _packetsTotal);
    }
    
    if (_radioReady) {
        Serial.printf("PA Level:   %d (0=MIN, 3=MAX/+20dBm)\n", _configs[(uint8_t)_currentMode].paLevel);
        Serial.printf("Data Rate:  %s\n", 
            _configs[(uint8_t)_currentMode].dataRate == 0 ? "1 Mbps" :
            _configs[(uint8_t)_currentMode].dataRate == 1 ? "2 Mbps" : "250 Kbps");
    }
    Serial.println("========================================\n");
}

void NrfJammer::printModes() {
    Serial.println("\nAvailable Jamming Modes:");
    Serial.println("────────────────────────────────────────");
    for (int i = 0; i < NRF_JAM_MODE_COUNT; i++) {
        Serial.printf("  %-10s - %s\n", MODE_SHORT[i], MODE_NAMES[i]);
    }
    Serial.println("────────────────────────────────────────");
    Serial.println("Usage: nrf start <mode_name>");
    Serial.println("Example: nrf start wifi\n");
}

void NrfJammer::printConfig() {
    NrfJamConfig& cfg = _configs[(uint8_t)_currentMode];
    const char* paLabels[] = {"MIN (-18dBm)", "LOW (-12dBm)", "HIGH (-6dBm)", "MAX (0/+20dBm)"};
    const char* drLabels[] = {"1 Mbps", "2 Mbps", "250 Kbps"};

    Serial.printf("\nConfig for mode: %s\n", getModeName(_currentMode));
    Serial.println("────────────────────────────────────────");
    Serial.printf("  PA Level:   %d = %s\n", cfg.paLevel, paLabels[cfg.paLevel & 3]);
    Serial.printf("  Data Rate:  %d = %s\n", cfg.dataRate, drLabels[cfg.dataRate <= 2 ? cfg.dataRate : 1]);
    Serial.printf("  Dwell Time: %d ms%s\n", cfg.dwellTimeMs, cfg.dwellTimeMs == 0 ? " (turbo)" : "");
    Serial.printf("  Strategy:   %s\n", cfg.useFlooding ? "Data Flooding" : "Constant Carrier (CW)");
    Serial.println("────────────────────────────────────────");
    Serial.println("Modify: nrf set pa|rate|dwell|flood <value>\n");
}

// ═══════════════════════════════════════════════════════════════
// ═══════════════ CONFIG SETTERS ═══════════════════════════════
// ═══════════════════════════════════════════════════════════════

void NrfJammer::setPaLevel(uint8_t level) {
    if (level > 3) level = 3;
    _configs[(uint8_t)_currentMode].paLevel = level;
    const char* labels[] = {"MIN (-18dBm)", "LOW (-12dBm)", "HIGH (-6dBm)", "MAX (0/+20dBm)"};
    Serial.printf("[NRF] PA Level set to %d = %s\n", level, labels[level]);
}

void NrfJammer::setDataRate(uint8_t rate) {
    if (rate > 2) rate = 1;
    _configs[(uint8_t)_currentMode].dataRate = rate;
    const char* labels[] = {"1 Mbps", "2 Mbps", "250 Kbps"};
    Serial.printf("[NRF] Data Rate set to %d = %s\n", rate, labels[rate]);
}

void NrfJammer::setDwellTime(uint16_t ms) {
    if (ms > 200) ms = 200;
    _configs[(uint8_t)_currentMode].dwellTimeMs = ms;
    Serial.printf("[NRF] Dwell Time set to %d ms%s\n", ms, ms == 0 ? " (turbo)" : "");
}

void NrfJammer::setFlooding(uint8_t flood) {
    flood = flood ? 1 : 0;
    _configs[(uint8_t)_currentMode].useFlooding = flood;
    Serial.printf("[NRF] Strategy set to: %s\n", flood ? "Data Flooding" : "Constant Carrier (CW)");
    if (_running) {
        Serial.println("[NRF] Note: restart jammer for changes to take effect");
    }
}

// ═══════════════════════════════════════════════════════════════
// ═══════════════ STATIC HELPERS ═══════════════════════════════
// ═══════════════════════════════════════════════════════════════

NrfJamMode NrfJammer::parseModeByName(const String& name) {
    String lower = name;
    lower.trim();
    lower.toLowerCase();

    for (int i = 0; i < NRF_JAM_MODE_COUNT; i++) {
        if (lower == MODE_SHORT[i]) {
            return (NrfJamMode)i;
        }
    }
    
    // Partial match fallback
    if (lower.startsWith("ble_a") || lower == "adv")       return NRF_JAM_BLE_ADV;
    if (lower.startsWith("blue") || lower == "bluetooth")   return NRF_JAM_BLUETOOTH;
    if (lower == "all" || lower == "spectrum")               return NRF_JAM_FULL;
    
    return NRF_JAM_FULL; // Default
}

const char* NrfJammer::getModeName(NrfJamMode mode) {
    if (mode < NRF_JAM_MODE_COUNT) return MODE_NAMES[(uint8_t)mode];
    return "Unknown";
}
