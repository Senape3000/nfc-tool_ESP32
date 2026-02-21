#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <NimBLEAdvertising.h>
#include <ArduinoJson.h>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"
#include "logger.h"

/**
 * @file ble_handler.h
 * @brief Bluetooth Low Energy Handler for ESP32 NFC Tool
 * 
 * Provides BLE communication using Nordic UART Service (NUS).
 * Features:
 * - Secure JSON command/response protocol
 * - Checksum validation for large transfers
 * - Connection management and status tracking
 * - Integration with LED status indicators
 * 
 * Protocol:
 * - Client sends JSON commands via RX characteristic
 * - Server responds via TX characteristic with optional checksum
 * - Large data (>1024 bytes) includes SHA-256 checksum
 * 
 * @note Requires NimBLE library for ESP32
 */

class BLEHandler {
public:
    /**
     * @brief Connection status enumeration
     */
    enum ConnectionStatus {
        BT_DISCONNECTED = 0,      ///< No client connected
        BT_ADVERTISING = 1,       ///< Advertising for connections
        BT_CONNECTED = 2,         ///< Client connected
        BT_PROCESSING = 3         ///< Processing command
    };

    /**
     * @brief Transfer security levels
     */
    enum SecurityLevel {
        SECURITY_NONE = 0,        ///< No validation
        SECURITY_CHECKSUM = 1,    ///< SHA-256 checksum for large transfers
        SECURITY_FULL = 2         ///< Full validation (future)
    };

    // ============================================
    // CONSTRUCTOR & INITIALIZATION
    // ============================================
    
    /**
     * @brief Constructor
     */
    BLEHandler();

    /**
     * @brief Destructor
     * 
     * Cleanup BLE server and resources.
     */
    ~BLEHandler();

    /**
     * @brief Initialize BLE subsystem
     * @return true if initialization successful
     * 
     * Sets up BLE device, server, advertising, and Nordic UART Service.
     * Must be called before any other BLE operations.
     */
    bool begin();

    /**
     * @brief Start advertising for connections
     */
    void startAdvertising();

    /**
     * @brief Stop advertising
     */
    void stopAdvertising();

    /**
     * @brief Check if BLE is initialized
     * @return true if begin() was successful
     */
    bool isInitialized() const { return _initialized; }

    /**
     * @brief Get current connection status
     * @return Connection status enum
     */
    ConnectionStatus getStatus() const { return _status; }

    /**
     * @brief Check if client is connected
     * @return true if client connected
     */
    bool isConnected() const { return _status >= BT_CONNECTED; }

    /**
     * @brief Get number of connected clients
     * @return Number of active connections
     */
    uint32_t getConnectedCount() const { return _connected_count; }

    // ============================================
    // COMMUNICATION METHODS
    // ============================================
    
    /**
     * @brief Send JSON response to client
     * @param response JSON document to send
     * @return true if send successful
     * 
     * Automatically adds checksum for large transfers if enabled.
     */
    bool sendResponse(const JsonDocument& response);

    /**
     * @brief Send error response
     * @param command Original command name
     * @param error_message Error description
     * @param error_code Error code (default: -1)
     * @return true if send successful
     */
    bool sendError(const String& command, const String& error_message, int error_code = -1);

    /**
     * @brief Send success response with data
     * @param command Original command name
     * @param data JSON data to include
     * @param message Optional success message
     * @return true if send successful
     */
    bool sendSuccess(const String& command, const JsonVariantConst& data, const String& message = "");

    /**
     * @brief Check for incoming commands
     * @return true if command received (use getCommand() to retrieve)
     * 
     * Called periodically from main loop or task.
     */
    bool checkForCommand();

    /**
     * @brief Get latest received command
     * @param command Output JsonDocument (will be cleared)
     * @return true if command available
     */
    bool getCommand(JsonDocument& command);

    /**
     * @brief Clear command buffer
     */
    void clearCommand();

    // ============================================
    // SECURITY & VALIDATION
    // ============================================
    
    /**
     * @brief Calculate SHA-256 checksum
     * @param data Pointer to data buffer
     * @param length Data length in bytes
     * @param checksum Output 32-byte checksum buffer
     * @return true if calculation successful
     */
    bool calculateChecksum(const uint8_t* data, size_t length, uint8_t* checksum);

    /**
     * @brief Verify SHA-256 checksum
     * @param data Pointer to data buffer
     * @param length Data length in bytes
     * @param expected_checksum Expected checksum (32 bytes)
     * @return true if checksum matches
     */
    bool verifyChecksum(const uint8_t* data, size_t length, const uint8_t* expected_checksum);

    /**
     * @brief Set security level for transfers
     * @param level Security level
     */
    void setSecurityLevel(SecurityLevel level) { _security_level = level; }

    /**
     * @brief Get current security level
     * @return Current security level
     */
    SecurityLevel getSecurityLevel() const { return _security_level; }

    // ============================================
    // CONFIGURATION
    // ============================================
    
    /**
     * @brief Set device name
     * @param name New device name
     * @return true if set successful
     */
    bool setDeviceName(const String& name);

    /**
     * @brief Get device name
     * @return Current device name
     */
    String getDeviceName() const { return _device_name; }

    /**
     * @brief Set maximum MTU size
     * @param mtu_size Maximum MTU size
     */
    void setMaxMTU(uint16_t mtu_size) { _max_mtu = mtu_size; }

    /**
     * @brief Get current MTU size
     * @return Current MTU size
     */
    uint16_t getCurrentMTU() const { return _current_mtu; }

    // ============================================
    // STATUS & DIAGNOSTICS
    // ============================================
    
    /**
     * @brief Get diagnostic information
     * @return JSON document with BLE status
     */
    JsonDocument getDiagnostics();

    /**
     * @brief Get statistics
     * @return JSON document with transfer statistics
     */
    JsonDocument getStatistics();

    /**
     * @brief Reset statistics
     */
    void resetStatistics();

    /**
     * @brief Check for connection timeouts
     */
    void checkConnectionTimeouts();

private:
    // ============================================
    // MEMBER VARIABLES
    // ============================================
    
    // BLE objects
    NimBLEServer* _server;              ///< BLE server instance
    NimBLECharacteristic* _tx_char;     ///< TX characteristic (responses)
    NimBLECharacteristic* _rx_char;     ///< RX characteristic (commands)
    NimBLEAdvertising* _advertising;    ///< Advertising instance
    
    // Status
    bool _initialized;                   ///< BLE initialization flag
    ConnectionStatus _status;           ///< Current connection status
    uint32_t _connected_count;          ///< Number of connected clients
    String _device_name;                 ///< Current device name
    
    // Configuration
    uint16_t _max_mtu;                   ///< Maximum allowed MTU
    uint16_t _current_mtu;               ///< Current negotiated MTU
    SecurityLevel _security_level;      ///< Current security level
    
    // Command processing
    String _command_buffer;             ///< Buffer for received commands
    bool _command_ready;                ///< Flag when command is ready
    
    // Connection tracking for multiple clients
    struct ConnectionInfo {
        uint32_t conn_handle;
        uint32_t last_activity_ms;
        uint16_t mtu;
        bool subscribed;
    };
    std::vector<ConnectionInfo> _connections;
    
    // Statistics (protected by mutex)
    struct {
        uint32_t commands_received;      ///< Total commands received
        uint32_t responses_sent;         ///< Total responses sent
        uint32_t bytes_transmitted;      ///< Total bytes transmitted
        uint32_t bytes_received;         ///< Total bytes received
        uint32_t checksum_validated;     ///< Number of checksums validated
        uint32_t connection_count;       ///< Total connections established
        uint32_t errors_sent;            ///< Error responses sent
        uint32_t uptime_ms;              ///< BLE uptime in milliseconds
    } _stats;
    
    // Thread safety
    mutable portMUX_TYPE _connection_mutex;
    mutable portMUX_TYPE _stats_mutex;
    
    // Connection timeout management
    uint32_t _connection_timeout_check_interval = 5000;  // Check every 5 seconds
    uint32_t _last_timeout_check = 0;
    
    // Reconnection management
    struct {
        bool enabled;
        uint32_t last_attempt_ms;
        uint8_t retry_count;
    } _reconnect_info;

    // ============================================
    // CALLBACK CLASSES
    // ============================================
    
    /**
     * @brief Server callbacks for connection events
     */
    class ServerCallbacks : public NimBLEServerCallbacks {
    public:
        ServerCallbacks(BLEHandler* handler) : _handler(handler) {}
        
        void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo);
        void onDisconnect(NimBLEConnInfo& connInfo, int reason);
        
    private:
        BLEHandler* _handler;
    };
    
    /**
     * @brief Characteristic callbacks for data events
     */
    class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    public:
        CharacteristicCallbacks(BLEHandler* handler) : _handler(handler) {}
        
        void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo);
        void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue);
        void onUnsubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo);
        
    private:
        BLEHandler* _handler;
    };
    
    // Callback instances
    ServerCallbacks* _server_callbacks;
    CharacteristicCallbacks* _char_callbacks;

    // ============================================
    // HELPER METHODS
    // ============================================
    
    /**
     * @brief Create BLE server and services
     * @return true if creation successful
     */
    bool createServer();

    /**
     * @brief Create Nordic UART Service
     * @return true if creation successful
     */
    bool createUARTService();

    /**
     * @brief Setup advertising data
     * @return true if setup successful
     */
    bool setupAdvertising();

    /**
     * @brief Update LED status based on connection state
     */
    void updateLEDStatus();

    /**
     * @brief Process incoming data
     * @param data Pointer to received data
     * @param length Data length
     * @param conn_handle Connection handle for activity tracking
     */
    void processReceivedData(const uint8_t* data, size_t length, uint32_t conn_handle);

    /**
     * @brief Prepare JSON response with metadata
     * @param response JSON document to prepare
     * @param success Success flag
     * @param command Original command name
     * @param message Response message
     */
    void prepareResponse(JsonDocument& response, bool success, const String& command, const String& message = "");

    /**
     * @brief Add checksum to response if needed
     * @param response JSON document
     * @param data_size Size of data being transferred
     * @return true if checksum added
     */
    bool addChecksumToResponse(JsonDocument& response, size_t data_size);

    /**
     * @brief Convert bytes to hex string
     * @param data Pointer to data
     * @param length Data length
     * @return Hex string
     */
    String bytesToHex(const uint8_t* data, size_t length);

    /**
     * @brief Convert hex string to bytes
     * @param hex Hex string
     * @param data Output buffer
     * @param max_length Maximum bytes to write
     * @return Number of bytes converted
     */
    size_t hexToBytes(const String& hex, uint8_t* data, size_t max_length);

    /**
     * @brief Add connection to tracking list
     * @param conn_handle Connection handle
     * @param mtu MTU size
     */
    void addConnection(uint32_t conn_handle, uint16_t mtu);
    
    /**
     * @brief Remove connection from tracking list
     * @param conn_handle Connection handle
     */
    void removeConnection(uint32_t conn_handle);
    
    /**
     * @brief Update connection activity timestamp
     * @param conn_handle Connection handle
     */
    void updateConnectionActivity(uint32_t conn_handle);
    
    /**
     * @brief Enable or disable reconnection attempts
     * @param enable Enable reconnection
     */
    void enableReconnection(bool enable);
    
    /**
     * @brief Handle reconnection logic with exponential backoff
     */
    void handleReconnection();
};