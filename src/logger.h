#pragma once

#include <Arduino.h>
#include "config.h"
/**
 * ╔════════════════════════════════════════════════════════════╗
 * ║                  Lib by Senape3000    - 2026               ║
 * ╚════════════════════════════════════════════════════════════╝
 */



/**
 * @file logger.h
 * @brief Structured logging system for ESP32
 * 
 * ADVANTAGES over Serial.println():
 * - Log levels (DEBUG, INFO, WARN, ERROR, CRITICAL)
 * - Automatic timestamps
 * - ANSI colors for Serial Monitor
 * - Module prefixes [NFC], [WIFI], [I2C]
 * - Fully disableable in production (0 overhead)
 * - Compile-time filtering by level
 * - Printf-style formatting
 * 
 * USAGE:
 *   LOG_INFO("NFC", "Tag detected: %s", uid);
 *   LOG_ERROR("I2C", "Bus timeout after %d ms", timeout);
 *   LOG_DEBUG("WIFI", "RSSI: %d dBm", WiFi.RSSI());
 */

// ========================================
// CONFIGURATION
// ========================================

/**
 * @brief Minimum log level to print
 * 
 * Set in platformio.ini with build_flags:
 *   -DLOG_LEVEL=5  (development)
 *   -DLOG_LEVEL=4   (production)
 *   -DLOG_LEVEL=0   (release - disable all logging)
 *   (0=NONE, 1=CRIT, 2=ERR, 3=WARN, 4=INFO, 5=DEBUG, 6=VERBOSE)
 */

#ifndef LOG_LEVEL
    #define LOG_LEVEL 3 
    #warning "LOG LEVEL NOT DEFINED, USING 3"
#endif

/**
 * @brief Enable ANSI colors in Serial Monitor
 * Default: enabled in DEBUG, disabled in production
 */
#ifndef LOG_COLORS_ENABLED
    #ifdef DEBUG
        #define LOG_COLORS_ENABLED 1
    #else
        #define LOG_COLORS_ENABLED 0
    #endif
#endif

/**
 * @brief Enable timestamps in log messages
 */
#ifndef LOG_TIMESTAMP_ENABLED
    #define LOG_TIMESTAMP_ENABLED 1
#endif

/**
 * @brief Buffer size for message formatting
 */
#ifndef LOG_BUFFER_SIZE
    #define LOG_BUFFER_SIZE 256
#endif


// ========================================
// LOG LEVELS
// ========================================

#define LOG_LEVEL_NONE     0
#define LOG_LEVEL_CRITICAL 1
#define LOG_LEVEL_ERROR    2
#define LOG_LEVEL_WARN     3
#define LOG_LEVEL_INFO     4
#define LOG_LEVEL_DEBUG    5
#define LOG_LEVEL_VERBOSE  6
typedef int LogLevel;

// ========================================
// ANSI COLORS
// ========================================

#if LOG_COLORS_ENABLED
    #define COLOR_RESET   "\033[0m"
    #define COLOR_RED     "\033[31m"
    #define COLOR_GREEN   "\033[32m"
    #define COLOR_YELLOW  "\033[33m"
    #define COLOR_BLUE    "\033[34m"
    #define COLOR_MAGENTA "\033[35m"
    #define COLOR_CYAN    "\033[36m"
    #define COLOR_WHITE   "\033[37m"
    #define COLOR_GRAY    "\033[90m"

    // Bold colors
    #define COLOR_BOLD_RED    "\033[1;31m"
    #define COLOR_BOLD_YELLOW "\033[1;33m"
#else
    #define COLOR_RESET   ""
    #define COLOR_RED     ""
    #define COLOR_GREEN   ""
    #define COLOR_YELLOW  ""
    #define COLOR_BLUE    ""
    #define COLOR_MAGENTA ""
    #define COLOR_CYAN    ""
    #define COLOR_WHITE   ""
    #define COLOR_GRAY    ""
    #define COLOR_BOLD_RED    ""
    #define COLOR_BOLD_YELLOW ""
#endif


// ========================================
// PUBLIC MACROS
// ========================================

/**
 * @brief Log CRITICAL - System non-operational
 * @param module Module name (e.g., "NFC", "I2C")
 * @param format Printf-style format string
 * @param ... Variadic arguments
 * 
 * Example: LOG_CRITICAL("SYSTEM", "Out of memory! Free: %d", ESP.getFreeHeap());
 */
#if LOG_LEVEL >= LOG_LEVEL_CRITICAL
    #define LOG_CRITICAL(module, format, ...) \
        Logger::log(LOG_LEVEL_CRITICAL, module, format, ##__VA_ARGS__)
#else
    #define LOG_CRITICAL(module, format, ...) ((void)0)
#endif

/**
 * @brief Log ERROR - Operation failed but system operational
 * 
 * Example: LOG_ERROR("NFC", "PN532 timeout after %d ms", timeout);
 */
#if LOG_LEVEL >= LOG_LEVEL_ERROR
    #define LOG_ERROR(module, format, ...) \
        Logger::log(LOG_LEVEL_ERROR, module, format, ##__VA_ARGS__)
#else
    #define LOG_ERROR(module, format, ...) ((void)0)
#endif

/**
 * @brief Log WARN - Non-blocking anomaly
 * 
 * Example: LOG_WARN("WIFI", "Weak signal: %d dBm", rssi);
 */
#if LOG_LEVEL >= LOG_LEVEL_WARN
    #define LOG_WARN(module, format, ...) \
        Logger::log(LOG_LEVEL_WARN, module, format, ##__VA_ARGS__)
#else
    #define LOG_WARN(module, format, ...) ((void)0)
#endif

/**
 * @brief Log INFO - Normal events (default production)
 * 
 * Example: LOG_INFO("SETUP", "WiFi connected to %s", WiFi.SSID().c_str());
 */
#if LOG_LEVEL >= LOG_LEVEL_INFO
    #define LOG_INFO(module, format, ...) \
        Logger::log(LOG_LEVEL_INFO, module, format, ##__VA_ARGS__)
#else
    #define LOG_INFO(module, format, ...) ((void)0)
#endif

/**
 * @brief Log DEBUG - Detailed information (development only)
 * 
 * Example: LOG_DEBUG("I2C", "Read block %d: 0x%02X", blockNum, data);
 */
#if LOG_LEVEL >= LOG_LEVEL_DEBUG
    #define LOG_DEBUG(module, format, ...) \
        Logger::log(LOG_LEVEL_DEBUG, module, format, ##__VA_ARGS__)
#else
    #define LOG_DEBUG(module, format, ...) ((void)0)
#endif

/**
 * @brief Log VERBOSE - Complete data dumps (significant overhead)
 * 
 * Example: LOG_VERBOSE("NFC", "Dump: %s", hexDump);
 */
#if LOG_LEVEL >= LOG_LEVEL_VERBOSE
    #define LOG_VERBOSE(module, format, ...) \
        Logger::log(LOG_LEVEL_VERBOSE, module, format, ##__VA_ARGS__)
#else
    #define LOG_VERBOSE(module, format, ...) ((void)0)
#endif


// ========================================
// SPECIAL MACROS
// ========================================

/**
 * @brief HEX dump of a buffer (only in DEBUG/VERBOSE)
 * 
 * Example:
 *   uint8_t data[16];
 *   LOG_HEX_DUMP("NFC", data, 16, "Tag UID");
 */
#if LOG_LEVEL >= LOG_LEVEL_DEBUG
    #define LOG_HEX_DUMP(module, data, len, label) \
        Logger::hexDump(module, (const uint8_t*)data, len, label)
#else
    #define LOG_HEX_DUMP(module, data, len, label) ((void)0)
#endif

/**
 * @brief Assert with automatic logging (crash if condition false)
 * 
 * Example:
 *   LOG_ASSERT(nfc != nullptr, "NFC", "NFC object is null");
 */
#define LOG_ASSERT(condition, module, format, ...) \
    if (!(condition)) { \
        LOG_CRITICAL(module, "ASSERTION FAILED: " format, ##__VA_ARGS__); \
        while(1) { delay(1000); } \
    }


// ========================================
// LOGGER CLASS
// ========================================

class Logger {
public:
    /**
     * @brief Internal function to print log messages
     * DO NOT call directly, use LOG_* macros instead
     */
    static void log(LogLevel level, const char* module, const char* format, ...) {
        // Buffer for formatted message
        char buffer[LOG_BUFFER_SIZE];

        // Format message with variadic args
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, LOG_BUFFER_SIZE, format, args);
        va_end(args);

        // Select color and prefix for level
        const char* color = COLOR_RESET;
        const char* levelStr = "???";

        switch (level) {
            case LOG_LEVEL_CRITICAL:
                color = COLOR_BOLD_RED;
                levelStr = "CRIT";
                break;
            case LOG_LEVEL_ERROR:
                color = COLOR_RED;
                levelStr = "ERR ";
                break;
            case LOG_LEVEL_WARN:
                color = COLOR_BOLD_YELLOW;
                levelStr = "WARN";
                break;
            case LOG_LEVEL_INFO:
                color = COLOR_GREEN;
                levelStr = "INFO";
                break;
            case LOG_LEVEL_DEBUG:
                color = COLOR_CYAN;
                levelStr = "DBG ";
                break;
            case LOG_LEVEL_VERBOSE:
                color = COLOR_GRAY;
                levelStr = "VERB";
                break;
            default:
                break;
        }

        // Build output
        #if LOG_TIMESTAMP_ENABLED
            uint32_t timestamp = millis();
            uint32_t seconds = timestamp / 1000;
            uint32_t millis_part = timestamp % 1000;

            Serial.printf("%s[%5lu.%03lu] [%s] [%-8s] %s%s\n",
                          color,
                          seconds,
                          millis_part,
                          levelStr,
                          module,
                          buffer,
                          COLOR_RESET);
        #else
            Serial.printf("%s[%s] [%-8s] %s%s\n",
                          color,
                          levelStr,
                          module,
                          buffer,
                          COLOR_RESET);
        #endif
    }

    /**
     * @brief Hex dump of a byte buffer
     */
    static void hexDump(const char* module, const uint8_t* data, size_t len, const char* label) {
        LOG_DEBUG(module, "%s (%d bytes):", label, len);

        // Print 16 bytes per line
        for (size_t i = 0; i < len; i += 16) {
            char line[80];
            int offset = 0;

            // Offset address
            offset += snprintf(line + offset, sizeof(line) - offset, "  %04X: ", i);

            // Hex values
            for (size_t j = 0; j < 16; j++) {
                if (i + j < len) {
                    offset += snprintf(line + offset, sizeof(line) - offset, "%02X ", data[i + j]);
                } else {
                    offset += snprintf(line + offset, sizeof(line) - offset, "   ");
                }
            }

            // ASCII representation
            offset += snprintf(line + offset, sizeof(line) - offset, " | ");
            for (size_t j = 0; j < 16 && i + j < len; j++) {
                char c = data[i + j];
                offset += snprintf(line + offset, sizeof(line) - offset, "%c", 
                                   (c >= 32 && c <= 126) ? c : '.');
            }

            Serial.println(line);
        }
    }

    /**
     * @brief Initialize logger (optional, call in setup())
     */
    static void begin() {
        Serial.println();
        LOG_INFO("LOGGER", "Logging system initialized");
        LOG_INFO("LOGGER", "Level: %s", getLevelName((LogLevel)LOG_LEVEL));
        LOG_INFO("LOGGER", "Colors: %s", LOG_COLORS_ENABLED ? "enabled" : "disabled");
        LOG_INFO("LOGGER", "Timestamp: %s", LOG_TIMESTAMP_ENABLED ? "enabled" : "disabled");
        Serial.println();
    }

    static const char* getLevelName(int level) {
    switch (level) {
        case LOG_LEVEL_NONE: return "NONE";
        case LOG_LEVEL_CRITICAL: return "CRITICAL";
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_WARN: return "WARN";
        case LOG_LEVEL_INFO: return "INFO";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_VERBOSE: return "VERBOSE";
        default: return "UNKNOWN";
    }
}

private:

};
