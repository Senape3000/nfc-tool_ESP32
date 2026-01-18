#include "serial_commander.h"
#include <LittleFS.h>

SerialCommander::SerialCommander(WiFiManager& wifi, NFCManager& nfc)
    : _wifi(wifi), _nfc(nfc), _enabled(true) 
{
    LOG_DEBUG("CMD", "SerialCommander initialized");
}

// ============================================
// MAIN COMMAND HANDLER
// ============================================

void SerialCommander::handleCommands() {
    // Early exit if disabled or no data available
    if (!_enabled || !Serial.available()) {
        return;
    }

    // Read and normalize command
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();

    if (cmd.isEmpty()) {
        return;
    }

    LOG_DEBUG("CMD", "Received command: '%s'", cmd.c_str());

    // Parse command into main category and subcommand
    // Example: "wifi status" -> mainCmd="wifi", subCmd="status"
    int spacePos = cmd.indexOf(' ');
    String mainCmd = (spacePos > 0) ? cmd.substring(0, spacePos) : cmd;
    String subCmd = (spacePos > 0) ? cmd.substring(spacePos + 1) : "";

    // Route to appropriate handler
    if (mainCmd == "wifi") {
        handleWifiCommands(subCmd);
    }
    else if (mainCmd == "nfc") {
        handleNfcCommands(subCmd);
    }
    else if (mainCmd == "system" || mainCmd == "sys") {
        handleSystemCommands(subCmd);
    }
    else if (mainCmd == "help" || mainCmd == "?") {
        showHelp();
    }
    else if (mainCmd == "clear") {
        Serial.print(ANSI_CLEAR_SCREEN);
        LOG_DEBUG("CMD", "Terminal cleared");
    }
    else {
        LOG_WARN("CMD", "Unknown command: '%s'", mainCmd.c_str());
        Serial.println("Type 'help' for available commands");
    }
}

// ============================================
// WIFI COMMANDS
// ============================================

void SerialCommander::handleWifiCommands(const String& subcmd) {
    // Status command (default if no subcommand)
    if (subcmd.isEmpty() || subcmd == "status") {
        bool connected = (WiFi.status() == WL_CONNECTED);
        
        LOG_INFO("CMD", "WiFi status: %s", connected ? "CONNECTED" : "NOT CONNECTED");
        Serial.print("Status: ");
        Serial.println(connected ? "CONNECTED" : "NOT CONNECTED");
        
        if (connected) {
            LOG_INFO("CMD", "SSID: %s, IP: %s, RSSI: %d dBm", 
                     WiFi.SSID().c_str(), 
                     WiFi.localIP().toString().c_str(),
                     WiFi.RSSI());
            
            Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
            Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
        }
        return;
    }

    // Scan networks command
    if (subcmd == "scan") {
        LOG_INFO("CMD", "Starting WiFi scan (Serial Commander temporarily disabled)");
        
        // Disable commander during WiFi setup to avoid conflicts
        // WiFi setup requires exclusive serial input access
        disable();
        _wifi.scanAndAskCredentials();
        enable();
        
        LOG_INFO("CMD", "WiFi scan complete (Serial Commander re-enabled)");
        return;
    }

    // Force AP mode
    if (subcmd == "ap") {
        LOG_INFO("CMD", "Starting Access Point mode");
        _wifi.startAP();
        return;
    }

    // Reset credentials and restart
    if (subcmd == "reset") {
        LOG_WARN("CMD", "Clearing WiFi credentials and restarting device");
        Serial.println("Clearing credentials and restarting...");
        
        _wifi.clearCredentials();
        delay(WIFI_RESET_DELAY_MS);
        ESP.restart();
        return;
    }

    // Reconnect to saved network
    if (subcmd == "reconnect") {
        LOG_INFO("CMD", "Attempting reconnection to saved network");
        Serial.println("Attempting reconnection...");
        
        if (_wifi.connectFromSaved()) {
            LOG_INFO("CMD", "Reconnection successful");
            Serial.println("✅ Reconnected!");
        } else {
            LOG_WARN("CMD", "Reconnection failed");
            Serial.println("❌ Reconnection failed");
        }
        return;
    }

    // Add new credential: "wifi add SSID PASSWORD"
    if (subcmd.startsWith("add ")) {
        String params = subcmd.substring(CMD_OFFSET_ADD);
        int spacePos = params.indexOf(' ');
        
        if (spacePos > 0) {
            String ssid = params.substring(0, spacePos);
            String pass = params.substring(spacePos + 1);
            
            LOG_INFO("CMD", "Adding WiFi credential for SSID: %s", ssid.c_str());
            
            WiFiManager::Cred cred;
            cred.ssid = ssid;
            cred.pass = pass;
            
            if (_wifi.addOrUpdateCred(cred)) {
                LOG_INFO("CMD", "Credential saved successfully");
                Serial.printf("✅ Credential saved: %s\n", ssid.c_str());
            } else {
                LOG_ERROR("CMD", "Failed to save credential");
                Serial.println("❌ Failed to save credential");
            }
        } else {
            LOG_WARN("CMD", "Invalid 'wifi add' syntax");
            Serial.println("Usage: wifi add SSID PASSWORD");
        }
        return;
    }

    // Unknown subcommand - show WiFi help
    LOG_DEBUG("CMD", "Unknown WiFi subcommand: '%s'", subcmd.c_str());
    Serial.println("\nWiFi Commands:");
    Serial.println("  wifi status          - Show WiFi status");
    Serial.println("  wifi scan            - Scan and select network");
    Serial.println("  wifi ap              - Force AP mode");
    Serial.println("  wifi reset           - Clear credentials and restart");
    Serial.println("  wifi reconnect       - Reconnect to saved network");
    Serial.println("  wifi add SSID PASS   - Add new credential");
}

// ============================================
// NFC COMMANDS
// ============================================

void SerialCommander::handleNfcCommands(const String& cmd) {
    NFCManager::TagInfo data;
    NFCManager::Result result;

    // ========== READ SRIX TAG ==========
    if (cmd == "srix_read") {
        LOG_INFO("CMD", "Reading SRIX tag (timeout: %d ms)", NFC_READ_TIMEOUT_MS);
        Serial.println("Reading SRIX tag...");
        
        result = _nfc.readSRIX(data, NFC_READ_TIMEOUT_MS);
        
        if (result.success) {
            LOG_INFO("CMD", "SRIX tag read successfully");
            Serial.println("✅ SRIX tag read successfully");
            Serial.println("Protocol: " + data.protocol_name);
            Serial.println("UID: " + _nfc.uidToString(data.uid, data.uid_length));
            
            size_t dumpSize = _nfc.getTagDataSize(data);
            const uint8_t* dumpData = _nfc.getTagDataPointer(data);
            
            Serial.printf("Data size: %d bytes\n", dumpSize);
            Serial.println("Data dump (first 64 bytes):");
            
            printHexDump(dumpData, dumpSize, HEX_DUMP_PREVIEW_BYTES);
            
            Serial.println("\nUse 'nfc save <filename>' to save the dump");
        } else {
            LOG_ERROR("CMD", "SRIX read failed: %s", result.message.c_str());
            Serial.println("❌ " + result.message);
        }
        return;
    }

    // ========== READ MIFARE TAG ==========
    if (cmd == "mifare_read") {
        LOG_INFO("CMD", "Reading Mifare Classic tag");
        NFCManager::TagInfo info;
        result = _nfc.readMifare(info, DEFAULT_MIFARE_READ_TIMEOUT_SEC);
        
        LOG_INFO("CMD", "Mifare read result: %s", result.message.c_str());
        Serial.println(result.message);
        return;
    }

    // ========== READ MIFARE UID ONLY ==========
    if (cmd == "mifare_uid") {
        LOG_INFO("CMD", "Reading Mifare UID only");
        NFCManager::TagInfo info;
        result = _nfc.readMifareUID(info, DEFAULT_MIFARE_UID_TIMEOUT_SEC);
        
        LOG_INFO("CMD", "Mifare UID result: %s", result.message.c_str());
        Serial.println(result.message);
        return;
    }

    // ========== WRITE MIFARE TAG ==========
    if (cmd == "mifare_write") {
        LOG_INFO("CMD", "Writing Mifare Classic tag");
        result = _nfc.writeMifare(_nfc.getCurrentTag(), DEFAULT_MIFARE_WRITE_TIMEOUT_SEC);
        
        LOG_INFO("CMD", "Mifare write result: %s", result.message.c_str());
        Serial.println(result.message);
        return;
    }

    // ========== SAVE TAG DATA TO FILE ==========
    if (cmd.startsWith("save ")) {
        // Extract filename from "save myfile" -> "myfile"
        String filename = cmd.substring(CMD_OFFSET_SAVE);
        filename.trim();
        
        if (filename.isEmpty()) {
            LOG_WARN("CMD", "Save command missing filename");
            Serial.println("❌ Usage: nfc save <filename>");
            return;
        }

        // Check if there's valid tag data to save
        if (!_nfc.hasValidData()) {
            LOG_WARN("CMD", "No tag data available to save");
            Serial.println("⚠️  No data to save");
            Serial.println("Read a tag first with 'nfc read_srix' or 'nfc mifare_read'");
            return;
        }

        // Get current tag data and protocol
        NFCManager::TagInfo currentTag = _nfc.getCurrentTag();
        NFCManager::Protocol currentProto = _nfc.getCurrentProtocol();
        
        LOG_INFO("CMD", "Saving %s dump to '%s'", 
                 _nfc.protocolToString(currentProto).c_str(), 
                 filename.c_str());
        
        Serial.printf("Saving %s dump to '%s'...\n",
                     _nfc.protocolToString(currentProto).c_str(),
                     filename.c_str());

        // Dispatch save operation based on protocol
        switch (currentProto) {
            case NFCManager::PROTOCOL_SRIX:
                result = _nfc.saveSRIX(currentTag, filename);
                break;
                
            case NFCManager::PROTOCOL_MIFARE_CLASSIC:
                result = _nfc.saveMifare(currentTag, filename);
                break;
                
            default:
                LOG_ERROR("CMD", "Cannot save unknown protocol");
                result.success = false;
                result.message = "Unknown protocol: cannot save";
                result.code = -3;
                break;
        }

        // Display result
        if (result.success) {
            LOG_INFO("CMD", "Save successful: %s", result.message.c_str());
            Serial.printf("✅ %s\n", result.message.c_str());
        } else {
            LOG_ERROR("CMD", "Save failed: %s (code: %d)", 
                     result.message.c_str(), result.code);
            Serial.printf("❌ %s (code: %d)\n", result.message.c_str(), result.code);
        }
        return;
    }

    // Handle "save" without arguments
    if (cmd == "save") {
        LOG_WARN("CMD", "Save command requires filename");
        Serial.println("❌ Usage: nfc save <filename>");
        return;
    }

    // ========== LOAD TAG DATA FROM FILE ==========
    if (cmd.startsWith("load ")) {
        // Extract filename from "load myfile.srix" -> "myfile.srix"
        String filename = cmd.substring(CMD_OFFSET_LOAD);
        filename.trim();
        
        if (filename.isEmpty()) {
            LOG_WARN("CMD", "Load command missing filename");
            Serial.println("❌ Usage: nfc load <filename.ext>");
            Serial.println("Extensions: .srix (SRIX4K) | .mfc (Mifare Classic)");
            return;
        }

        // Detect protocol from file extension
        String filenameWithoutExt;
        NFCManager::Protocol protocol = detectProtocolFromExtension(filename, filenameWithoutExt);
        
        if (protocol == NFCManager::PROTOCOL_UNKNOWN) {
            LOG_ERROR("CMD", "Unknown or missing file extension: %s", filename.c_str());
            Serial.println("❌ File extension required!");
            Serial.println("Supported extensions:");
            Serial.println("  .srix - SRIX4K/SRIX512");
            Serial.println("  .mfc  - Mifare Classic");
            Serial.println("Example: nfc load my_tag.srix");
            return;
        }

        LOG_INFO("CMD", "Loading %s dump from '%s'", 
                 _nfc.protocolToString(protocol).c_str(), 
                 filename.c_str());
        
        Serial.printf("Loading %s dump from '%s'...\n",
                     _nfc.protocolToString(protocol).c_str(),
                     filename.c_str());

        // Dispatch load operation based on protocol
        // Note: Pass filename WITHOUT extension to NFCManager
        switch (protocol) {
            case NFCManager::PROTOCOL_SRIX:
                result = _nfc.loadSRIX(data, filenameWithoutExt);
                break;
                
            case NFCManager::PROTOCOL_MIFARE_CLASSIC:
                result = _nfc.loadMifare(data, filenameWithoutExt);
                break;
                
            default:
                result.success = false;
                result.message = "Unsupported file format";
                result.code = -3;
                break;
        }

        // Display result
        if (result.success) {
            LOG_INFO("CMD", "Load successful: %s", result.message.c_str());
            Serial.printf("✅ %s\n", result.message.c_str());
            Serial.printf("Protocol: %s\n", 
                         _nfc.protocolToString(_nfc.getCurrentProtocol()).c_str());
            Serial.printf("UID: %s\n",
                         _nfc.uidToString(data.uid, data.uid_length).c_str());
            Serial.println("Use 'nfc dump' to view data or 'nfc write' to write");
        } else {
            LOG_ERROR("CMD", "Load failed: %s (code: %d)", 
                     result.message.c_str(), result.code);
            Serial.printf("❌ %s (code: %d)\n", result.message.c_str(), result.code);
        }
        return;
    }

    // Handle "load" without arguments
    if (cmd == "load") {
        LOG_WARN("CMD", "Load command requires filename");
        Serial.println("❌ Usage: nfc load <filename.ext>");
        Serial.println("Example: nfc load my_tag.srix");
        return;
    }

    // ========== WAIT FOR TAG DETECTION ==========
    if (cmd.startsWith("wait")) {
        uint32_t timeout_ms = DEFAULT_WAIT_TAG_TIMEOUT_MS;
        
        // Parse optional timeout argument: "wait 10" -> 10 seconds
        if (cmd.startsWith("wait ")) {
            String timeout_str = cmd.substring(CMD_OFFSET_WAIT);
            timeout_str.trim();
            
            if (!timeout_str.isEmpty()) {
                int timeout_sec = timeout_str.toInt();
                
                if (timeout_sec > 0 && timeout_sec <= MAX_WAIT_TAG_TIMEOUT_SEC) {
                    timeout_ms = timeout_sec * 1000;
                } else {
                    LOG_WARN("CMD", "Invalid timeout: %d (must be 1-%d seconds)", 
                            timeout_sec, MAX_WAIT_TAG_TIMEOUT_SEC);
                    Serial.printf("⚠️  Timeout must be 1-%d seconds\n", 
                                 MAX_WAIT_TAG_TIMEOUT_SEC);
                    return;
                }
            }
        }
        
        LOG_INFO("CMD", "Waiting for SRIX tag (timeout: %d ms)", timeout_ms);
        Serial.printf("Waiting for SRIX tag (%d seconds)...\n", timeout_ms / 1000);
        Serial.println("Place tag on reader...");
        
        bool found = _nfc.waitForSRIXTag(timeout_ms);
        
        if (found) {
            LOG_INFO("CMD", "SRIX tag detected");
            Serial.println("✅ Tag detected!");
            Serial.println("Use 'nfc read_srix' to read the tag");
        } else {
            LOG_WARN("CMD", "Tag detection timeout");
            Serial.println("❌ Timeout - No tag found");
        }
        return;
    }

    // Unknown NFC command
    LOG_WARN("CMD", "Unknown NFC command: '%s'", cmd.c_str());
    Serial.println("❌ Unknown NFC command: " + cmd);
    Serial.println("Type 'help' for available commands");
}

// ============================================
// SYSTEM COMMANDS
// ============================================

void SerialCommander::handleSystemCommands(const String& subcmd) {
    // System info (default command)
    if (subcmd.isEmpty() || subcmd == "info") {
        LOG_INFO("CMD", "Displaying system information");
        
        Serial.println("\n========================================");
        Serial.println("ESP32 NFC Tool - System Info");
        Serial.println("========================================");
        Serial.printf("Chip: %s\n", ESP.getChipModel());
        Serial.printf("CPU Freq: %d MHz\n", ESP.getCpuFreqMHz());
        Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
        Serial.printf("Flash Size: %d bytes\n", ESP.getFlashChipSize());
        
        // LittleFS filesystem info
        Serial.println("\nLittleFS:");
        Serial.printf("  Total: %d bytes\n", LittleFS.totalBytes());
        Serial.printf("  Used: %d bytes\n", LittleFS.usedBytes());
        Serial.printf("  Free: %d bytes\n", 
                     LittleFS.totalBytes() - LittleFS.usedBytes());
        Serial.println("========================================\n");
        return;
    }

    // Restart ESP32
    if (subcmd == "restart" || subcmd == "reboot") {
        LOG_WARN("CMD", "System restart requested");
        Serial.printf("Restarting in %d seconds...\n", RESTART_DELAY_MS / 1000);
        delay(RESTART_DELAY_MS);
        ESP.restart();
        return;
    }

    // Format LittleFS - request confirmation
    if (subcmd == "format") {
        LOG_WARN("CMD", "Format requested (awaiting confirmation)");
        Serial.println("⚠️  This will erase ALL data!");
        Serial.println("Type 'system format confirm' to proceed");
        return;
    }

    // Format LittleFS - confirmed
    if (subcmd == "format confirm") {
        LOG_CRITICAL("CMD", "Formatting LittleFS filesystem");
        Serial.println("Formatting LittleFS...");
        
        LittleFS.format();
        
        LOG_INFO("CMD", "Format complete, restarting");
        Serial.println("✅ Format complete. Restarting...");
        delay(FORMAT_DELAY_MS);
        ESP.restart();
        return;
    }

    // Show heap memory
    if (subcmd == "heap") {
        uint32_t freeHeap = ESP.getFreeHeap();
        LOG_INFO("CMD", "Free heap: %d bytes", freeHeap);
        Serial.printf("Free Heap: %d bytes\n", freeHeap);
        return;
    }

    // Unknown system command - show help
    LOG_DEBUG("CMD", "Unknown system subcommand: '%s'", subcmd.c_str());
    Serial.println("\nSystem Commands:");
    Serial.println("  system info      - Show system information");
    Serial.println("  system restart   - Restart ESP32");
    Serial.println("  system format    - Format LittleFS (WARNING!)");
    Serial.println("  system heap      - Show free heap");
}

// ============================================
// HELP DISPLAY
// ============================================

void SerialCommander::showHelp() {
    LOG_INFO("CMD", "Displaying help");
    
    Serial.println("\n========================================");
    Serial.println("ESP32 NFC Tool - Command Reference");
    Serial.println("========================================");
    
    Serial.println("\nWiFi Commands:");
    Serial.println("  wifi status          - Show connection status");
    Serial.println("  wifi scan            - Scan available networks");
    Serial.println("  wifi ap              - Start AP mode");
    Serial.println("  wifi reset           - Clear credentials");
    Serial.println("  wifi reconnect       - Reconnect to saved network");
    Serial.println("  wifi add SSID PASS   - Add new credential");
    
    Serial.println("\nNFC Commands:");
    Serial.println("  nfc srix_read        - Read SRIX4K tag");
    Serial.println("  nfc mifare_read      - Read Mifare Classic");
    Serial.println("  nfc mifare_uid       - Read Mifare UID only");
    Serial.println("  nfc mifare_write     - Write Mifare tag");
    Serial.println("  nfc save <file>      - Save dump to file");
    Serial.println("  nfc load <file.ext>  - Load dump from file");
    Serial.println("  nfc wait [seconds]   - Wait for tag");
    
    Serial.println("\nSystem Commands:");
    Serial.println("  system info          - System information");
    Serial.println("  system restart       - Restart ESP32");
    Serial.println("  system format        - Format filesystem");
    Serial.println("  system heap          - Show free heap");
    
    Serial.println("\nGeneral:");
    Serial.println("  clear                - Clear terminal");
    Serial.println("  help                 - Show this message");
    Serial.println("========================================\n");
}

// ============================================
// UTILITY METHODS
// ============================================

NFCManager::Protocol SerialCommander::detectProtocolFromExtension(const String& filename, String& filenameWithoutExt) {
    if (filename.endsWith(".srix")) {
        // Remove ".srix" extension (5 characters)
        filenameWithoutExt = filename.substring(0, filename.length() - EXT_LEN_SRIX);
        return NFCManager::PROTOCOL_SRIX;
    }
    else if (filename.endsWith(".mfc")) {
        // Remove ".mfc" extension (4 characters)
        filenameWithoutExt = filename.substring(0, filename.length() - EXT_LEN_MFC);
        return NFCManager::PROTOCOL_MIFARE_CLASSIC;
    }
    else {
        // Unknown or missing extension
        filenameWithoutExt = filename;
        return NFCManager::PROTOCOL_UNKNOWN;
    }
}

void SerialCommander::printHexDump(const uint8_t* data, size_t size, size_t maxBytes) {
    // Determine how many bytes to display
    size_t bytesToShow = (maxBytes > 0 && maxBytes < size) ? maxBytes : size;
    
    // Display hex dump in rows of 16 bytes
    for (size_t i = 0; i < bytesToShow; i++) {
        Serial.printf("%02X ", data[i]);
        
        // Newline after every 16 bytes
        if ((i + 1) % HEX_DUMP_BYTES_PER_LINE == 0) {
            Serial.println();
        }
    }
    
    // Add final newline if last row wasn't complete
    if (bytesToShow % HEX_DUMP_BYTES_PER_LINE != 0) {
        Serial.println();
    }
}
