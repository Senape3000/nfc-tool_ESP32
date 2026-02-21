#include "bluetooth_manager.h"

// ========================================
// CONSTRUCTOR & DESTRUCTOR
// ========================================

BluetoothManager::BluetoothManager(WiFiManager& wifi, NFCManager& nfc, LedManager& led)
    : _wifi(wifi)
    , _nfc(nfc)
    , _led(led)
    , _ble_handler()
    , _initialized(false)
    , _enabled(true)
    , _commands_processed(0)
    , _errors_count(0)
    , _start_time_ms(0)
    , _command_start_time(0)
    , _processing_command(false) {
    
    LOG_INFO("BT_MGR", "Bluetooth Manager created");
}

BluetoothManager::~BluetoothManager() {
    if (_initialized) {
        LOG_INFO("BT_MGR", "Bluetooth Manager destroyed");
    }
}

// ========================================
// INITIALIZATION
// ========================================

bool BluetoothManager::begin() {
    if (_initialized) {
        LOG_WARN("BT_MGR", "Already initialized");
        return true;
    }
    
    LOG_INFO("BT_MGR", "Initializing Bluetooth Manager...");
    
    // Initialize BLE handler
    if (!_ble_handler.begin()) {
        LOG_ERROR("BT_MGR", "Failed to initialize BLE handler");
        return false;
    }
    
    _start_time_ms = millis();
    _initialized = true;
    
    LOG_INFO("BT_MGR", "Bluetooth Manager initialized successfully");
    LOG_INFO("BT_MGR", "Ready to process commands");
    
    return true;
}

// ========================================
// COMMAND PROCESSING
// ========================================

BluetoothManager::CommandStatus BluetoothManager::handleCommands() {
    if (!_initialized || !_enabled) {
        return CMD_NO_DATA;
    }
    
    // Check for BLE connection timeouts
    _ble_handler.checkConnectionTimeouts();
    
    // Check for timeout on current command
    if (_processing_command && checkCommandTimeout()) {
        LOG_WARN("BT_MGR", "Command processing timeout");
        abortCurrentCommand();
        return CMD_TIMEOUT;
    }
    
    // Don't process new commands if one is already processing
    if (_processing_command) {
        return CMD_IN_PROGRESS;
    }
    
    // Check for incoming command
    if (!_ble_handler.checkForCommand()) {
        return CMD_NO_DATA;
    }
    
    // Get command from BLE handler
    JsonDocument command;
    if (!_ble_handler.getCommand(command)) {
        LOG_ERROR("BT_MGR", "Failed to get command from BLE handler");
        _errors_count++;
        return CMD_ERROR;
    }
    
    // Validate command format
    if (!validateCommand(command)) {
        LOG_ERROR("BT_MGR", "Invalid command format");
        _errors_count++;
        return CMD_INVALID;
    }
    
    // Get command ID for tracking
    String commandId = command["id"] | "";
    String cmdType = command["cmd"] | "";
    
    if (commandId.length() == 0) {
        commandId = generateCommandId();
    }
    
    LOG_DEBUG("BT_MGR", "Processing command: %s (ID: %s)", cmdType.c_str(), commandId.c_str());
    
    // Start command processing
    _current_command_id = commandId;
    _command_start_time = millis();
    _processing_command = true;
    updateLEDStatus(true);
    
    // Process the command
    CommandStatus result = processCommand(command);
    
    // Finish processing
    uint32_t duration = millis() - _command_start_time;
    logCommand(cmdType, result, duration);
    
    _processing_command = false;
    updateLEDStatus(false);
    _current_command_id = "";
    _command_start_time = 0;
    
    _commands_processed++;
    
    return result;
}

BluetoothManager::CommandStatus BluetoothManager::processCommand(const JsonDocument& command) {
    String cmdType = command["cmd"] | "";
    JsonObjectConst params = command["params"] | JsonObjectConst();
    String commandId = command["id"] | "";
    
    LOG_DEBUG("BT_MGR", "Command received - cmd: '%s', id: '%s'", cmdType.c_str(), commandId.c_str());
    
    if (cmdType.length() == 0) {
        sendError("unknown", "Missing command parameter", CMD_INVALID, commandId);
        return CMD_INVALID;
    }
    
    // Route command based on type
    String category = getCommandType(cmdType);
    LOG_DEBUG("BT_MGR", "Command category: '%s'", category.c_str());
    
    CommandStatus result = CMD_ERROR;
    
    if (category == "wifi") {
        result = handleWifiCommand(params, commandId);
    } else if (category == "nfc") {
        result = handleNfcCommand(params, commandId);
    } else if (category == "system") {
        result = handleSystemCommand(params, commandId);
    } else if (category == "files") {
        result = handleFilesCommand(params, commandId);
    } else if (category == "bt") {
        result = handleBtCommand(params, commandId);
    } else {
        sendError(cmdType, "Unknown command category", CMD_NOT_IMPLEMENTED, commandId);
        result = CMD_NOT_IMPLEMENTED;
    }
    
    return result;
}

bool BluetoothManager::sendResponse(bool success, const String& command, 
                                     const JsonVariantConst& data,
                                     const String& message, const String& commandId) {
    JsonDocument response;
    response["success"] = success;
    response["cmd"] = command;
    response["timestamp"] = millis();
    
    if (commandId.length() > 0) {
        response["id"] = commandId;
    }
    
    if (message.length() > 0) {
        response["message"] = message;
    }
    
    if (!data.isNull()) {
        response["data"] = data;
    }
    
    return _ble_handler.sendResponse(response);
}

bool BluetoothManager::sendError(const String& command, const String& error_message, 
                                 int error_code, const String& commandId) {
    JsonDocument response;
    response["success"] = false;
    response["cmd"] = command;
    response["error"] = error_message;
    response["error_code"] = error_code;
    response["timestamp"] = millis();
    
    if (commandId.length() > 0) {
        response["id"] = commandId;
    }
    
    return _ble_handler.sendResponse(response);
}

// ========================================
// WIFI COMMAND HANDLER
// ========================================

BluetoothManager::CommandStatus BluetoothManager::handleWifiCommand(const JsonObjectConst& params, const String& commandId) {
    String action = params["action"] | "";
    
    if (action == "status") {
        JsonDocument wifiStatus;
        wifiStatus["connected"] = WiFi.status() == WL_CONNECTED;
        wifiStatus["ssid"] = WiFi.SSID().c_str();
        wifiStatus["ip"] = WiFi.localIP().toString().c_str();
        wifiStatus["rssi"] = WiFi.RSSI();
        wifiStatus["gateway"] = WiFi.gatewayIP().toString().c_str();
        wifiStatus["mac"] = WiFi.macAddress().c_str();
        
        return sendResponse(true, "wifi_status", wifiStatus, "WiFi status retrieved", commandId) ? 
               CMD_SUCCESS : CMD_ERROR;
    
    } else if (action == "scan") {
        // WiFi scanning would need async implementation
        // For now, return placeholder
        JsonDocument scanResult;
        scanResult["scanning"] = false;
        scanResult["networks"] = JsonArray();
        
        return sendResponse(true, "wifi_scan", scanResult, "WiFi scan not implemented", commandId) ? 
               CMD_NOT_IMPLEMENTED : CMD_ERROR;
    
    } else if (action == "connect") {
        String ssid = params["ssid"] | "";
        String password = params["password"] | "";
        
        if (ssid.length() == 0) {
            return sendError("wifi_connect", "Missing SSID parameter", CMD_INVALID, commandId) ? 
                   CMD_ERROR : CMD_INVALID;
        }
        
        // This would need async implementation
        JsonVariantConst emptyData = JsonVariantConst();
        return sendResponse(false, "wifi_connect", emptyData, 
                           "WiFi connection not implemented via BLE", commandId) ? CMD_SUCCESS : CMD_ERROR;
    
    } else {
        return sendError("wifi", "Unknown WiFi action", CMD_INVALID, commandId) ? 
               CMD_ERROR : CMD_INVALID;
    }
}

// ========================================
// NFC COMMAND HANDLER
// ========================================

BluetoothManager::CommandStatus BluetoothManager::handleNfcCommand(const JsonObjectConst& params, const String& commandId) {
    String action = params["action"] | "";
    int timeout = params["timeout"] | NFCManager::DEFAULT_READ_TIMEOUT_SEC;
    
    if (action == "read_srix") {
        LOG_INFO("BT_MGR", "Reading SRIX tag...");
        NFCManager::TagInfo tagInfo;
        NFCManager::Result result = _nfc.readSRIX(tagInfo, timeout);
        
        if (result.success) {
            JsonDocument tagData = nfcTagToJson(tagInfo);
            return sendResponse(true, "nfc_read_srix", tagData, "SRIX tag read successfully", commandId) ? 
                   CMD_SUCCESS : CMD_ERROR;
        } else {
            return sendError("nfc_read_srix", result.message, result.code, commandId) ? 
                   CMD_ERROR : CMD_INVALID;
        }
    
    } else if (action == "mifare_read") {
        LOG_INFO("BT_MGR", "Reading Mifare tag...");
        NFCManager::TagInfo tagInfo;
        NFCManager::Result result = _nfc.readMifare(tagInfo, timeout);
        
        if (result.success) {
            JsonDocument tagData = nfcTagToJson(tagInfo);
            return sendResponse(true, "nfc_mifare_read", tagData, "Mifare tag read successfully", commandId) ? 
                   CMD_SUCCESS : CMD_ERROR;
        } else {
            return sendError("nfc_mifare_read", result.message, result.code, commandId) ? 
                   CMD_ERROR : CMD_INVALID;
        }
    
    } else if (action == "mifare_uid") {
        LOG_INFO("BT_MGR", "Reading Mifare UID...");
        NFCManager::TagInfo tagInfo;
        NFCManager::Result result = _nfc.readMifareUID(tagInfo, timeout);
        
        if (result.success) {
            JsonDocument tagData = nfcTagToJson(tagInfo, false);  // Don't include dump data
            return sendResponse(true, "nfc_mifare_uid", tagData, "Mifare UID read successfully", commandId) ? 
                   CMD_SUCCESS : CMD_ERROR;
        } else {
            return sendError("nfc_mifare_uid", result.message, result.code, commandId) ? 
                   CMD_ERROR : CMD_INVALID;
        }
    
    } else if (action == "save") {
        String filename = params["filename"] | "";
        
        if (filename.length() == 0) {
            return sendError("nfc_save", "Missing filename parameter", CMD_INVALID, commandId) ? 
                   CMD_ERROR : CMD_INVALID;
        }
        
        if (!_nfc.hasValidData()) {
            return sendError("nfc_save", "No tag data to save", CMD_NO_DATA, commandId) ? 
                   CMD_ERROR : CMD_NO_DATA;
        }
        
        NFCManager::Result result = _nfc.save(filename);
        
        if (result.success) {
            JsonDocument saveData;
            saveData["filename"] = filename;
            saveData["protocol"] = _nfc.protocolToString(_nfc.getCurrentProtocol());
            return sendResponse(true, "nfc_save", saveData, "Tag data saved successfully", commandId) ? 
                   CMD_SUCCESS : CMD_ERROR;
        } else {
            return sendError("nfc_save", result.message, result.code, commandId) ? 
                   CMD_ERROR : CMD_INVALID;
        }
    
    } else if (action == "load") {
        String filename = params["filename"] | "";
        String protocolStr = params["protocol"] | "";
        
        if (filename.length() == 0) {
            return sendError("nfc_load", "Missing filename parameter", CMD_INVALID, commandId) ? 
                   CMD_ERROR : CMD_INVALID;
        }
        
        NFCManager::Protocol protocol = NFCManager::PROTOCOL_UNKNOWN;
        if (protocolStr == "srix") {
            protocol = NFCManager::PROTOCOL_SRIX;
        } else if (protocolStr == "mifare") {
            protocol = NFCManager::PROTOCOL_MIFARE_CLASSIC;
        }
        
        NFCManager::Result result = _nfc.load(filename, protocol);
        
        if (result.success) {
            JsonDocument loadData = nfcTagToJson(_nfc.getCurrentTag());
            return sendResponse(true, "nfc_load", loadData, "Tag data loaded successfully", commandId) ? 
                   CMD_SUCCESS : CMD_ERROR;
        } else {
            return sendError("nfc_load", result.message, result.code, commandId) ? 
                   CMD_ERROR : CMD_INVALID;
        }
    
    } else if (action == "list") {
        String protocolStr = params["protocol"] | "srix";
        NFCManager::Protocol protocol = NFCManager::PROTOCOL_SRIX;
        
        if (protocolStr == "mifare") {
            protocol = NFCManager::PROTOCOL_MIFARE_CLASSIC;
        }
        
        NFCManager::Result result = _nfc.listFiles(protocol);
        
        if (result.success) {
            // For now, send placeholder - would need actual file listing
            JsonDocument fileList;
            fileList["protocol"] = protocolStr;
            fileList["files"] = JsonArray();
            
            return sendResponse(true, "nfc_list", fileList, "File list retrieved", commandId) ? 
                   CMD_SUCCESS : CMD_ERROR;
        } else {
            return sendError("nfc_list", result.message, result.code, commandId) ? 
                   CMD_ERROR : CMD_INVALID;
        }
    
    } else {
        return sendError("nfc", "Unknown NFC action", CMD_INVALID, commandId) ? 
               CMD_ERROR : CMD_INVALID;
    }
}

// ========================================
// SYSTEM COMMAND HANDLER
// ========================================

BluetoothManager::CommandStatus BluetoothManager::handleSystemCommand(const JsonObjectConst& params, const String& commandId) {
    String action = params["action"] | "";
    
    if (action == "info") {
        JsonDocument sysInfo;
        sysInfo["chip_model"] = ESP.getChipModel();
        sysInfo["chip_revision"] = ESP.getChipRevision();
        sysInfo["cpu_freq"] = ESP.getCpuFreqMHz();
        sysInfo["flash_size"] = ESP.getFlashChipSize();
        sysInfo["free_heap"] = ESP.getFreeHeap();
        sysInfo["sketch_size"] = ESP.getSketchSize();
        sysInfo["free_sketch_space"] = ESP.getFreeSketchSpace();
        sysInfo["wifi_connected"] = WiFi.status() == WL_CONNECTED;
        sysInfo["nfc_ready"] = _nfc.isReady();
        sysInfo["ble_ready"] = _ble_handler.isInitialized();
        sysInfo["uptime_ms"] = millis() - _start_time_ms;
        
        return sendResponse(true, "system_info", sysInfo, "System information retrieved", commandId) ? 
               CMD_SUCCESS : CMD_ERROR;
    
    } else if (action == "restart") {
        LOG_INFO("BT_MGR", "System restart requested via BLE");
        sendResponse(true, "system_restart", JsonVariantConst(), "System restarting...", commandId);
        
        delay(1000);  // Allow response to be sent
        ESP.restart();
        
        return CMD_SUCCESS;  // This won't be reached
    
    } else if (action == "heap") {
        JsonDocument heapInfo;
        heapInfo["free_heap"] = ESP.getFreeHeap();
        heapInfo["min_free_heap"] = ESP.getMinFreeHeap();
        heapInfo["heap_size"] = ESP.getHeapSize();
        
        return sendResponse(true, "system_heap", heapInfo, "Heap information retrieved", commandId) ? 
               CMD_SUCCESS : CMD_ERROR;
    
    } else if (action == "stats") {
        JsonDocument stats = getStatistics();
        return sendResponse(true, "system_stats", stats, "Statistics retrieved", commandId) ? 
               CMD_SUCCESS : CMD_ERROR;
    
    } else if (action == "diag") {
        JsonDocument bleDiag = _ble_handler.getDiagnostics();
        JsonDocument sysDiag;
        sysDiag["ble"] = bleDiag;
        sysDiag["bt_manager"] = JsonVariantConst();
        sysDiag["bt_manager"]["initialized"] = _initialized;
        sysDiag["bt_manager"]["enabled"] = _enabled;
        sysDiag["bt_manager"]["commands_processed"] = _commands_processed;
        sysDiag["bt_manager"]["errors_count"] = _errors_count;
        
        return sendResponse(true, "system_diag", sysDiag, "Diagnostics retrieved", commandId) ? 
               CMD_SUCCESS : CMD_ERROR;
    
    } else {
        return sendError("system", "Unknown system action", CMD_INVALID, commandId) ? 
               CMD_ERROR : CMD_INVALID;
    }
}

// ========================================
// FILES COMMAND HANDLER
// ========================================

BluetoothManager::CommandStatus BluetoothManager::handleFilesCommand(const JsonObjectConst& params, const String& commandId) {
    String action = params["action"] | "";
    
    if (action == "list") {
        String protocol = params["protocol"] | "srix";
        
        // Placeholder for file listing
        JsonDocument fileList;
        fileList["protocol"] = protocol;
        fileList["files"] = JsonArray();
        
        return sendResponse(true, "files_list", fileList, "File list retrieved", commandId) ? 
               CMD_SUCCESS : CMD_ERROR;
    
    } else if (action == "download") {
        String filename = params["filename"] | "";
        
        if (filename.length() == 0) {
            return sendError("files_download", "Missing filename parameter", CMD_INVALID, commandId) ? 
                   CMD_ERROR : CMD_INVALID;
        }
        
        return processFileTransfer(filename, commandId);
    
    } else if (action == "delete") {
        String filename = params["filename"] | "";
        String protocol = params["protocol"] | "srix";
        
        if (filename.length() == 0) {
            return sendError("files_delete", "Missing filename parameter", CMD_INVALID, commandId) ? 
                   CMD_ERROR : CMD_INVALID;
        }
        
        NFCManager::Protocol proto = NFCManager::PROTOCOL_SRIX;
        if (protocol == "mifare") {
            proto = NFCManager::PROTOCOL_MIFARE_CLASSIC;
        }
        
        NFCManager::Result result = _nfc.deleteFile(filename, proto);
        
        if (result.success) {
            JsonDocument deleteData;
            deleteData["filename"] = filename;
            deleteData["protocol"] = protocol;
            return sendResponse(true, "files_delete", deleteData, "File deleted successfully", commandId) ? 
                   CMD_SUCCESS : CMD_ERROR;
        } else {
            return sendError("files_delete", result.message, result.code, commandId) ? 
                   CMD_ERROR : CMD_INVALID;
        }
    
    } else {
        return sendError("files", "Unknown files action", CMD_INVALID, commandId) ? 
               CMD_ERROR : CMD_INVALID;
    }
}

// ========================================
// BLUETOOTH COMMAND HANDLER
// ========================================

BluetoothManager::CommandStatus BluetoothManager::handleBtCommand(const JsonObjectConst& params, const String& commandId) {
    String action = params["action"] | "";
    
    if (action == "status") {
        JsonDocument btStatus = _ble_handler.getDiagnostics();
        return sendResponse(true, "bt_status", btStatus, "Bluetooth status retrieved", commandId) ? 
               CMD_SUCCESS : CMD_ERROR;
    
    } else if (action == "stats") {
        JsonDocument btStats = _ble_handler.getStatistics();
        return sendResponse(true, "bt_stats", btStats, "Bluetooth statistics retrieved", commandId) ? 
               CMD_SUCCESS : CMD_ERROR;
    
    } else if (action == "diag") {
        JsonDocument bleDiag = _ble_handler.getDiagnostics();
        return sendResponse(true, "bt_diag", bleDiag, "Bluetooth diagnostics retrieved", commandId) ? 
               CMD_SUCCESS : CMD_ERROR;
    
    } else if (action == "reset_stats") {
        _ble_handler.resetStatistics();
        resetStatistics();
        return sendResponse(true, "bt_reset_stats", JsonVariantConst(), "Bluetooth statistics reset", commandId) ? 
               CMD_SUCCESS : CMD_ERROR;
    
    } else {
        return sendError("bt", "Unknown Bluetooth action", CMD_INVALID, commandId) ? 
               CMD_ERROR : CMD_INVALID;
    }
}

// ========================================
// UTILITY METHODS
// ========================================

String BluetoothManager::statusToString(CommandStatus status) {
    switch (status) {
        case CMD_SUCCESS: return "success";
        case CMD_ERROR: return "error";
        case CMD_INVALID: return "invalid";
        case CMD_TIMEOUT: return "timeout";
        case CMD_NOT_IMPLEMENTED: return "not_implemented";
        case CMD_NO_DATA: return "no_data";
        case CMD_IN_PROGRESS: return "in_progress";
        default: return "unknown";
    }
}

bool BluetoothManager::validateCommand(const JsonDocument& command) {
    // Check for required fields
    if (!command["cmd"].is<const char*>() || String(command["cmd"] | "").length() == 0) {
        return false;
    }
    
    // Optional ID field
    if (command["id"].is<const char*>() && String(command["id"] | "").length() == 0) {
        return false;
    }
    
    return true;
}

JsonDocument BluetoothManager::getSupportedCommands() {
    JsonDocument commands;
    
    JsonArray wifiCmds = commands["wifi"].to<JsonArray>();
    wifiCmds.add("status");
    wifiCmds.add("scan");
    wifiCmds.add("connect");
    
    JsonArray nfcCmds = commands["nfc"].to<JsonArray>();
    nfcCmds.add("read_srix");
    nfcCmds.add("mifare_read");
    nfcCmds.add("mifare_uid");
    nfcCmds.add("save");
    nfcCmds.add("load");
    nfcCmds.add("list");
    
    JsonArray systemCmds = commands["system"].to<JsonArray>();
    systemCmds.add("info");
    systemCmds.add("restart");
    systemCmds.add("heap");
    systemCmds.add("stats");
    systemCmds.add("diag");
    
    JsonArray filesCmds = commands["files"].to<JsonArray>();
    filesCmds.add("list");
    filesCmds.add("download");
    filesCmds.add("delete");
    
    JsonArray btCmds = commands["bt"].to<JsonArray>();
    btCmds.add("status");
    btCmds.add("stats");
    btCmds.add("diag");
    btCmds.add("reset_stats");
    
    return commands;
}

JsonDocument BluetoothManager::getStatistics() {
    JsonDocument stats;
    
    stats["commands_processed"] = _commands_processed;
    stats["errors_count"] = _errors_count;
    stats["uptime_ms"] = _initialized ? (millis() - _start_time_ms) : 0;
    stats["processing_command"] = _processing_command;
    
    if (_processing_command) {
        stats["current_command_duration"] = millis() - _command_start_time;
        stats["current_command_id"] = _current_command_id;
    }
    
    return stats;
}

void BluetoothManager::resetStatistics() {
    _commands_processed = 0;
    _errors_count = 0;
    
    LOG_INFO("BT_MGR", "Statistics reset");
}

// ========================================
// PRIVATE HELPER METHODS
// ========================================

JsonDocument BluetoothManager::nfcTagToJson(const NFCManager::TagInfo& info, bool includeData) {
    JsonDocument tagData;
    
    tagData["protocol"] = info.protocol_name;
    tagData["uid"] = _nfc.uidToString(info.uid, info.uid_length);
    tagData["uid_length"] = info.uid_length;
    tagData["timestamp"] = info.timestamp;
    tagData["valid"] = info.valid;
    
    if (includeData && info.valid) {
        const uint8_t* dumpData = _nfc.getTagDataPointer(info);
        size_t dumpSize = _nfc.getTagDataSize(info);
        tagData["dump_size"] = dumpSize;
        if (dumpData && dumpSize > 0) {
            tagData["dump_base64"] = base64Encode(dumpData, dumpSize);
        } else {
            tagData["dump_base64"] = "";
        }
    }
    
    return tagData;
}

String BluetoothManager::base64Encode(const uint8_t* data, size_t length) {
    static const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String encoded = "";
    encoded.reserve(((length + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < length) {
        uint32_t triple = (data[i] << 16) | (data[i+1] << 8) | data[i+2];
        encoded += base64_chars[(triple >> 18) & 0x3F];
        encoded += base64_chars[(triple >> 12) & 0x3F];
        encoded += base64_chars[(triple >> 6) & 0x3F];
        encoded += base64_chars[triple & 0x3F];
        i += 3;
    }

    if (i < length) {
        uint8_t a = data[i];
        uint8_t b = (i + 1 < length) ? data[i+1] : 0;
        uint32_t triple = (a << 16) | (b << 8);

        encoded += base64_chars[(triple >> 18) & 0x3F];
        encoded += base64_chars[(triple >> 12) & 0x3F];

        if (i + 1 < length) {
            encoded += base64_chars[(triple >> 6) & 0x3F];
            encoded += base64_chars[triple & 0x3F];
        } else {
            encoded += base64_chars[(triple >> 6) & 0x3F];
            encoded += '=';
        }

        if (i + 1 >= length) {
            // If only one byte left, append extra padding
            encoded.setCharAt(encoded.length() - 2, encoded.charAt(encoded.length() - 2));
        }
    }

    // Ensure padding to multiple of 4
    while (encoded.length() % 4 != 0) {
        encoded += '=';
    }

    return encoded;
}

BluetoothManager::CommandStatus BluetoothManager::processFileTransfer(const String& filename, const String& commandId) {
    // Placeholder for file transfer implementation
    JsonDocument fileData;
    fileData["filename"] = filename;
    fileData["size"] = 0;
    fileData["data"] = "File transfer not implemented yet";
    
    return sendResponse(true, "files_download", fileData, "File data retrieved", commandId) ? 
           CMD_SUCCESS : CMD_ERROR;
}

bool BluetoothManager::checkCommandTimeout() {
    if (!_processing_command) {
        return false;
    }
    
    uint32_t elapsed = millis() - _command_start_time;
    return elapsed > (BT_RESPONSE_TIMEOUT_MS);
}

void BluetoothManager::abortCurrentCommand() {
    if (_processing_command) {
        sendError(_current_command_id, "Command processing timeout", CMD_TIMEOUT);
        _processing_command = false;
        _current_command_id = "";
        _command_start_time = 0;
        updateLEDStatus(false);
        _errors_count++;
    }
}

String BluetoothManager::generateCommandId() {
    static uint32_t counter = 0;
    counter++;
    return "bt_" + String(counter);
}

String BluetoothManager::getCommandType(const String& fullCommand) {
    // Check for prefixed commands (backward compatibility)
    if (fullCommand.startsWith("wifi_")) return "wifi";
    if (fullCommand.startsWith("nfc_")) return "nfc";
    if (fullCommand.startsWith("system_")) return "system";
    if (fullCommand.startsWith("files_")) return "files";
    if (fullCommand.startsWith("bt_")) return "bt";
    
    // Check for direct category commands (Bruce format)
    if (fullCommand == "wifi") return "wifi";
    if (fullCommand == "nfc") return "nfc";
    if (fullCommand == "system") return "system";
    if (fullCommand == "files") return "files";
    if (fullCommand == "bt") return "bt";
    
    return "unknown";
}

void BluetoothManager::logCommand(const String& command, CommandStatus status, uint32_t duration) {
    LOG_INFO("BT_MGR", "Command: %s, Status: %s, Duration: %dms", 
             command.c_str(), statusToString(status).c_str(), duration);
}

void BluetoothManager::updateLEDStatus(bool processing) {
    // Priority: Processing > Connection Status
    if (processing) {
        _led.pulse();  // Fast pulse during processing
    } else {
        // Check actual BLE status rather than cached
        if (_ble_handler.isConnected()) {
            uint32_t clientCount = _ble_handler.getConnectedCount();
            if (clientCount > 1) {
                _led.fastBlink();  // Fast blink for multiple clients
            } else {
                _led.on();  // Solid when single client connected
            }
        } else {
            _led.blinking();  // Normal blink when advertising
        }
    }
}