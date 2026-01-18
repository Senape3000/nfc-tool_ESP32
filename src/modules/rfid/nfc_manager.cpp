/**
 * @file nfc_manager.cpp
 * @brief Multi-Protocol NFC Handler Implementation
 */

#include "nfc_manager.h"

// ============================================
// CONSTRUCTOR & INITIALIZATION
// ============================================

NFCManager::NFCManager()
    : _srix_handler(nullptr),
      _mifare_handler(nullptr),
      _initialized(false),
      _current_protocol(PROTOCOL_UNKNOWN) {
    _current_tag.valid = false;
    
    LOG_DEBUG("NFC", "NFCManager constructor initialized");
}

NFCManager::~NFCManager() {
    if (_srix_handler) {
        delete _srix_handler;
        LOG_DEBUG("NFC", "SRIX handler destroyed");
    }
    if (_mifare_handler) {
        delete _mifare_handler;
        LOG_DEBUG("NFC", "Mifare handler destroyed");
    }
}

bool NFCManager::begin() {
    LOG_INFO("NFC", "NFC MANAGER v1.2 INIT");
    
    // Create dump directories if they don't exist
    if (!LittleFS.exists(NFC_DUMP_FOLDER)) {
        if (LittleFS.mkdir(NFC_DUMP_FOLDER)) {
            LOG_DEBUG("NFC", "Created base dump folder: %s", NFC_DUMP_FOLDER);
        } else {
            LOG_ERROR("NFC", "Failed to create dump folder");
        }
    }
    
    if (!LittleFS.exists(NFC_SRIX_DUMP_FOLDER)) {
        if (LittleFS.mkdir(NFC_SRIX_DUMP_FOLDER)) {
            LOG_DEBUG("NFC", "Created SRIX dump folder: %s", NFC_SRIX_DUMP_FOLDER);
        }
    }
    
    if (!LittleFS.exists(NFC_MIFARE_DUMP_FOLDER)) {
        if (LittleFS.mkdir(NFC_MIFARE_DUMP_FOLDER)) {
            LOG_DEBUG("NFC", "Created Mifare dump folder: %s", NFC_MIFARE_DUMP_FOLDER);
        }
    }
    
    _initialized = true;
    
    LOG_INFO("NFC", "NFCManager ready (handlers will be lazy-loaded)");
    LOG_INFO("NFC", "Type 'nfc help' for commands");
    
    return true;
}

// ============================================
// SRIX HANDLER INITIALIZATION
// ============================================

bool NFCManager::beginSRIX() {
    // Already initialized
    if (_srix_handler != nullptr) {
        LOG_DEBUG("SRIX", "Handler already initialized");
        return true;
    }
    
    LOG_INFO("SRIX", "╔═══════════════════════════════════════╗");
    LOG_INFO("SRIX", "║    INITIALIZING SRIX HANDLER       ║");
    LOG_INFO("SRIX", "╚═══════════════════════════════════════╝");
    
    // Create SRIX handler (headless mode)
    LOG_DEBUG("SRIX", "Creating handler...");
    _srix_handler = new SRIXTool(true);
    
    if (!_srix_handler) {
        LOG_ERROR("SRIX", "Failed to allocate SRIX handler");
        return false;
    }
    
    // Verify PN532 is responding
    if (!_srix_handler->getNFC()) {
        LOG_ERROR("SRIX", "PN532 not responding");
        delete _srix_handler;
        _srix_handler = nullptr;
        return false;
    }
    
    LOG_INFO("SRIX", "Handler ready");
    return true;
}

// ============================================
// MIFARE HANDLER INITIALIZATION
// ============================================

bool NFCManager::beginMifare() {
    // Already initialized
    if (_mifare_handler != nullptr) {
        LOG_DEBUG("MIFARE", "Handler already initialized");
        return true;
    }
    
    LOG_INFO("MIFARE", "╔═══════════════════════════════════════╗");
    LOG_INFO("MIFARE", "║   INITIALIZING MIFARE HANDLER      ║");
    LOG_INFO("MIFARE", "╚═══════════════════════════════════════╝");
    
    // Create Mifare handler (non-headless for debugging)
    LOG_DEBUG("MIFARE", "Creating handler...");
    _mifare_handler = new MifareTool(false);
    
    if (!_mifare_handler) {
        LOG_ERROR("MIFARE", "Failed to allocate Mifare handler");
        return false;
    }
    
    // Initialize PN532 hardware
    if (!_mifare_handler->begin()) {
        LOG_ERROR("MIFARE", "PN532 initialization failed");
        delete _mifare_handler;
        _mifare_handler = nullptr;
        return false;
    }
    
    LOG_INFO("MIFARE", "Handler ready");
    return true;
}

// ============================================
// SRIX OPERATIONS
// ============================================

NFCManager::Result NFCManager::readSRIX(TagInfo& info, int timeout_sec) {
    Result result = {false, "", -1};
    
    if (!_initialized) {
        result.message = "NFCManager not initialized (call begin() first)";
        LOG_ERROR("SRIX", "%s", result.message.c_str());
        return result;
    }
    
    // Lazy init: initialize SRIX if needed
    if (!beginSRIX()) {
        result.message = "Failed to initialize SRIX handler";
        LOG_ERROR("SRIX", "%s", result.message.c_str());
        return result;
    }
    
    int timeout_ms = timeout_sec * 1000;
    LOG_INFO("SRIX", "Reading tag (timeout: %d seconds)...", timeout_sec);
    
    String json = _srix_handler->read_tag_headless(timeout_ms);
    
    if (json.isEmpty()) {
        result.message = "Timeout: No SRIX tag found";
        result.code = -1;
        LOG_WARN("SRIX", "%s", result.message.c_str());
        return result;
    }
    
    // Parse JSON response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        result.message = "Failed to parse tag data";
        result.code = -2;
        LOG_ERROR("SRIX", "JSON parse error: %s", error.c_str());
        return result;
    }
    
    // Fill TagInfo
    info.protocol = PROTOCOL_SRIX;
    info.protocol_name = "SRIX4K";
    info.valid = true;
    info.timestamp = millis();
    
    // Parse UID (8 bytes)
    String uid_str = doc["uid"].as<String>();
    uid_str.replace(" ", "");
    stringToUid(uid_str, info.uid, info.uid_length);
    
    // Parse dump (512 bytes)
    String dump_hex = doc["data"].as<String>();
    hexToDump(dump_hex, info.data.srix.dump, SRIX_DUMP_SIZE);
    
    // Save as current
    _current_tag = info;
    _current_protocol = PROTOCOL_SRIX;
    
    result.success = true;
    result.message = "SRIX tag read successfully";
    result.code = 0;
    
    LOG_INFO("SRIX", "Read successful - UID: %s", uidToString(info.uid, info.uid_length).c_str());
    
    return result;
}

NFCManager::Result NFCManager::writeSRIX(const TagInfo& info, int timeout_sec) {
    Result result = {false, "", -1};
    
    if (!_initialized) {
        result.message = "NFCManager not initialized (call begin() first)";
        LOG_ERROR("SRIX", "%s", result.message.c_str());
        return result;
    }
    
    // Lazy init
    if (!beginSRIX()) {
        result.message = "Failed to initialize SRIX handler";
        LOG_ERROR("SRIX", "%s", result.message.c_str());
        return result;
    }
    
    if (!info.valid || info.protocol != PROTOCOL_SRIX) {
        result.message = "Invalid SRIX data";
        result.code = -2;
        LOG_ERROR("SRIX", "%s", result.message.c_str());
        return result;
    }
    
    // Copy data to SRIX handler
    memcpy(_srix_handler->getDump(), info.data.srix.dump, SRIX_DUMP_SIZE);
    memcpy(_srix_handler->getUID(), info.uid, 8);
    _srix_handler->setDumpValidFromLoad();
    
    LOG_INFO("SRIX", "Writing tag (timeout: %d seconds)...", timeout_sec);
    
    int write_result = _srix_handler->write_tag_headless(timeout_sec);
    
    if (write_result == 0) {
        result.success = true;
        result.message = "SRIX tag written and verified";
        result.code = 0;
        LOG_INFO("SRIX", "Write complete and verified");
    } else if (write_result > 0) {
        result.success = false;
        result.message = "Tag lost at block: " + String(write_result);
        result.code = write_result;
        LOG_ERROR("SRIX", "Tag lost at block %d", write_result);
    } else {
        result.success = false;
        result.code = write_result;
        
        switch (write_result) {
            case -1: result.message = "Timeout waiting for tag"; break;
            case -2: result.message = "No data loaded"; break;
            case -5: result.message = "Write failed"; break;
            case -6: result.message = "NFC hardware error"; break;
            default: result.message = "Unknown error"; break;
        }
        
        LOG_ERROR("SRIX", "Write failed: %s (code: %d)", result.message.c_str(), write_result);
    }
    
    return result;
}

NFCManager::Result NFCManager::saveSRIX(const TagInfo& info, String filename) {
    Result result = {false, "", -1};
    
    if (!_initialized) {
        result.message = "NFCManager not initialized";
        LOG_ERROR("SRIX", "%s", result.message.c_str());
        return result;
    }
    
    // saveSRIX doesn't need hardware, works only on memory
    // But if we never initialized, create handler just for file ops
    if (!_srix_handler) {
        _srix_handler = new SRIXTool(true);
        if (!_srix_handler) {
            result.message = "Failed to create SRIX handler";
            LOG_ERROR("SRIX", "%s", result.message.c_str());
            return result;
        }
    }
    
    if (!info.valid || info.protocol != PROTOCOL_SRIX) {
        result.message = "Invalid SRIX data to save";
        LOG_ERROR("SRIX", "%s", result.message.c_str());
        return result;
    }
    
    // Copy data to SRIX handler
    memcpy(_srix_handler->getDump(), info.data.srix.dump, SRIX_DUMP_SIZE);
    memcpy(_srix_handler->getUID(), info.uid, 8);
    _srix_handler->setDumpValidFromLoad();
    
    LOG_DEBUG("SRIX", "Saving to file: %s", filename.c_str());
    
    String filepath = _srix_handler->save_file_headless(filename);
    
    if (filepath.isEmpty()) {
        result.message = "Failed to save file";
        LOG_ERROR("SRIX", "File save failed: %s", filename.c_str());
        return result;
    }
    
    result.success = true;
    result.message = "Saved to " + filepath;
    result.code = 0;
    
    LOG_INFO("SRIX", "File saved: %s", filepath.c_str());
    
    return result;
}

NFCManager::Result NFCManager::loadSRIX(TagInfo& info, String filename) {
    Result result = {false, "", -1};
    
    if (!_initialized) {
        result.message = "NFCManager not initialized";
        LOG_ERROR("SRIX", "%s", result.message.c_str());
        return result;
    }
    
    // loadSRIX doesn't need hardware, works only on file
    if (!_srix_handler) {
        _srix_handler = new SRIXTool(true);
        if (!_srix_handler) {
            result.message = "Failed to create SRIX handler";
            LOG_ERROR("SRIX", "%s", result.message.c_str());
            return result;
        }
    }
    
    LOG_DEBUG("SRIX", "Loading file: %s", filename.c_str());
    
    int load_result = _srix_handler->load_file_headless(filename);
    
    if (load_result != 0) {
        switch (load_result) {
            case -1: result.message = "Failed to open file"; break;
            case -2: result.message = "File not found"; break;
            case -3: result.message = "Incomplete or corrupt file"; break;
            default: result.message = "Unknown error"; break;
        }
        result.code = load_result;
        LOG_ERROR("SRIX", "Load failed: %s (code: %d)", result.message.c_str(), load_result);
        return result;
    }
    
    // Fill TagInfo from loaded data
    info.protocol = PROTOCOL_SRIX;
    info.protocol_name = "SRIX4K";
    info.valid = true;
    info.timestamp = millis();
    info.uid_length = 8;
    memcpy(info.uid, _srix_handler->getUID(), 8);
    memcpy(info.data.srix.dump, _srix_handler->getDump(), SRIX_DUMP_SIZE);
    
    // Save as current
    _current_tag = info;
    _current_protocol = PROTOCOL_SRIX;
    
    result.success = true;
    result.message = "File loaded successfully";
    result.code = 0;
    
    LOG_INFO("SRIX", "File loaded: %s (UID: %s)", 
             filename.c_str(), 
             uidToString(info.uid, info.uid_length).c_str());
    
    return result;
}

NFCManager::Result NFCManager::writeSRIXBlock(uint8_t block_num, const uint8_t* data) {
    Result result = {false, "", -1};
    
    if (!_initialized) {
        result.message = "NFCManager not initialized";
        LOG_ERROR("SRIX", "%s", result.message.c_str());
        return result;
    }
    
    // Lazy init
    if (!beginSRIX()) {
        result.message = "Failed to initialize SRIX handler";
        LOG_ERROR("SRIX", "%s", result.message.c_str());
        return result;
    }
    
    if (block_num > 127) {
        result.message = "Invalid block number (max 127)";
        result.code = -4;
        LOG_ERROR("SRIX", "Invalid block: %d", block_num);
        return result;
    }
    
    LOG_DEBUG("SRIX", "Writing block %d: %02X %02X %02X %02X",
             block_num, data[0], data[1], data[2], data[3]);
    
    int write_result = _srix_handler->write_single_block_headless(block_num, data);
    
    if (write_result == 0) {
        result.success = true;
        result.message = "Block written and verified";
        result.code = 0;
        LOG_DEBUG("SRIX", "Block %d written successfully", block_num);
    } else if (write_result > 0) {
        result.success = false;
        result.message = "Tag lost";
        result.code = write_result;
        LOG_ERROR("SRIX", "Tag lost during block %d write", block_num);
    } else {
        result.success = false;
        result.message = "Write failed";
        result.code = write_result;
        LOG_ERROR("SRIX", "Block %d write failed (code: %d)", block_num, write_result);
    }
    
    return result;
}

NFCManager::Result NFCManager::writeSRIXBlocksSelective(const std::vector<uint8_t>& block_numbers) {
    Result result = {false, "", -1};
    
    if (block_numbers.empty()) {
        result.message = "No blocks specified";
        result.code = -3;
        LOG_ERROR("SRIX", "%s", result.message.c_str());
        return result;
    }
    
    if (!hasValidData()) {
        result.message = "No loaded data to write from";
        result.code = -2;
        LOG_ERROR("SRIX", "%s", result.message.c_str());
        return result;
    }
    
    LOG_INFO("SRIX", "========================================");
    LOG_INFO("SRIX", "SELECTIVE WRITE: %d blocks", block_numbers.size());
    LOG_INFO("SRIX", "========================================");
    
    int blocks_written = 0;
    
    for (size_t i = 0; i < block_numbers.size(); i++) {
        uint8_t block_num = block_numbers[i];
        
        if (block_num > 127) {
            LOG_WARN("SRIX", "Skipping invalid block %d", block_num);
            continue;
        }
        
        // Get data from current tag
        const uint8_t* block_data = _current_tag.data.srix.dump + (block_num * 4);
        
        LOG_DEBUG("SRIX", "[%d/%d] Writing block #%d...", 
                 i + 1, block_numbers.size(), block_num);
        
        // Write block
        Result block_result = writeSRIXBlock(block_num, block_data);
        
        if (block_result.success || (block_result.code >= 0 && block_result.code <= 2)) {
            blocks_written++;
            
            if (block_result.code == 0) {
                LOG_INFO("SRIX", "Block %d written & verified (%d/%d)",
                        block_num, blocks_written, block_numbers.size());
            } else if (block_result.code == 1) {
                LOG_WARN("SRIX", "Block %d written (verify mismatch) (%d/%d)",
                        block_num, blocks_written, block_numbers.size());
            } else if (block_result.code == 2) {
                LOG_INFO("SRIX", "Block %d written (verify skipped) (%d/%d)",
                        block_num, blocks_written, block_numbers.size());
            }
        } else {
            // Real error - stop everything
            LOG_ERROR("SRIX", "Block %d FAILED: %s (code=%d)",
                     block_num, block_result.message.c_str(), block_result.code);
            
            result.success = false;
            result.message = "Failed at block " + String(block_num) + ": " +
                           block_result.message + " (code=" + String(block_result.code) + ")";
            result.code = block_result.code;
            return result;
        }
    }
    
    LOG_INFO("SRIX", "========================================");
    LOG_INFO("SRIX", "COMPLETE: %d/%d blocks written", blocks_written, block_numbers.size());
    LOG_INFO("SRIX", "========================================");
    
    result.success = true;
    result.message = "Successfully wrote " + String(blocks_written) + " blocks";
    result.code = 0;
    
    return result;
}

bool NFCManager::waitForSRIXTag(uint32_t timeout_ms) {
    // Check basic initialization
    if (!_initialized) {
        LOG_ERROR("NFC", "NFCManager not initialized (call begin() first)");
        return false;
    }
    
    // Lazy init: initialize SRIX if needed
    if (!beginSRIX()) {
        LOG_ERROR("NFC", "Failed to initialize SRIX handler");
        return false;
    }
    
    LOG_DEBUG("SRIX", "Waiting for tag (timeout: %lu ms)...", timeout_ms);
    
    // Call waitForTagHeadless
    return _srix_handler->waitForTagHeadless(timeout_ms);
}
// ============================================
// MIFARE OPERATIONS
// ============================================

NFCManager::Result NFCManager::readMifare(TagInfo& info, int timeout_sec) {
    Result result = {false, "", -1};
    
    if (!_initialized) {
        result.message = "NFCManager not initialized (call begin() first)";
        LOG_ERROR("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    // Lazy init: initialize Mifare if needed
    if (!beginMifare()) {
        result.message = "Failed to initialize Mifare handler";
        LOG_ERROR("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    int timeout_ms = timeout_sec * 1000;
    LOG_INFO("MIFARE", "Reading tag (timeout: %d seconds)...", timeout_sec);
    
    String json = _mifare_handler->read_tag_headless(timeout_ms);
    
    if (json.isEmpty()) {
        result.message = "Timeout: No Mifare tag found";
        result.code = -1;
        LOG_WARN("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    // Parse JSON response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        result.message = "Failed to parse tag data";
        result.code = -2;
        LOG_ERROR("MIFARE", "JSON parse error: %s", error.c_str());
        return result;
    }
    
    // Fill TagInfo
    info.protocol = PROTOCOL_MIFARE_CLASSIC;
    info.protocol_name = doc["card_type"].as<String>();
    info.valid = true;
    info.timestamp = millis();
    
    // Parse UID
    String uid_str = doc["uid"].as<String>();
    uid_str.replace(" ", "");
    stringToUid(uid_str, info.uid, info.uid_length);
    
    // Copy dump from handler
    int total_blocks = _mifare_handler->getTotalBlocks();
    int blocks_read = _mifare_handler->getBlocksRead();
    
    if (_mifare_handler->getCardType() == MifareTool::CARD_MIFARE_1K) {
        memcpy(info.data.mifare_classic.dump, _mifare_handler->getDump(), MIFARE_1K_DUMP_SIZE);
        info.data.mifare_classic.sectors = 16;
    } else {
        // 4K card - TagInfo supports only 1K, truncate or expand structure if needed
        memcpy(info.data.mifare_classic.dump, _mifare_handler->getDump(), MIFARE_1K_DUMP_SIZE);
        info.data.mifare_classic.sectors = 16; // Limit to 16 sectors for now
        LOG_WARN("MIFARE", "4K card detected but TagInfo limited to 1K");
    }
    
    // Save as current
    _current_tag = info;
    _current_protocol = PROTOCOL_MIFARE_CLASSIC;
    
    result.success = true;
    result.message = "Mifare tag read successfully";
    result.code = 0;
    
    LOG_INFO("MIFARE", "Read successful - UID: %s, Blocks: %d/%d",
             uidToString(info.uid, info.uid_length).c_str(),
             blocks_read, total_blocks);
    
    return result;
}

NFCManager::Result NFCManager::readMifareUID(TagInfo& info, int timeout_sec) {
    Result result = {false, "", -1};
    
    if (!_initialized) {
        result.message = "NFCManager not initialized";
        LOG_ERROR("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    // Lazy init
    if (!beginMifare()) {
        result.message = "Failed to initialize Mifare handler";
        LOG_ERROR("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    int timeout_ms = timeout_sec * 1000;
    LOG_INFO("MIFARE", "Reading UID only (timeout: %d seconds)...", timeout_sec);
    
    String json = _mifare_handler->read_uid_headless(timeout_ms);
    
    if (json.isEmpty()) {
        result.message = "Timeout: No Mifare tag found";
        result.code = -1;
        LOG_WARN("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    // Parse JSON
    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        result.message = "Failed to parse UID data";
        result.code = -2;
        LOG_ERROR("MIFARE", "JSON parse error");
        return result;
    }
    
    // Fill TagInfo (UID only, no dump)
    info.protocol = PROTOCOL_MIFARE_CLASSIC;
    info.protocol_name = doc["card_type"].as<String>();
    info.valid = true;
    info.timestamp = millis();
    
    String uid_str = doc["uid"].as<String>();
    uid_str.replace(" ", "");
    stringToUid(uid_str, info.uid, info.uid_length);
    
    // No dump data
    memset(info.data.mifare_classic.dump, 0, sizeof(info.data.mifare_classic.dump));
    info.data.mifare_classic.sectors = 0;
    
    result.success = true;
    result.message = "UID read successfully";
    result.code = 0;
    
    LOG_INFO("MIFARE", "UID read successful: %s", uidToString(info.uid, info.uid_length).c_str());
    
    return result;
}

NFCManager::Result NFCManager::writeMifare(const TagInfo& info, int timeout_sec) {
    Result result = {false, "", -1};
    
    if (!_initialized) {
        result.message = "NFCManager not initialized";
        LOG_ERROR("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    // Lazy init
    if (!beginMifare()) {
        result.message = "Failed to initialize Mifare handler";
        LOG_ERROR("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    if (!info.valid || info.protocol != PROTOCOL_MIFARE_CLASSIC) {
        result.message = "Invalid Mifare data";
        result.code = -2;
        LOG_ERROR("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    // Copy data to handler
    memcpy(_mifare_handler->getDump(), info.data.mifare_classic.dump, MIFARE_1K_DUMP_SIZE);
    memcpy(_mifare_handler->getUID(), info.uid, info.uid_length);
    _mifare_handler->setDumpValidFromLoad();
    
    LOG_INFO("MIFARE", "Writing tag (timeout: %d seconds)...", timeout_sec);
    
    int write_result = _mifare_handler->write_tag_headless(timeout_sec);
    
    if (write_result == MifareTool::SUCCESS) {
        result.success = true;
        result.message = "Mifare tag written successfully";
        result.code = 0;
        LOG_INFO("MIFARE", "Write complete");
    } else {
        result.success = false;
        result.code = write_result;
        
        switch (write_result) {
            case MifareTool::ERROR_NO_TAG:
                result.message = "Timeout: No tag found";
                break;
            case MifareTool::ERROR_AUTH_FAILED:
                result.message = "Authentication failed";
                break;
            case MifareTool::ERROR_WRITE_FAILED:
                result.message = "Write failed";
                break;
            case MifareTool::ERROR_INVALID_DATA:
                result.message = "Invalid data or card type mismatch";
                break;
            default:
                result.message = "Unknown error";
                break;
        }
        
        LOG_ERROR("MIFARE", "Write failed: %s (code: %d)", result.message.c_str(), write_result);
    }
    
    return result;
}

NFCManager::Result NFCManager::cloneMifareUID(const TagInfo& info, int timeout_sec) {
    Result result = {false, "", -1};
    
    if (!_initialized) {
        result.message = "NFCManager not initialized";
        LOG_ERROR("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    // Lazy init
    if (!beginMifare()) {
        result.message = "Failed to initialize Mifare handler";
        LOG_ERROR("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    if (!info.valid || info.protocol != PROTOCOL_MIFARE_CLASSIC) {
        result.message = "Invalid Mifare data";
        result.code = -2;
        LOG_ERROR("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    // Copy UID to handler
    memcpy(_mifare_handler->getUID(), info.uid, info.uid_length);
    _mifare_handler->setDumpValidFromLoad();
    
    LOG_INFO("MIFARE", "Cloning UID: %s (timeout: %d seconds)...",
             uidToString(info.uid, info.uid_length).c_str(), timeout_sec);
    
    int clone_result = _mifare_handler->clone_uid_headless(timeout_sec);
    
    if (clone_result == MifareTool::SUCCESS) {
        result.success = true;
        result.message = "UID cloned successfully";
        result.code = 0;
        LOG_INFO("MIFARE", "Clone complete");
    } else {
        result.success = false;
        result.code = clone_result;
        
        switch (clone_result) {
            case MifareTool::ERROR_NO_TAG:
                result.message = "Timeout: No tag found";
                break;
            case MifareTool::ERROR_WRITE_FAILED:
                result.message = "Clone failed (not a magic card?)";
                break;
            case MifareTool::ERROR_INVALID_DATA:
                result.message = "Invalid UID data";
                break;
            default:
                result.message = "Unknown error";
                break;
        }
        
        LOG_ERROR("MIFARE", "Clone failed: %s (code: %d)", result.message.c_str(), clone_result);
    }
    
    return result;
}

NFCManager::Result NFCManager::saveMifare(const TagInfo& info, String filename) {
    Result result = {false, "", -1};
    
    if (!_initialized) {
        result.message = "NFCManager not initialized";
        LOG_ERROR("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    // saveMifare doesn't need active hardware
    if (!_mifare_handler) {
        _mifare_handler = new MifareTool(true);
        if (!_mifare_handler || !_mifare_handler->begin()) {
            result.message = "Failed to create Mifare handler";
            LOG_ERROR("MIFARE", "%s", result.message.c_str());
            return result;
        }
    }
    
    if (!info.valid || info.protocol != PROTOCOL_MIFARE_CLASSIC) {
        result.message = "Invalid Mifare data to save";
        LOG_ERROR("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    // Copy data to handler
    memcpy(_mifare_handler->getDump(), info.data.mifare_classic.dump, MIFARE_1K_DUMP_SIZE);
    memcpy(_mifare_handler->getUID(), info.uid, info.uid_length);
    _mifare_handler->setDumpValidFromLoad();
    
    LOG_DEBUG("MIFARE", "Saving to file: %s", filename.c_str());
    
    String filepath = _mifare_handler->save_file_headless(filename);
    
    if (filepath.isEmpty()) {
        result.message = "Failed to save file";
        LOG_ERROR("MIFARE", "File save failed: %s", filename.c_str());
        return result;
    }
    
    result.success = true;
    result.message = "Saved to " + filepath;
    result.code = 0;
    
    LOG_INFO("MIFARE", "File saved: %s", filepath.c_str());
    
    return result;
}

NFCManager::Result NFCManager::loadMifare(TagInfo& info, String filename) {
    Result result = {false, "", -1};
    
    if (!_initialized) {
        result.message = "NFCManager not initialized";
        LOG_ERROR("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    // loadMifare doesn't need active hardware
    if (!_mifare_handler) {
        _mifare_handler = new MifareTool(true);
        if (!_mifare_handler || !_mifare_handler->begin()) {
            result.message = "Failed to create Mifare handler";
            LOG_ERROR("MIFARE", "%s", result.message.c_str());
            return result;
        }
    }
    
    LOG_DEBUG("MIFARE", "Loading file: %s", filename.c_str());
    
    int load_result = _mifare_handler->load_file_headless(filename);
    
    if (load_result != MifareTool::SUCCESS) {
        switch (load_result) {
            case MifareTool::ERROR_FILE_ERROR:
                result.message = "File not found or cannot open";
                break;
            case MifareTool::ERROR_INVALID_DATA:
                result.message = "Invalid or corrupt file";
                break;
            default:
                result.message = "Unknown error";
                break;
        }
        result.code = load_result;
        LOG_ERROR("MIFARE", "Load failed: %s (code: %d)", result.message.c_str(), load_result);
        return result;
    }
    
    // Fill TagInfo from loaded data
    info.protocol = PROTOCOL_MIFARE_CLASSIC;
    info.valid = true;
    info.timestamp = millis();
    info.uid_length = _mifare_handler->getUIDLength();
    memcpy(info.uid, _mifare_handler->getUID(), info.uid_length);
    memcpy(info.data.mifare_classic.dump, _mifare_handler->getDump(), MIFARE_1K_DUMP_SIZE);
    
    if (_mifare_handler->getCardType() == MifareTool::CARD_MIFARE_1K) {
        info.protocol_name = "Mifare Classic 1K";
        info.data.mifare_classic.sectors = 16;
    } else {
        info.protocol_name = "Mifare Classic 4K";
        info.data.mifare_classic.sectors = 40; // Note: TagInfo limited to 1K
        LOG_WARN("MIFARE", "4K card loaded but TagInfo limited to 1K");
    }
    
    // Save as current
    _current_tag = info;
    _current_protocol = PROTOCOL_MIFARE_CLASSIC;
    
    result.success = true;
    result.message = "File loaded successfully";
    result.code = 0;
    
    LOG_INFO("MIFARE", "File loaded: %s (UID: %s)", 
             filename.c_str(),
             uidToString(info.uid, info.uid_length).c_str());
    
    return result;
}

NFCManager::Result NFCManager::writeMifareBlocksSelective(const std::vector<uint8_t>& block_numbers) {
    Result result = {false, "", -1};
    
    if (block_numbers.empty()) {
        result.message = "No blocks specified";
        result.code = -3;
        LOG_ERROR("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    if (!hasValidData()) {
        result.message = "No loaded data to write from";
        result.code = -2;
        LOG_ERROR("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    // Lazy init
    if (!beginMifare()) {
        result.message = "Failed to initialize Mifare handler";
        LOG_ERROR("MIFARE", "%s", result.message.c_str());
        return result;
    }
    
    LOG_INFO("MIFARE", "SELECTIVE WRITE: %d blocks", block_numbers.size());
    
    int blocks_written = 0;
    
    for (size_t i = 0; i < block_numbers.size(); i++) {
        uint8_t block_num = block_numbers[i];
        
        if (block_num >= 64) {
            LOG_WARN("MIFARE", "Skipping invalid block %d", block_num);
            continue;
        }
        
        LOG_DEBUG("MIFARE", "[%d/%d] Writing block %d...",
                 i + 1, block_numbers.size(), block_num);
        
        // Write block using handler's write_single_block_headless
        int write_result = _mifare_handler->write_single_block_headless(block_num);
        
        if (write_result == MifareTool::SUCCESS) {
            blocks_written++;
            LOG_INFO("MIFARE", "Block %d written [%d/%d]",
                    block_num, blocks_written, block_numbers.size());
        } else {
            // Real error - stop everything
            LOG_ERROR("MIFARE", "Block %d FAILED (code=%d)", block_num, write_result);
            
            result.success = false;
            result.message = "Failed at block " + String(block_num) + 
                           " (code=" + String(write_result) + ")";
            result.code = write_result;
            return result;
        }
    }
    
    LOG_INFO("MIFARE", "COMPLETE: %d/%d blocks written", blocks_written, block_numbers.size());
    
    result.success = true;
    result.message = "Successfully wrote " + String(blocks_written) + " blocks";
    result.code = 0;
    
    return result;
}
// ============================================
// MEMORY MANAGEMENT
// ============================================

void NFCManager::clearCurrentTag() {
    memset(&_current_tag, 0, sizeof(TagInfo));
    _current_tag.valid = false;
    _current_protocol = PROTOCOL_UNKNOWN;
    
    LOG_INFO("NFC", "Memory cleared");
}

void NFCManager::restoreCurrentTag(const TagInfo& info) {
    _current_tag = info;
    _current_protocol = info.protocol;
    
    // Critical: Restore dump to handler as well
    if (info.protocol == PROTOCOL_MIFARE_CLASSIC && _mifare_handler) {
        memcpy(_mifare_handler->getDump(), info.data.mifare_classic.dump, MIFARE_1K_DUMP_SIZE);
        memcpy(_mifare_handler->getUID(), info.uid, info.uid_length);
        _mifare_handler->setDumpValidFromLoad();
        LOG_INFO("NFC", "Restored Mifare dump to handler");
    } else if (info.protocol == PROTOCOL_SRIX && _srix_handler) {
        memcpy(_srix_handler->getDump(), info.data.srix.dump, SRIX_DUMP_SIZE);
        memcpy(_srix_handler->getUID(), info.uid, 8);
        _srix_handler->setDumpValidFromLoad();
        LOG_INFO("NFC", "Restored SRIX dump to handler");
    }
    
    LOG_DEBUG("NFC", "Current tag restored (protocol: %s)", 
             protocolToString(info.protocol).c_str());
}

// ============================================
// FILE OPERATIONS
// ============================================

NFCManager::Result NFCManager::save(String filename) {
    Result result = {false, "", -1};
    
    if (!_initialized) {
        result.message = "NFCManager not initialized";
        LOG_ERROR("NFC", "%s", result.message.c_str());
        return result;
    }
    
    if (!_current_tag.valid) {
        result.message = "No valid data to save (read/load a tag first)";
        result.code = -2;
        LOG_ERROR("NFC", "%s", result.message.c_str());
        return result;
    }
    
    // Dispatch based on current protocol
    LOG_INFO("NFC", "Saving %s dump...", protocolToString(_current_protocol).c_str());
    
    switch (_current_protocol) {
        case PROTOCOL_SRIX:
            return saveSRIX(_current_tag, filename);
            
        case PROTOCOL_MIFARE_CLASSIC:
            return saveMifare(_current_tag, filename);
            
        case PROTOCOL_NTAG:
            // Future: return saveNTAG(_current_tag, filename);
            result.message = "NTAG save not yet implemented";
            result.code = -99;
            LOG_WARN("NFC", "%s", result.message.c_str());
            return result;
            
        default:
            result.message = "Unknown protocol: cannot save";
            result.code = -3;
            LOG_ERROR("NFC", "%s", result.message.c_str());
            return result;
    }
}

NFCManager::Result NFCManager::load(String filename, Protocol protocol) {
    Result result = {false, "", -1};
    
    if (!_initialized) {
        result.message = "NFCManager not initialized";
        LOG_ERROR("NFC", "%s", result.message.c_str());
        return result;
    }
    
    // If protocol not specified, try to deduce from extension
    if (protocol == PROTOCOL_UNKNOWN) {
        if (filename.endsWith(".srix")) {
            protocol = PROTOCOL_SRIX;
        } else if (filename.endsWith(".mfc")) {
            protocol = PROTOCOL_MIFARE_CLASSIC;
        } else {
            result.message = "Cannot detect protocol (specify or use correct extension)";
            LOG_ERROR("NFC", "%s", result.message.c_str());
            return result;
        }
    }
    
    LOG_INFO("NFC", "Loading %s dump...", protocolToString(protocol).c_str());
    
    // Dispatch based on protocol
    switch (protocol) {
        case PROTOCOL_SRIX: {
            TagInfo info;
            result = loadSRIX(info, filename);
            if (result.success) {
                _current_tag = info;
                _current_protocol = PROTOCOL_SRIX;
            }
            return result;
        }
        
        case PROTOCOL_MIFARE_CLASSIC: {
            TagInfo info;
            result = loadMifare(info, filename);
            if (result.success) {
                _current_tag = info;
                _current_protocol = PROTOCOL_MIFARE_CLASSIC;
            }
            return result;
        }
        
        case PROTOCOL_NTAG:
            // Future: return loadNTAG(_current_tag, filename);
            result.message = "NTAG load not yet implemented";
            result.code = -99;
            LOG_WARN("NFC", "%s", result.message.c_str());
            return result;
            
        default:
            result.message = "Unsupported protocol";
            result.code = -3;
            LOG_ERROR("NFC", "%s", result.message.c_str());
            return result;
    }
}

NFCManager::Result NFCManager::listFiles(Protocol protocol) {
    Result result = {false, "", -1};
    
    String folder = getProtocolFolder(protocol);
    String extension = getFileExtension(protocol);
    
    if (!LittleFS.exists(folder)) {
        result.message = "Dump folder not found";
        LOG_ERROR("NFC", "Folder not found: %s", folder.c_str());
        return result;
    }
    
    File dir = LittleFS.open(folder);
    if (!dir || !dir.isDirectory()) {
        result.message = "Failed to open directory";
        LOG_ERROR("NFC", "Cannot open directory: %s", folder.c_str());
        return result;
    }
    
    LOG_INFO("NFC", "╔═══════════════════════════════════════╗");
    LOG_INFO("NFC", "║ %s FILES", protocolToString(protocol).c_str());
    LOG_INFO("NFC", "╠═══════════════════════════════════════╣");
    
    int count = 0;
    File file = dir.openNextFile();
    
    while (file) {
        if (!file.isDirectory()) {
            String name = String(file.name());
            if (extension.isEmpty() || name.endsWith(extension)) {
                LOG_INFO("NFC", " %s (%d bytes)", name.c_str(), file.size());
                count++;
            }
        }
        file = dir.openNextFile();
    }
    
    if (count == 0) {
        LOG_INFO("NFC", " (No files found)");
    }
    
    LOG_INFO("NFC", "╚═══════════════════════════════════════╝");
    
    result.success = true;
    result.message = String(count) + " files found";
    result.code = count;
    
    return result;
}

NFCManager::Result NFCManager::deleteFile(String filename, Protocol protocol) {
    // Filename is already complete (e.g., "example.srix")
    String filepath = getProtocolFolder(protocol) + filename;
    
    LOG_DEBUG("NFC", "Deleting file: %s", filepath.c_str());
    
    if (!LittleFS.exists(filepath)) {
        LOG_ERROR("NFC", "File not found: %s", filepath.c_str());
        return {false, "File not found", -2};
    }
    
    if (LittleFS.remove(filepath)) {
        LOG_INFO("NFC", "File deleted: %s", filepath.c_str());
        return {true, "File deleted", 0};
    }
    
    LOG_ERROR("NFC", "Failed to delete: %s", filepath.c_str());
    return {false, "Failed to delete file", -1};
}

// ============================================
// UTILITY FUNCTIONS
// ============================================

String NFCManager::protocolToString(Protocol proto) {
    switch (proto) {
        case PROTOCOL_SRIX:
            return "SRIX4K";
        case PROTOCOL_MIFARE_CLASSIC:
            return "Mifare Classic";
        case PROTOCOL_NTAG:
            return "NTAG";
        case PROTOCOL_DESFIRE:
            return "DESFire";
        default:
            return "Unknown";
    }
}

String NFCManager::uidToString(const uint8_t* uid, uint8_t length) {
    String result;
    result.reserve(length * 3); // Pre-allocate memory
    
    for (uint8_t i = 0; i < length; i++) {
        if (uid[i] < 0x10) result += "0";
        result += String(uid[i], HEX);
        if (i < length - 1) result += ":";
    }
    
    result.toUpperCase();
    return result;
}

String NFCManager::dumpToHex(const uint8_t* data, size_t length) {
    String result;
    result.reserve(length * 2 + length / 4); // Pre-allocate for hex + spacing
    
    for (size_t i = 0; i < length; i++) {
        if (data[i] < 0x10) result += "0";
        result += String(data[i], HEX);
        
        // Add line break every 16 bytes
        if (i > 0 && (i + 1) % 16 == 0) {
            result += "\n";
        }
        // Add space every 4 bytes
        else if (i > 0 && (i + 1) % 4 == 0) {
            result += " ";
        }
    }
    
    result.toUpperCase();
    return result;
}

size_t NFCManager::getTagDataSize(const TagInfo& info) const {
    switch (info.protocol) {
        case PROTOCOL_SRIX:
            return SRIX_DUMP_SIZE;
        case PROTOCOL_MIFARE_CLASSIC:
            return MIFARE_1K_DUMP_SIZE;
        default:
            return 0;
    }
}

const uint8_t* NFCManager::getTagDataPointer(const TagInfo& info) const {
    switch (info.protocol) {
        case PROTOCOL_SRIX:
            return info.data.srix.dump;
        case PROTOCOL_MIFARE_CLASSIC:
            return info.data.mifare_classic.dump;
        default:
            return nullptr;
    }
}

String NFCManager::getProtocolFolder(Protocol proto) {
    switch (proto) {
        case PROTOCOL_SRIX:
            return NFC_SRIX_DUMP_FOLDER;
        case PROTOCOL_MIFARE_CLASSIC:
            return NFC_MIFARE_DUMP_FOLDER;
        // Future: add NTAG, DESFire folders
        default:
            return NFC_DUMP_FOLDER;
    }
}

String NFCManager::getFileExtension(Protocol proto) {
    switch (proto) {
        case PROTOCOL_SRIX:
            return ".srix";
        case PROTOCOL_MIFARE_CLASSIC:
            return ".mfc";
        case PROTOCOL_NTAG:
            return ".ntag";
        case PROTOCOL_DESFIRE:
            return ".desfire";
        default:
            return "";
    }
}

void NFCManager::stringToUid(const String& str, uint8_t* uid, uint8_t& length) {
    String clean = str;
    clean.replace(":", "");
    clean.replace(" ", "");
    
    length = clean.length() / 2;
    if (length > MAX_UID_LENGTH) {
        length = MAX_UID_LENGTH;
    }
    
    for (uint8_t i = 0; i < length; i++) {
        String byte_str = clean.substring(i * 2, i * 2 + 2);
        uid[i] = strtoul(byte_str.c_str(), NULL, 16);
    }
}

void NFCManager::hexToDump(const String& hex, uint8_t* data, size_t length) {
    for (size_t i = 0; i < length && i * 2 < hex.length(); i++) {
        String byte_str = hex.substring(i * 2, i * 2 + 2);
        data[i] = strtoul(byte_str.c_str(), NULL, 16);
    }
}
