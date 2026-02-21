#include "ble_handler.h"
#include <mbedtls/sha256.h>

// ========================================
// CONSTRUCTOR & DESTRUCTOR
// ========================================

BLEHandler::BLEHandler() 
    : _server(nullptr)
    , _tx_char(nullptr)
    , _rx_char(nullptr)
    , _advertising(nullptr)
    , _initialized(false)
    , _status(BT_DISCONNECTED)
    , _connected_count(0)
    , _device_name(BT_DEVICE_NAME)
    , _max_mtu(BT_MAX_MTU_SIZE)
    , _current_mtu(23)  // Default BLE MTU
    , _security_level(BT_ENABLE_CHECKSUM ? SECURITY_CHECKSUM : SECURITY_NONE)
    , _command_ready(false)
    , _server_callbacks(nullptr)
    , _char_callbacks(nullptr)
    , _connection_mutex(portMUX_INITIALIZER_UNLOCKED)
    , _stats_mutex(portMUX_INITIALIZER_UNLOCKED)
    , _last_timeout_check(0)
    , _reconnect_info{false, 0, 0} {
    
    // Initialize statistics
    memset(&_stats, 0, sizeof(_stats));
    
    LOG_INFO("BLE", "BLE Handler initialized");
}

BLEHandler::~BLEHandler() {
    if (_initialized) {
        stopAdvertising();
        
        // Clear callbacks before destroying server
        if (_server && _server_callbacks) {
            _server->setCallbacks(nullptr);
        }
        if (_rx_char && _char_callbacks) {
            _rx_char->setCallbacks(nullptr);
        }
        
        // Clean up callback instances
        if (_server_callbacks) {
            delete _server_callbacks;
            _server_callbacks = nullptr;
        }
        
        if (_char_callbacks) {
            delete _char_callbacks;
            _char_callbacks = nullptr;
        }
        
        // Clear connection tracking
        _connections.clear();
        
        // Don't delete server - managed by NimBLEDevice
        _server = nullptr;
        _tx_char = nullptr;
        _rx_char = nullptr;
        _advertising = nullptr;
        
        NimBLEDevice::deinit();
        _initialized = false;
        
        LOG_INFO("BLE", "BLE Handler deinitialized");
    }
}

// ========================================
// INITIALIZATION
// ========================================

bool BLEHandler::begin() {
    if (_initialized) {
        LOG_WARN("BLE", "Already initialized - deinit first");
        return false;
    }
    
    LOG_INFO("BLE", "Initializing BLE...");
    
    // Clear any previous state to prevent memory leaks
    NimBLEDevice::deinit();
    
    // Initialize NimBLE device
    if (!NimBLEDevice::init(_device_name.c_str())) {
        LOG_ERROR("BLE", "NimBLE initialization failed");
        return false;
    }
    
    // Set power level
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // +9dbm
    
    // Clear connections and statistics
    _connections.clear();
    memset(&_stats, 0, sizeof(_stats));
    _connected_count = 0;
    _status = BT_DISCONNECTED;
    
    // Create server and services
    if (!createServer()) {
        LOG_ERROR("BLE", "Failed to create BLE server");
        return false;
    }
    
    if (!createUARTService()) {
        LOG_ERROR("BLE", "Failed to create UART service");
        return false;
    }
    
    if (!setupAdvertising()) {
        LOG_ERROR("BLE", "Failed to setup advertising");
        return false;
    }
    
    // Initialize reconnection management
    _reconnect_info.enabled = true;
    _reconnect_info.retry_count = 0;
    _reconnect_info.last_attempt_ms = millis();
    _last_timeout_check = millis();
    
    // Only set initialized after ALL setup complete
    _initialized = true;
    
    // Start advertising
    startAdvertising();
    
    portENTER_CRITICAL(&_stats_mutex);
    _stats.uptime_ms = millis();
    portEXIT_CRITICAL(&_stats_mutex);
    
    LOG_INFO("BLE", "BLE initialized successfully");
    LOG_INFO("BLE", "Device: %s", _device_name.c_str());
    LOG_INFO("BLE", "Bluetooth MAC: %s", NimBLEDevice::getAddress().toString().c_str());
    LOG_INFO("BLE", "Max MTU: %d bytes", _max_mtu);
    
    return true;
}

bool BLEHandler::createServer() {
    // Create server with callback
    _server = NimBLEDevice::createServer();
    if (!_server) {
        LOG_ERROR("BLE", "Failed to create server");
        return false;
    }
    
    // Set server callbacks
    _server_callbacks = new ServerCallbacks(this);
    _server->setCallbacks(_server_callbacks);
    
    LOG_DEBUG("BLE", "BLE server created");
    return true;
}

bool BLEHandler::createUARTService() {
    // Create Nordic UART Service
    NimBLEService* uartService = _server->createService(BT_NUS_SERVICE_UUID);
    if (!uartService) {
        LOG_ERROR("BLE", "Failed to create UART service");
        return false;
    }
    
    // Create TX characteristic (server responses)
    _tx_char = uartService->createCharacteristic(
        BT_NUS_TX_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ
    );
    if (!_tx_char) {
        LOG_ERROR("BLE", "Failed to create TX characteristic");
        return false;
    }
    
    // Create RX characteristic (client commands)
    _rx_char = uartService->createCharacteristic(
        BT_NUS_RX_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ
    );
    if (!_rx_char) {
        LOG_ERROR("BLE", "Failed to create RX characteristic");
        return false;
    }
    
    // Set characteristic callbacks
    _char_callbacks = new CharacteristicCallbacks(this);
    _rx_char->setCallbacks(_char_callbacks);
    
    // Start service
    uartService->start();
    
    LOG_DEBUG("BLE", "UART service created");
    return true;
}

bool BLEHandler::setupAdvertising() {
    if (!_advertising) {
        _advertising = NimBLEDevice::getAdvertising();
        if (!_advertising) {
            LOG_ERROR("BLE", "Failed to get advertising instance");
            return false;
        }
    }
    
    // Configure advertising data with proper flags
    NimBLEAdvertisementData advData;
    advData.setFlags(0x06);  // LE General Discoverable + BR/EDR Not Supported
    advData.addServiceUUID(BT_NUS_SERVICE_UUID);
    
    // Use shortened name in advertisement if needed to avoid payload overflow
    String advName = _device_name;
    if (advName.length() > 8) {
        advName = _device_name.substring(0, 8);  // Truncate for space
        advData.setName(advName.c_str(), false);  // Shortened name
        LOG_WARN("BLE", "Device name truncated for advertising payload");
    } else {
        advData.setName(_device_name.c_str(), true);  // Complete name
    }
    
    _advertising->setAdvertisementData(advData);
    
    // Set scan response with complete name
    NimBLEAdvertisementData scanResp;
    scanResp.setName(_device_name.c_str(), true);
    _advertising->setScanResponseData(scanResp);
    _advertising->enableScanResponse(true);
    
    // Set proper advertising intervals (in 0.625ms units)
    _advertising->setMinInterval(BT_ADVERTISING_INTERVAL_MIN);   // 30ms minimum
    _advertising->setMaxInterval(BT_ADVERTISING_INTERVAL_MAX);   // 60ms maximum
    
    // Note: Connection parameters (latency, timeout) are negotiated via connection callbacks
    // NimBLE does not support setting them via advertising object
    
    LOG_DEBUG("BLE", "Advertising configured with proper BLE parameters");
    LOG_DEBUG("BLE", "Device name: %s (adv: %s)", _device_name.c_str(), advName.c_str());
    LOG_DEBUG("BLE", "Advertising interval: %d-%d (0.625ms units)", 
              BT_ADVERTISING_INTERVAL_MIN, BT_ADVERTISING_INTERVAL_MAX);
    
    return true;
}

// ========================================
// ADVERTISING CONTROL
// ========================================

void BLEHandler::startAdvertising() {
    if (!_initialized) {
        LOG_WARN("BLE", "Cannot start advertising - not initialized");
        return;
    }
    
    if (_status == BT_ADVERTISING) {
        LOG_DEBUG("BLE", "Already advertising");
        return;
    }
    
    if (_advertising && _advertising->start()) {
        _status = BT_ADVERTISING;
        updateLEDStatus();
        LOG_INFO("BLE", "Advertising started");
        LOG_INFO("BLE", "Bluetooth MAC (advertising): %s", NimBLEDevice::getAddress().toString().c_str());
    } else {
        LOG_ERROR("BLE", "Failed to start advertising");
    }
}

void BLEHandler::stopAdvertising() {
    if (!_initialized || !_advertising) {
        return;
    }
    
    _advertising->stop();
    
    portENTER_CRITICAL(&_connection_mutex);
    if (_status == BT_ADVERTISING) {
        // More robust status determination
        if (_connected_count > 0) {
            _status = BT_CONNECTED;
        } else {
            _status = BT_DISCONNECTED;
        }
    }
    portEXIT_CRITICAL(&_connection_mutex);
    
    updateLEDStatus();
    LOG_INFO("BLE", "Advertising stopped");
}

// ========================================
// COMMUNICATION METHODS
// ========================================

bool BLEHandler::sendResponse(const JsonDocument& response) {
    if (!_initialized || !_tx_char || _status < BT_CONNECTED) {
        LOG_WARN("BLE", "Cannot send response - not connected");
        return false;
    }
    
    // Check if any clients are subscribed to notifications
    // In NimBLE, we proceed with notification attempt - it will fail silently if no subscribers
    if (_tx_char == nullptr) {
        LOG_ERROR("BLE", "TX characteristic is null");
        return false;
    }
    
    // Use JsonDocument for JSON processing
    JsonDocument tempDoc;
    
    tempDoc.set(response);
    
    // Serialize JSON to string
    String responseStr;
    serializeJson(tempDoc, responseStr);
    
    // Add checksum if response is large and security level requires it
    if (_security_level >= SECURITY_CHECKSUM && responseStr.length() > BT_CHECKSUM_THRESHOLD) {
        if (!addChecksumToResponse(tempDoc, responseStr.length())) {
            LOG_WARN("BLE", "Failed to add checksum to response");
        } else {
            // Re-serialize with checksum
            responseStr = "";
            serializeJson(tempDoc, responseStr);
        }
    }
    
    // Send data
    _tx_char->setValue(responseStr.c_str());
    _tx_char->notify();
    
    // Update statistics thread-safely
    portENTER_CRITICAL(&_stats_mutex);
    _stats.responses_sent++;
    _stats.bytes_transmitted += responseStr.length();
    portEXIT_CRITICAL(&_stats_mutex);
    
    LOG_DEBUG("BLE", "Response sent (%d bytes)", responseStr.length());
    LOG_VERBOSE("BLE", "JSON: %s", responseStr.c_str());
    return true;
}

bool BLEHandler::sendError(const String& command, const String& error_message, int error_code) {
    JsonDocument response;
    prepareResponse(response, false, command, error_message);
    response["error_code"] = error_code;
    
    return sendResponse(response);
}

bool BLEHandler::sendSuccess(const String& command, const JsonVariantConst& data, const String& message) {
    JsonDocument response;
    prepareResponse(response, true, command, message);
    
    if (!data.isNull()) {
        response["data"] = data;
    }
    
    return sendResponse(response);
}

bool BLEHandler::checkForCommand() {
    return _command_ready;
}

bool BLEHandler::getCommand(JsonDocument& command) {
    if (!_command_ready || _command_buffer.length() == 0) {
        return false;
    }
    
    // Parse command from buffer
    DeserializationError error = deserializeJson(command, _command_buffer);
    
    if (error) {
        LOG_ERROR("BLE", "JSON parse error: %s", error.c_str());
        clearCommand();
        return false;
    }
    
    // Get command ID for logging
    const char* cmdId = command["id"] | "unknown";
    const char* cmdType = command["cmd"] | "unknown";
    
    LOG_DEBUG("BLE", "Received command: %s (ID: %s)", cmdType, cmdId);
    
    clearCommand();
    return true;
}

void BLEHandler::clearCommand() {
    _command_buffer = "";
    _command_ready = false;
}

// ========================================
// SECURITY & VALIDATION
// ========================================

bool BLEHandler::calculateChecksum(const uint8_t* data, size_t length, uint8_t* checksum) {
    if (!data || !checksum || length == 0) {
        return false;
    }
    
    mbedtls_sha256(data, length, checksum, 0);  // 0 = SHA-256
    return true;
}

bool BLEHandler::verifyChecksum(const uint8_t* data, size_t length, const uint8_t* expected_checksum) {
    if (!data || !expected_checksum || length == 0) {
        return false;
    }
    
    uint8_t calculated_checksum[32];
    if (!calculateChecksum(data, length, calculated_checksum)) {
        return false;
    }
    
    bool valid = (memcmp(calculated_checksum, expected_checksum, 32) == 0);
    
    if (valid) {
        _stats.checksum_validated++;
        LOG_DEBUG("BLE", "Checksum validated successfully");
    } else {
        LOG_WARN("BLE", "Checksum validation failed");
    }
    
    return valid;
}

// ========================================
// CONFIGURATION
// ========================================

bool BLEHandler::setDeviceName(const String& name) {
    if (name.length() == 0 || name.length() > 20) {
        LOG_ERROR("BLE", "Invalid device name length (1-20 characters)");
        return false;
    }
    
    _device_name = name;
    
    if (_initialized) {
        NimBLEDevice::deinit();
        NimBLEDevice::init(_device_name.c_str());
        LOG_INFO("BLE", "Device name updated to: %s", _device_name.c_str());
    }
    
    return true;
}

// ========================================
// STATUS & DIAGNOSTICS
// ========================================

JsonDocument BLEHandler::getDiagnostics() {
    JsonDocument diagnostics;
    
    diagnostics["initialized"] = _initialized;
    diagnostics["status"] = (int)_status;
    diagnostics["status_text"] = _status == BT_DISCONNECTED ? "disconnected" :
                                _status == BT_ADVERTISING ? "advertising" :
                                _status == BT_CONNECTED ? "connected" : "processing";
    diagnostics["connected_count"] = _connected_count;
    diagnostics["device_name"] = _device_name;
    diagnostics["max_mtu"] = _max_mtu;
    diagnostics["current_mtu"] = _current_mtu;
    diagnostics["security_level"] = (int)_security_level;
    
    return diagnostics;
}

JsonDocument BLEHandler::getStatistics() {
    JsonDocument stats;
    
    stats["commands_received"] = _stats.commands_received;
    stats["responses_sent"] = _stats.responses_sent;
    stats["bytes_transmitted"] = _stats.bytes_transmitted;
    stats["bytes_received"] = _stats.bytes_received;
    stats["checksum_validated"] = _stats.checksum_validated;
    stats["connection_count"] = _stats.connection_count;
    stats["errors_sent"] = _stats.errors_sent;
    stats["uptime_ms"] = _initialized ? (millis() - _stats.uptime_ms) : 0;
    
    return stats;
}

void BLEHandler::resetStatistics() {
    memset(&_stats, 0, sizeof(_stats));
    if (_initialized) {
        _stats.uptime_ms = millis();
    }
    
    LOG_INFO("BLE", "Statistics reset");
}

// ========================================
// CALLBACK IMPLEMENTATIONS
// ========================================

void BLEHandler::ServerCallbacks::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
    uint32_t conn_handle = connInfo.getConnHandle();
    uint16_t mtu = connInfo.getMTU();
    
    // Thread-safe connection management
    portENTER_CRITICAL(&_handler->_connection_mutex);
    
    // Add connection to tracking
    _handler->addConnection(conn_handle, mtu);
    
    _handler->_connected_count++;
    if (_handler->_status < BT_CONNECTED) {
        _handler->_status = BT_CONNECTED;
    }
    
    portEXIT_CRITICAL(&_handler->_connection_mutex);
    
    // Update statistics thread-safely
    portENTER_CRITICAL(&_handler->_stats_mutex);
    _handler->_stats.connection_count++;
    portEXIT_CRITICAL(&_handler->_stats_mutex);
    
    // MTU negotiation is handled automatically by NimBLE during connection
    // No manual updateMTU() call is needed or available
    
    _handler->updateLEDStatus();
    
    LOG_INFO("BT", "Client connected (handle: %d, total: %d, MTU: %d)", 
             conn_handle, _handler->_connected_count, mtu);
}

void BLEHandler::ServerCallbacks::onDisconnect(NimBLEConnInfo& connInfo, int reason) {
    uint32_t conn_handle = connInfo.getConnHandle();
    
    portENTER_CRITICAL(&_handler->_connection_mutex);
    
    // Remove connection from tracking
    _handler->removeConnection(conn_handle);
    
    if (_handler->_connected_count > 0) {
        _handler->_connected_count--;
    }
    
    // Clear command buffer for disconnected client
    if (_handler->_connected_count == 0) {
        _handler->_status = BT_ADVERTISING;
        _handler->_current_mtu = 23;  // Reset to default
        _handler->clearCommand();
    }
    
    portEXIT_CRITICAL(&_handler->_connection_mutex);
    
    // Restart advertising if no clients (outside critical section)
    if (_handler->_connected_count == 0) {
        _handler->handleReconnection();
    }
    
    _handler->updateLEDStatus();
    
    LOG_INFO("BT", "Client disconnected (handle: %d, reason: %d, remaining: %d)", 
             conn_handle, reason, _handler->_connected_count);
}


void BLEHandler::CharacteristicCallbacks::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    if (pCharacteristic != _handler->_rx_char) {
        return;
    }
    
    // Get actual connection handle
    uint32_t conn_handle = connInfo.getConnHandle();
    
    std::string rxData = pCharacteristic->getValue();
    _handler->processReceivedData((const uint8_t*)rxData.data(), rxData.length(), conn_handle);
}

void BLEHandler::CharacteristicCallbacks::onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) {
    LOG_DEBUG("BLE", "Client subscribed to characteristic");
}

void BLEHandler::CharacteristicCallbacks::onUnsubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    LOG_DEBUG("BLE", "Client unsubscribed from characteristic");
}

// ========================================
// HELPER METHODS
// ========================================

void BLEHandler::processReceivedData(const uint8_t* data, size_t length, uint32_t conn_handle) {
    if (!data || length == 0) {
        return;
    }
    
    // Update connection activity with real handle
    updateConnectionActivity(conn_handle);
    
    // Check buffer size BEFORE adding data
    if (_command_buffer.length() + length > BT_MAX_COMMAND_SIZE) {
        LOG_WARN("BLE", "Command would exceed buffer limit, clearing buffer");
        clearCommand();
        return;
    }
    
    // Safe concatenation with length limit
    size_t actual_length = min(length, (size_t)(BT_MAX_COMMAND_SIZE - _command_buffer.length()));
    
    _command_buffer += String((const char*)data, actual_length);
    
    // Update statistics thread-safely
    portENTER_CRITICAL(&_stats_mutex);
    _stats.bytes_received += actual_length;
    portEXIT_CRITICAL(&_stats_mutex);
    
    LOG_DEBUG("BLE", "Received %d bytes (total buffer: %d)", 
              actual_length, _command_buffer.length());
    
    // More robust command detection
    int brace_count = 0;
    bool in_string = false;
    for (size_t i = 0; i < _command_buffer.length(); i++) {
        char c = _command_buffer[i];
        if (c == '"' && (i == 0 || _command_buffer[i-1] != '\\')) {
            in_string = !in_string;
        } else if (!in_string) {
            if (c == '{') brace_count++;
            else if (c == '}') brace_count--;
        }
    }
    
    if (brace_count == 0 && _command_buffer.indexOf("{") != -1) {
        _command_ready = true;
        portENTER_CRITICAL(&_stats_mutex);
        _stats.commands_received++;
        portEXIT_CRITICAL(&_stats_mutex);
        
        LOG_DEBUG("BLE", "Command ready for processing");
    }
}

void BLEHandler::updateLEDStatus() {
    // LED status would be handled by LED manager
    // This is a placeholder for future integration
    
    switch (_status) {
        case BT_ADVERTISING:
            LOG_DEBUG("BLE", "Status: Advertising");
            break;
        case BT_CONNECTED:
            LOG_DEBUG("BLE", "Status: Connected");
            break;
        case BT_PROCESSING:
            LOG_DEBUG("BLE", "Status: Processing");
            break;
        case BT_DISCONNECTED:
        default:
            LOG_DEBUG("BLE", "Status: Disconnected");
            break;
    }
}

void BLEHandler::prepareResponse(JsonDocument& response, bool success, const String& command, const String& message) {
    response.clear();
    response["success"] = success;
    response["cmd"] = command;
    response["timestamp"] = millis();
    response["id"] = "";  // Will be filled by caller if needed
    
    if (message.length() > 0) {
        response["message"] = message;
    }
}

bool BLEHandler::addChecksumToResponse(JsonDocument& response, size_t data_size) {
    // Convert response to string
    String responseStr;
    serializeJson(response, responseStr);
    
    // Calculate checksum
    uint8_t checksum[32];
    if (!calculateChecksum((const uint8_t*)responseStr.c_str(), responseStr.length(), checksum)) {
        return false;
    }
    
    // Add checksum to response
    response["checksum"] = bytesToHex(checksum, 32);
    response["checksum_size"] = data_size;
    
    return true;
}

String BLEHandler::bytesToHex(const uint8_t* data, size_t length) {
    String hex = "";
    hex.reserve(length * 2);
    
    for (size_t i = 0; i < length; i++) {
        char byteStr[4];
        snprintf(byteStr, sizeof(byteStr), "%02X", data[i]);
        hex += byteStr;
    }
    
    return hex;
}

size_t BLEHandler::hexToBytes(const String& hex, uint8_t* data, size_t max_length) {
    size_t hexLen = hex.length();
    if (hexLen % 2 != 0) {
        return 0;  // Invalid hex string
    }
    
    size_t byteCount = min(hexLen / 2, max_length);
    
    for (size_t i = 0; i < byteCount; i++) {
        uint8_t byte = 0;
        char high = hex[i * 2];
        char low = hex[i * 2 + 1];
        
        if (high >= '0' && high <= '9') byte = (high - '0') << 4;
        else if (high >= 'A' && high <= 'F') byte = (high - 'A' + 10) << 4;
        else if (high >= 'a' && high <= 'f') byte = (high - 'a' + 10) << 4;
        
        if (low >= '0' && low <= '9') byte |= (low - '0');
        else if (low >= 'A' && low <= 'F') byte |= (low - 'A' + 10);
        else if (low >= 'a' && low <= 'f') byte |= (low - 'a' + 10);
        
        data[i] = byte;
    }
    
    return byteCount;
}

// ========================================
// CONNECTION MANAGEMENT
// ========================================

void BLEHandler::checkConnectionTimeouts() {
    uint32_t now = millis();
    if (now - _last_timeout_check < _connection_timeout_check_interval) {
        return;
    }
    _last_timeout_check = now;
    
    // Collect handles to disconnect (can't disconnect inside critical section - causes deadlock)
    std::vector<uint32_t> timeout_handles;
    
    portENTER_CRITICAL(&_connection_mutex);
    for (const auto& conn : _connections) {
        if (now - conn.last_activity_ms > BT_CONNECTION_TIMEOUT_MS) {
            timeout_handles.push_back(conn.conn_handle);
        }
    }
    portEXIT_CRITICAL(&_connection_mutex);
    
    // Disconnect timed-out connections outside critical section
    if (!timeout_handles.empty()) {
        NimBLEServer* server = NimBLEDevice::getServer();
        if (server) {
            for (uint32_t handle : timeout_handles) {
                LOG_WARN("BLE", "Connection timeout for handle %d - disconnecting", handle);
                server->disconnect(handle);
                // removeConnection will be called by onDisconnect callback
            }
        }
    }
}

void BLEHandler::addConnection(uint32_t conn_handle, uint16_t mtu) {
    ConnectionInfo conn_info;
    conn_info.conn_handle = conn_handle;
    conn_info.mtu = mtu;
    conn_info.last_activity_ms = millis();
    conn_info.subscribed = false;
    
    _connections.push_back(conn_info);
    
    // Update global MTU if this is first/only connection
    if (_connected_count == 1) {
        _current_mtu = mtu;
    }
    
    LOG_DEBUG("BLE", "Added connection %d with MTU %d", conn_handle, mtu);
}

void BLEHandler::removeConnection(uint32_t conn_handle) {
    auto it = std::find_if(_connections.begin(), _connections.end(),
                          [conn_handle](const ConnectionInfo& info) {
                              return info.conn_handle == conn_handle;
                          });
    
    if (it != _connections.end()) {
        _connections.erase(it);
        LOG_DEBUG("BLE", "Removed connection %d", conn_handle);
    }
}

void BLEHandler::updateConnectionActivity(uint32_t conn_handle) {
    auto it = std::find_if(_connections.begin(), _connections.end(),
                          [conn_handle](const ConnectionInfo& info) {
                              return info.conn_handle == conn_handle;
                          });
    
    if (it != _connections.end()) {
        it->last_activity_ms = millis();
    }
}

void BLEHandler::enableReconnection(bool enable) {
    _reconnect_info.enabled = enable;
    _reconnect_info.retry_count = 0;
    _reconnect_info.last_attempt_ms = millis();
    
    if (enable && _connected_count == 0) {
        startAdvertising();
    }
}

void BLEHandler::handleReconnection() {
    if (!_reconnect_info.enabled || _connected_count > 0) {
        return;
    }
    
    uint32_t now = millis();
    uint32_t retry_delay = 1000 * (1 << _reconnect_info.retry_count); // Exponential backoff
    retry_delay = min(retry_delay, (uint32_t)30000); // Cap at 30 seconds
    
    if (now - _reconnect_info.last_attempt_ms >= retry_delay) {
        _reconnect_info.last_attempt_ms = now;
        _reconnect_info.retry_count++;
        
        if (_reconnect_info.retry_count <= 5) { // Max 5 attempts
            LOG_INFO("BLE", "Attempting reconnection (%d/5)", _reconnect_info.retry_count);
            startAdvertising();
        } else {
            LOG_WARN("BLE", "Max reconnection attempts reached, giving up");
            _reconnect_info.enabled = false;
        }
    }
}