#pragma once

#include <Arduino.h>
#include "modules/wifi/wifi_manager.h"
#include "modules/rfid/nfc_manager.h"
#include "config.h"
#include "logger.h"

/**
 * @brief Command-line interface handler for serial communication
 * 
 * Provides a text-based command interface via Serial for controlling
 * WiFi, NFC operations, and system functions. Designed to run in a
 * dedicated FreeRTOS task to avoid blocking main operations.
 * 
 * Command Structure:
 * - Format: <category> <action> [arguments]
 * - Categories: wifi, nfc, system/sys, help
 * - Examples: "wifi status", "nfc read_srix", "system info"
 * 
 * Implementation Notes:
 * - Commands are case-insensitive
 * - Can be temporarily disabled during long operations (e.g., WiFi scan)
 * - Uses references to WiFiManager and NFCManager (not owned)
 */
class SerialCommander {
public:
    /**
     * @brief Construct SerialCommander with manager references
     * @param wifi Reference to WiFiManager instance
     * @param nfc Reference to NFCManager instance
     */
    SerialCommander(WiFiManager& wifi, NFCManager& nfc);

    /**
     * @brief Process incoming serial commands
     * 
     * Must be called periodically (typically in a FreeRTOS task).
     * Checks for available serial data, parses commands, and
     * dispatches to appropriate handlers.
     * 
     * @note Non-blocking: returns immediately if no data available
     */
    void handleCommands();

    /**
     * @brief Enable command processing
     */
    void enable() { _enabled = true; }

    /**
     * @brief Disable command processing
     * 
     * Useful during operations that require exclusive serial access
     * (e.g., WiFi setup prompts). Calls to handleCommands() will
     * return immediately when disabled.
     */
    void disable() { _enabled = false; }

    /**
     * @brief Check if commander is enabled
     * @return true if command processing is active
     */
    bool isEnabled() const { return _enabled; }

private:
    // ============================================
    // CONSTANTS
    // ============================================
    
    // Command parsing offsets
    static constexpr size_t CMD_OFFSET_SAVE = 5;      // Length of "save "
    static constexpr size_t CMD_OFFSET_LOAD = 5;      // Length of "load "
    static constexpr size_t CMD_OFFSET_WAIT = 5;      // Length of "wait "
    static constexpr size_t CMD_OFFSET_ADD = 4;       // Length of "add "
    
    // File extension lengths
    static constexpr size_t EXT_LEN_SRIX = 5;         // ".srix"
    static constexpr size_t EXT_LEN_MFC = 4;          // ".mfc"
    
    // Display formatting
    static constexpr size_t HEX_DUMP_PREVIEW_BYTES = 64;   // Show first 64 bytes
    static constexpr size_t HEX_DUMP_BYTES_PER_LINE = 16;  // 16 bytes per line
    
    // Timeouts
    static constexpr uint32_t DEFAULT_WAIT_TAG_TIMEOUT_MS = 5000;    // 5 seconds
    static constexpr uint32_t MAX_WAIT_TAG_TIMEOUT_SEC = 60;         // 60 seconds max
    static constexpr uint32_t DEFAULT_MIFARE_READ_TIMEOUT_SEC = 10;  // 10 seconds
    static constexpr uint32_t DEFAULT_MIFARE_UID_TIMEOUT_SEC = 5;    // 5 seconds
    static constexpr uint32_t DEFAULT_MIFARE_WRITE_TIMEOUT_SEC = 20; // 20 seconds
    
    // System operation delays
    static constexpr uint32_t RESTART_DELAY_MS = 2000;     // 2 second countdown
    static constexpr uint32_t FORMAT_DELAY_MS = 1000;      // 1 second before restart
    static constexpr uint32_t WIFI_RESET_DELAY_MS = 500;   // 0.5 second before restart
    
    // ANSI escape sequences
    static constexpr const char* ANSI_CLEAR_SCREEN = "\033[2J\033[H";

    // ============================================
    // MEMBER VARIABLES
    // ============================================
    
    WiFiManager& _wifi;      // Reference to WiFi manager
    NFCManager& _nfc;        // Reference to NFC manager
    bool _enabled;           // Command processing enabled flag

    // ============================================
    // COMMAND HANDLERS
    // ============================================
    
    /**
     * @brief Handle WiFi-related commands
     * @param subcmd Subcommand and arguments (e.g., "status", "scan", "add SSID PASS")
     * 
     * Supported subcommands:
     * - status: Show connection info
     * - scan: Interactive network selection
     * - ap: Force AP mode
     * - reset: Clear credentials and restart
     * - reconnect: Reconnect to saved network
     * - add SSID PASSWORD: Add credential
     */
    void handleWifiCommands(const String& subcmd);

    /**
     * @brief Handle NFC-related commands
     * @param subcmd Subcommand and arguments
     * 
     * Supported subcommands:
     * - read_srix: Read SRIX4K tag
     * - mifare_read: Read Mifare Classic tag
     * - mifare_uid: Read Mifare UID only
     * - mifare_write: Write Mifare tag
     * - save <filename>: Save tag data to file
     * - load <filename.ext>: Load tag data from file
     * - wait [seconds]: Wait for SRIX tag detection
     */
    void handleNfcCommands(const String& subcmd);

    /**
     * @brief Handle system-related commands
     * @param subcmd Subcommand and arguments
     * 
     * Supported subcommands:
     * - info: Show system information (chip, memory, filesystem)
     * - restart/reboot: Restart ESP32
     * - format: Format LittleFS (requires confirmation)
     * - heap: Show free heap memory
     */
    void handleSystemCommands(const String& subcmd);

    /**
     * @brief Display command reference
     * 
     * Shows comprehensive list of all available commands
     * organized by category (WiFi, NFC, System).
     */
    void showHelp();

    // ============================================
    // UTILITY METHODS
    // ============================================
    
    /**
     * @brief Detect NFC protocol from file extension
     * @param filename Filename with extension (e.g., "tag.srix")
     * @param[out] filenameWithoutExt Filename with extension removed
     * @return Detected protocol or PROTOCOL_UNKNOWN
     */
    NFCManager::Protocol detectProtocolFromExtension(
        const String& filename, 
        String& filenameWithoutExt
    );

    /**
     * @brief Display hex dump of tag data
     * @param data Pointer to data buffer
     * @param size Size of data in bytes
     * @param maxBytes Maximum bytes to display (0 = all)
     */
    void printHexDump(const uint8_t* data, size_t size, size_t maxBytes = 0);
};
