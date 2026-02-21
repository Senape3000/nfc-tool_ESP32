/**
 * @file nrf_jammer.h
 * @brief 2.4 GHz jammer via nRF24L01+ (E01-ML01DP5 PA+LNA module)
 *
 * Serial-only interface for ESP32 NFC Tool project.
 * Supports Constant Carrier (CW) and Data Flooding strategies
 * across 10 preset jamming modes.
 *
 * Hardware: E01-ML01DP5 (nRF24L01+ PA+LNA, up to +20 dBm)
 * Interface: SPI (HSPI bus on ESP32)
 */
#ifndef __NRF_JAMMER_H
#define __NRF_JAMMER_H

#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>
#include "config.h"
#include "logger.h"

// ── Jamming Mode Presets ────────────────────────────────────────
enum NrfJamMode : uint8_t {
    NRF_JAM_FULL = 0,      // All channels 0-124
    NRF_JAM_WIFI = 1,      // WiFi ch 1, 6, 11 bandwidth
    NRF_JAM_BLE = 2,       // BLE data channels
    NRF_JAM_BLE_ADV = 3,   // BLE advertising channels (37,38,39)
    NRF_JAM_BLUETOOTH = 4, // Classic Bluetooth FHSS
    NRF_JAM_USB = 5,       // USB wireless dongles
    NRF_JAM_VIDEO = 6,     // Video streaming (FPV, baby monitors)
    NRF_JAM_RC = 7,        // RC controllers
    NRF_JAM_ZIGBEE = 8,    // Zigbee channels 11-26
    NRF_JAM_DRONE = 9,     // Drone FHSS protocols
    NRF_JAM_MODE_COUNT = 10
};

// ── Per-mode configuration ──────────────────────────────────────
struct NrfJamConfig {
    uint8_t paLevel;      // 0-3 (MIN..MAX, PA+LNA: 0dBm → +20dBm)
    uint8_t dataRate;     // 0=1Mbps, 1=2Mbps, 2=250Kbps
    uint16_t dwellTimeMs; // Time on each channel (0=turbo)
    uint8_t useFlooding;  // 0=Constant Carrier, 1=Data Flooding
};

/**
 * @brief nRF24L01+ Jammer controller (serial-only, FreeRTOS task)
 * 
 * Serial commands (via SerialCommander "nrf" category):
 *   nrf status         - Show radio status and jamming state
 *   nrf start [mode]   - Start jammer (full/wifi/ble/ble_adv/bt/usb/video/rc/zigbee/drone)
 *   nrf stop           - Stop jammer
 *   nrf modes          - List available modes
 *   nrf config         - Show current mode config
 *   nrf set pa <0-3>   - Set PA level (0=MIN, 3=MAX +20dBm)
 *   nrf set rate <0-2> - Set data rate (0=1M, 1=2M, 2=250K)
 *   nrf set dwell <ms> - Set per-channel dwell time (0=turbo)
 *   nrf set flood <0|1>- Set strategy (0=CW carrier, 1=Data flooding)
 */
class NrfJammer {
public:
    NrfJammer();
    ~NrfJammer();

    /**
     * @brief Initialize the nRF24L01+ radio on HSPI
     * @return true if radio detected and initialized
     */
    bool begin();

    /** @brief Check if radio was initialized successfully */
    bool isRadioReady() const { return _radioReady; }

    /** @brief Check if jammer is currently running */
    bool isRunning() const { return _running; }

    /** @brief Get current jamming mode */
    NrfJamMode currentMode() const { return _currentMode; }

    /**
     * @brief Start jamming with specified mode
     * @param mode Jamming mode preset
     * @return true if started successfully
     */
    bool start(NrfJamMode mode = NRF_JAM_FULL);

    /** @brief Stop jamming and power down radio */
    void stop();

    /** @brief Print radio and jammer status to Serial */
    void printStatus();

    /** @brief Print available modes to Serial */
    void printModes();

    /** @brief Print current mode config to Serial */
    void printConfig();

    /** @brief Set PA level for current mode (0-3) */
    void setPaLevel(uint8_t level);

    /** @brief Set data rate for current mode (0=1M, 1=2M, 2=250K) */
    void setDataRate(uint8_t rate);

    /** @brief Set dwell time for current mode (ms, 0=turbo, max 200) */
    void setDwellTime(uint16_t ms);

    /** @brief Set jamming strategy (0=CW, 1=Flooding) */
    void setFlooding(uint8_t flood);

    /**
     * @brief Parse mode name string to enum
     * @param name Mode name (case insensitive)
     * @return Mode enum or NRF_JAM_FULL as default
     */
    static NrfJamMode parseModeByName(const String& name);

    /** @brief Get mode display name from enum */
    static const char* getModeName(NrfJamMode mode);

private:
    SPIClass* _hspi;
    RF24* _radio;
    bool _radioReady;
    volatile bool _running;
    NrfJamMode _currentMode;
    TaskHandle_t _jamTaskHandle;
    volatile int _currentChannel;
    int _hopIndex;
    volatile uint32_t _packetsTotal;
    volatile uint32_t _startTimeMs;

    // Per-mode configs
    NrfJamConfig _configs[NRF_JAM_MODE_COUNT];

    // Internal methods
    void _applyConfig(const NrfJamConfig& cfg, bool flooding);
    void _initCW(int channel);
    void _cwChannel(uint8_t ch, uint16_t dwellMs);
    void _floodChannel(uint8_t ch, uint16_t dwellMs);

    // Channel list accessor
    static const uint8_t* _getChannelList(NrfJamMode mode, size_t& count);

    // FreeRTOS jamming task
    static void _jamTaskFunc(void* param);
    void _jamLoop();
};

#endif // __NRF_JAMMER_H
