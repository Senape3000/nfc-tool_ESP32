#pragma once

/**
 * @file config.h
 * @brief ESP32 NFC Tool - Configuration Header
 * 
 * This file contains all compile-time configuration constants.
 * In the future, runtime configuration will be loaded from flash storage.
 * 
 * Categories:
 * - Hardware pins (I2C, NFC, LED)
 * - WiFi settings
 * - NFC operation timeouts
 * - System parameters
 * - Debug flags
 * 
 * @note For production, change default passwords and disable DEBUG_SKIP_AUTH
 */

#include <Arduino.h>

// ========================================
// HARDWARE VERSION
// ========================================

#define HARDWARE_VERSION "1.0"
#define FIRMWARE_VERSION "1.0.0"
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__


// ========================================
// PIN CONFIGURATION
// ========================================

/**
 * @brief I2C Bus Configuration
 * ESP32 default I2C pins: SDA=21, SCL=22
 */
#define I2C_SDA_PIN         21      ///< I2C Data pin
#define I2C_SCL_PIN         22      ///< I2C Clock pin
#define I2C_FREQUENCY       100000  ///< I2C bus speed (Hz) - 100kHz for PN532 stability

/**
 * @brief PN532 NFC Module Pins
 */
#define PN532_IRQ_PIN       18      ///< PN532 interrupt pin (optional, not currently used)
#define PN532_RESET_PIN     19      ///< PN532 hardware reset pin

/**
 * @brief LED Status Indicator
 */
#define LED_PIN             2       ///< Built-in LED on most ESP32 boards
#define LED_ACTIVE_LOW      false   ///< Set true if LED is active LOW (cathode to GPIO)


// ========================================
// WiFi CONFIGURATION
// ========================================

/**
 * @brief WiFi Persistent Storage
 */
#define WIFI_DB_PATH        "/wifi_db.json"     ///< WiFi credentials storage file

/**
 * @brief WiFi Connection Parameters
 */
#define WIFI_CONNECT_TIMEOUT_MS     20000       ///< Timeout before fallback to AP mode (20s)
#define WIFI_RECONNECT_ATTEMPTS     3           ///< Number of reconnection attempts
#define WIFI_RECONNECT_DELAY_MS     1000        ///< Delay between reconnect attempts

/**
 * @brief Access Point (AP) Mode Settings
 * Used when WiFi connection fails or on first boot
 */
#define AP_SSID             "ESP32-NFCTool"     ///< AP SSID (visible network name)
#define AP_PASSWORD         "nfctool123"        ///< AP password (min 8 characters)
#define AP_CHANNEL          1                   ///< WiFi channel (1-13)
#define AP_MAX_CONNECTIONS  4                   ///< Maximum simultaneous connections
#define AP_HIDDEN           false               ///< Hide SSID broadcast

/**
 * @brief mDNS (Multicast DNS) Configuration
 * Access device via http://nfctool.local instead of IP
 */
#define MDNS_HOSTNAME       "nfctool"           ///< mDNS hostname
#define MDNS_ENABLED        true                ///< Enable mDNS discovery


// ========================================
// NFC CONFIGURATION
// ========================================

/**
 * @brief SRIX4K Specific Settings
 */
#define SRIX_TAG_TIMEOUT_MS         100         ///< Timeout per tag detection attempt
#define SRIX_MAX_RETRY_ATTEMPTS     5           ///< Max retry attempts for SRIX operations
#define SRIX_EEPROM_WRITE_DELAY_MS  15          ///< Delay after EEPROM write (chip spec)

/**
 * @brief Mifare Classic Settings
 */
#define MIFARE_AUTH_TIMEOUT_MS      1000        ///< Authentication timeout
#define MIFARE_SECTOR_BLOCKS        4           ///< Blocks per sector (1K cards)

/**
 * @brief Generic NFC Operation Timeouts
 */
#define NFC_DETECT_TIMEOUT_MS       2000        ///< Tag presence detection timeout
#define NFC_READ_TIMEOUT_MS         5000        ///< Full tag read timeout
#define NFC_WRITE_TIMEOUT_MS        10000       ///< Full tag write timeout
#define NFC_POLL_INTERVAL_MS        200         ///< Polling interval for tag detection


// ========================================
// WEB SERVER CONFIGURATION
// ========================================

/**
 * @brief Web Server Settings
 */
#define WEB_SERVER_PORT     80                  ///< HTTP port (80 = default)
#define WEB_MAX_CLIENTS     2                   ///< Maximum simultaneous web connections

/**
 * @brief Web Authentication
 * @warning CHANGE THESE DEFAULT CREDENTIALS IN PRODUCTION!
 */
#ifndef WEB_USERNAME
    #define WEB_USERNAME    "admin"             ///< Default web interface username
#endif

#ifndef WEB_PASSWORD
    #define WEB_PASSWORD    "admin"             ///< Default web interface password
#endif

/**
 * @brief Web Session Settings
 */
#define WEB_SESSION_TIMEOUT_MS      3600000     ///< Session timeout (1 hour)
#define WEB_MAX_SESSIONS            2           ///< Maximum active sessions


// ========================================
// FILESYSTEM CONFIGURATION
// ========================================

/**
 * @brief LittleFS Settings
 */
#define LITTLEFS_FORMAT_ON_FAIL     false       ///< Auto-format on mount failure
#define LITTLEFS_MAX_OPEN_FILES     5           ///< Max simultaneously open files

/**
 * @brief NFC Data Storage Paths
 */
#define NFC_DUMP_ROOT_FOLDER        "/DUMPS"            ///< Root folder for NFC dumps
#define NFC_SRIX_DUMP_FOLDER        "/DUMPS/SRIX/"       ///< SRIX4K dumps
#define NFC_MIFARE_DUMP_FOLDER      "/DUMPS/MIFARE/"     ///< Mifare Classic dumps
#define NFC_KEYS_FILE               "/mifare_keys.json" ///< Mifare keys database
#define NFC_MAX_DUMP_FILES          100                 ///< Maximum dump files before cleanup


// ========================================
// SYSTEM CONFIGURATION
// ========================================

/**
 * @brief Serial Communication
 */
#define SERIAL_BAUD_RATE    115200              ///< Serial baud rate
#define SERIAL_RX_BUFFER    256                 ///< Serial RX buffer size
#define SERIAL_TX_BUFFER    256                 ///< Serial TX buffer size

/**
 * @brief Boot & Timing
 */
#define BOOT_DELAY_MS       100                 ///< Delay after each boot stage
#define WATCHDOG_TIMEOUT_S  60                  ///< Watchdog timeout (0 = disabled)

/**
 * @brief Memory Management
 */
#define HEAP_WARNING_THRESHOLD      10000       ///< Warn if free heap below this (bytes)
#define HEAP_CRITICAL_THRESHOLD     5000        ///< Critical heap level (bytes)

/**
 * @brief Task Configuration
 */
#define SERIAL_TASK_STACK_SIZE      4096        ///< Serial command task stack (bytes)
#define SERIAL_TASK_PRIORITY        1           ///< Serial task priority (0-24)
#define SERIAL_TASK_CORE            1           ///< Serial task CPU core (0 or 1)


// ========================================
// DEBUG & DEVELOPMENT
// ========================================

/**
 * @brief Debug Flags
 * @warning Disable all debug flags in production builds!
 */

#ifndef DEBUG_SKIP_AUTH
    #define DEBUG_SKIP_AUTH         false       ///< Skip web authentication (DANGEROUS!)
#endif

#ifndef DEBUG_VERBOSE_NFC
    #define DEBUG_VERBOSE_NFC       false       ///< Enable verbose NFC debug output
#endif

#ifndef DEBUG_DUMP_I2C
    #define DEBUG_DUMP_I2C          false       ///< Dump all I2C transactions
#endif

#ifndef DEBUG_SIMULATE_TAGS
    #define DEBUG_SIMULATE_TAGS     false       ///< Simulate tag presence (testing)
#endif


// ========================================
// VALIDATION & SAFETY CHECKS
// ========================================

// Compile-time validation of critical settings
#if WEB_SERVER_PORT < 1 || WEB_SERVER_PORT > 65535
    #error "WEB_SERVER_PORT must be between 1 and 65535"
#endif

#if SERIAL_BAUD_RATE < 9600
    #error "SERIAL_BAUD_RATE too low (minimum 9600)"
#endif

#if I2C_FREQUENCY < 100000 || I2C_FREQUENCY > 400000
    #warning "I2C_FREQUENCY outside recommended range (100-400 kHz)"
#endif

#if DEBUG_SKIP_AUTH == true && !defined(DEBUG)
    #warning "⚠️   DEBUG_SKIP_AUTH enabled in production build!   ⚠️"
#endif


// ========================================
// BACKWARD COMPATIBILITY ALIASES
// ========================================
// These aliases maintain compatibility with existing code
// while transitioning to new naming conventions

#define sda_pin             I2C_SDA_PIN         ///< @deprecated Use I2C_SDA_PIN
#define scl_pin             I2C_SCL_PIN         ///< @deprecated Use I2C_SCL_PIN
#define PN532_IRQ           PN532_IRQ_PIN       ///< @deprecated Use PN532_IRQ_PIN
#define PN532_RF_REST       PN532_RESET_PIN     ///< @deprecated Use PN532_RESET_PIN
#define WIFI_TIMEOUT_MS     WIFI_CONNECT_TIMEOUT_MS  ///< @deprecated Use WIFI_CONNECT_TIMEOUT_MS
#define AP_PASS             AP_PASSWORD         ///< @deprecated Use AP_PASSWORD
#define TAG_TIMEOUT_MS      SRIX_TAG_TIMEOUT_MS ///< @deprecated Use SRIX_TAG_TIMEOUT_MS
#define TAG_MAX_ATTEMPTS    SRIX_MAX_RETRY_ATTEMPTS ///< @deprecated
#define SERIAL_BAUD         SERIAL_BAUD_RATE    ///< @deprecated Use SERIAL_BAUD_RATE
#define WEB_USER            WEB_USERNAME        ///< @deprecated Use WEB_USERNAME
#define WEB_PASS            WEB_PASSWORD        ///< @deprecated Use WEB_PASSWORD
#define NFC_DUMP_FOLDER     NFC_DUMP_ROOT_FOLDER ///< @deprecated Use NFC_DUMP_ROOT_FOLDER

// ========================================
// HELPER MACROS
// ========================================

/**
 * @brief Convert milliseconds to seconds
 */
#define MS_TO_SEC(ms)       ((ms) / 1000)

/**
 * @brief Convert seconds to milliseconds
 */
#define SEC_TO_MS(sec)      ((sec) * 1000)

/**
 * @brief Check if debug mode is enabled
 */
#ifdef DEBUG
    #define IS_DEBUG_BUILD  true
#else
    #define IS_DEBUG_BUILD  false
#endif


// ========================================
// CONFIGURATION SUMMARY (Compile-time info)
// ========================================

#ifdef DEBUG
    #pragma message "=== ESP32 NFC Tool Configuration ==="
    #pragma message "Firmware: " FIRMWARE_VERSION
    #pragma message "Build: " __DATE__ " " __TIME__
    #if DEBUG_SKIP_AUTH
        #pragma message "⚠️  WARNING: Authentication disabled!"
    #endif
    #if DEBUG_VERBOSE_NFC
        #pragma message "ℹ️  Verbose NFC debugging enabled"
    #endif
#endif