#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "modules/wifi/wifi_manager.h"
#include "modules/rfid/nfc_manager.h"
#include "modules/bluetooth/ble_handler.h"
#include "modules/led/led_manager.h"
#include "config.h"
#include "logger.h"

/**
 * @file bluetooth_manager.h
 * @brief Bluetooth Manager for ESP32 NFC Tool
 * 
 * Provides high-level interface for Bluetooth Low Energy communication.
 * Acts as coordinator between BLE handler and system modules (WiFi, NFC).
 * Features:
 * - JSON command parsing and routing
 * - Integration with existing SerialCommander logic
 * - Secure data transfers with checksum validation
 * - Command queue and processing management
 * - Diagnostics and statistics
 * 
 * Command Categories:
 * - wifi: WiFi status, scan, connect, disconnect
 * - nfc: NFC read/write operations (SRIX, Mifare)
 * - system: System info, restart, diagnostics
 * - files: File operations (list, download, delete)
 * - bt: Bluetooth specific commands
 * 
 * Usage:
 * @code
 * BluetoothManager btMgr(wifiMgr, nfcMgr, ledMgr);
 * btMgr.begin();
 * 
 * // In main loop or task:
 * btMgr.handleCommands();
 * @endcode
 */

class BluetoothManager {
public:
    /**
     * @brief Command processing status
     */
    enum CommandStatus {
        CMD_SUCCESS = 0,           ///< Command completed successfully
        CMD_ERROR = -1,            ///< Command failed with error
        CMD_INVALID = -2,          ///< Invalid command format
        CMD_TIMEOUT = -3,          ///< Command processing timeout
        CMD_NOT_IMPLEMENTED = -4,  ///< Command not yet implemented
        CMD_NO_DATA = -5,          ///< No data available
        CMD_IN_PROGRESS = 1        ///< Command currently processing
    };

    // ============================================
    // CONSTRUCTOR & INITIALIZATION
    // ============================================
    
    /**
     * @brief Constructor
     * @param wifi Reference to WiFiManager instance
     * @param nfc Reference to NFCManager instance
     * @param led Reference to LED manager instance
     */
    BluetoothManager(WiFiManager& wifi, NFCManager& nfc, LedManager& led);

    /**
     * @brief Destructor
     */
    ~BluetoothManager();

    /**
     * @brief Initialize Bluetooth Manager
     * @return true if initialization successful
     * 
     * Initializes BLE handler and sets up command processing.
     */
    bool begin();

    /**
     * @brief Check if Bluetooth Manager is ready
     * @return true if initialized and ready
     */
    bool isReady() const { return _initialized; }

    /**
     * @brief Get BLE handler instance
     * @return Reference to BLE handler
     */
    BLEHandler& getBLEHandler() { return _ble_handler; }

    /**
     * @brief Enable/disable command processing
     * @param enabled true to enable, false to disable
     */
    void setEnabled(bool enabled) { _enabled = enabled; }

    /**
     * @brief Check if command processing is enabled
     * @return true if enabled
     */
    bool isEnabled() const { return _enabled; }

    // ============================================
    // COMMAND PROCESSING
    // ============================================
    
    /**
     * @brief Handle incoming commands
     * 
     * Called periodically from main loop or task.
     * Checks for incoming BLE commands and processes them.
     * 
     * @return CommandStatus of last processed command
     */
    CommandStatus handleCommands();

    /**
     * @brief Process a specific command
     * @param command JSON document containing command
     * @return CommandStatus result
     * 
     * Used internally by handleCommands() and for testing.
     */
    CommandStatus processCommand(const JsonDocument& command);

    /**
     * @brief Send response to client
     * @param success Success flag
     * @param command Original command name
     * @param data Optional data to include
     * @param message Optional message
     * @param commandId Original command ID
     * @return true if send successful
     */
    bool sendResponse(bool success, const String& command, 
                      const JsonVariantConst& data = JsonVariantConst(),
                      const String& message = "", const String& commandId = "");

    /**
     * @brief Send error response
     * @param command Original command name
     * @param error_message Error description
     * @param error_code Error code
     * @param commandId Original command ID
     * @return true if send successful
     */
    bool sendError(const String& command, const String& error_message, 
                   int error_code, const String& commandId = "");

    // ============================================
    // COMMAND HANDLERS
    // ============================================
    
    /**
     * @brief Handle WiFi commands
     * @param params JSON parameters
     * @param commandId Command ID for response
     * @return CommandStatus result
     * 
     * Supported commands:
     * - status: Get WiFi connection status
     * - scan: Scan for networks (returns list)
     * - connect {ssid, password}: Connect to network
     * - disconnect: Disconnect current network
     * - reconnect: Reconnect to saved network
     */
    CommandStatus handleWifiCommand(const JsonObjectConst& params, const String& commandId);

    /**
     * @brief Handle NFC commands
     * @param params JSON parameters
     * @param commandId Command ID for response
     * @return CommandStatus result
     * 
     * Supported commands:
     * - read_srix {timeout}: Read SRIX4K tag
     * - mifare_read {timeout}: Read Mifare Classic tag
     * - mifare_uid {timeout}: Read Mifare UID only
     * - mifare_write {data}: Write Mifare tag
     * - save {filename}: Save current tag data
     * - load {filename}: Load tag data from file
     * - list {protocol}: List dump files
     * - delete {filename}: Delete dump file
     * - wait {seconds}: Wait for tag detection
     */
    CommandStatus handleNfcCommand(const JsonObjectConst& params, const String& commandId);

    /**
     * @brief Handle system commands
     * @param params JSON parameters
     * @param commandId Command ID for response
     * @return CommandStatus result
     * 
     * Supported commands:
     * - info: Get system information
     * - restart: Restart ESP32
     * - heap: Get heap information
     * - stats: Get system statistics
     * - diag: Get diagnostics information
     */
    CommandStatus handleSystemCommand(const JsonObjectConst& params, const String& commandId);

    /**
     * @brief Handle file commands
     * @param params JSON parameters
     * @param commandId Command ID for response
     * @return CommandStatus result
     * 
     * Supported commands:
     * - list {protocol}: List dump files
     * - download {filename}: Download file data
     * - delete {filename}: Delete file
     * - info {filename}: Get file information
     */
    CommandStatus handleFilesCommand(const JsonObjectConst& params, const String& commandId);

    /**
     * @brief Handle Bluetooth commands
     * @param params JSON parameters
     * @param commandId Command ID for response
     * @return CommandStatus result
     * 
     * Supported commands:
     * - status: Get BLE status
     * - stats: Get BLE statistics
     * - diag: Get BLE diagnostics
     * - reset_stats: Reset BLE statistics
     */
    CommandStatus handleBtCommand(const JsonObjectConst& params, const String& commandId);

    // ============================================
    // UTILITY METHODS
    // ============================================
    
    /**
     * @brief Get command status as string
     * @param status CommandStatus enum
     * @return String representation
     */
    String statusToString(CommandStatus status);

    /**
     * @brief Validate command format
     * @param command JSON document
     * @return true if format is valid
     */
    bool validateCommand(const JsonDocument& command);

    /**
     * @brief Get supported commands list
     * @return JSON array of available commands
     */
    JsonDocument getSupportedCommands();

    /**
     * @brief Get manager statistics
     * @return JSON document with statistics
     */
    JsonDocument getStatistics();

    /**
     * @brief Reset all statistics
     */
    void resetStatistics();

private:
    // ============================================
    // MEMBER VARIABLES
    // ============================================
    
    // Module references
    WiFiManager& _wifi;         ///< WiFi manager reference
    NFCManager& _nfc;           ///< NFC manager reference
    LedManager& _led;           ///< LED manager reference
    
    // BLE handler
    BLEHandler _ble_handler;    ///< BLE communication handler
    
    // State
    bool _initialized;          ///< Initialization flag
    bool _enabled;              ///< Command processing enabled
    uint32_t _commands_processed; ///< Total commands processed
    uint32_t _errors_count;     ///< Total errors encountered
    uint32_t _start_time_ms;    ///< Manager start time
    
    // Current command processing
    String _current_command_id; ///< ID of command being processed
    uint32_t _command_start_time; ///< Command start timestamp
    bool _processing_command;    ///< Flag when processing command

    // ============================================
    // COMMAND VALIDATION
    // ============================================
    
    /**
     * @brief Validate WiFi command parameters
     * @param params JSON parameters
     * @param command WiFi command name
     * @return true if parameters are valid
     */
    bool validateWifiParams(const JsonObjectConst& params, const String& command);

    /**
     * @brief Validate NFC command parameters
     * @param params JSON parameters
     * @param command NFC command name
     * @return true if parameters are valid
     */
    bool validateNfcParams(const JsonObjectConst& params, const String& command);

    /**
     * @brief Validate system command parameters
     * @param params JSON parameters
     * @param command System command name
     * @return true if parameters are valid
     */
    bool validateSystemParams(const JsonObjectConst& params, const String& command);

    /**
     * @brief Validate file command parameters
     * @param params JSON parameters
     * @param command File command name
     * @return true if parameters are valid
     */
    bool validateFileParams(const JsonObjectConst& params, const String& command);

    // ============================================
    // DATA PROCESSING
    // ============================================
    
    /**
     * @brief Convert NFC tag data to JSON
     * @param info TagInfo structure
     * @param includeData Include raw dump data
     * @return JSON document with tag information
     */
    JsonDocument nfcTagToJson(const NFCManager::TagInfo& info, bool includeData = true);

    /**
     * @brief Convert file list to JSON
     * @param files Vector of file information
     * @return JSON document with file list
     */
    JsonDocument fileListToJson(const std::vector<String>& files);

    /**
     * @brief Process file for secure transfer
     * @param filename File path
     * @param commandId Command ID for response
     * @return CommandStatus result
     * 
     * Reads file, calculates checksum, and sends in chunks if needed.
     */
    CommandStatus processFileTransfer(const String& filename, const String& commandId);

    /**
     * @brief Send large data in chunks
     * @param data Pointer to data
     * @param size Data size
     * @param commandId Command ID
     * @return true if transfer successful
     */
    bool sendChunkedData(const uint8_t* data, size_t size, const String& commandId);

    // ============================================
    // TIMEOUT HANDLING
    // ============================================

    // ============================================
    // ENCODING HELPERS
    // ============================================

    /**
     * @brief Encode binary data to Base64
     * @param data Pointer to binary data
     * @param length Length of data
     * @return Base64-encoded string
     */
    String base64Encode(const uint8_t* data, size_t length);
    
    /**
     * @brief Check if command processing timed out
     * @return true if timeout occurred
     */
    bool checkCommandTimeout();

    /**
     * @brief Abort current command processing
     */
    void abortCurrentCommand();

    // ============================================
    // HELPER METHODS
    // ============================================
    
    /**
     * @brief Generate unique command ID
     * @return New command ID string
     */
    String generateCommandId();

    /**
     * @brief Extract command type from full command
     * @param fullCommand Full command string (e.g., "nfc_read_srix")
     * @return Command type ("nfc", "wifi", etc.)
     */
    String getCommandType(const String& fullCommand);

    /**
     * @brief Log command processing
     * @param command Command string
     * @param status Result status
     * @param duration Processing duration (ms)
     */
    void logCommand(const String& command, CommandStatus status, uint32_t duration);

    /**
     * @brief Update LED status during command processing
     * @param processing true if processing command
     */
    void updateLEDStatus(bool processing);
};