/**
 * @file mifare_tool.cpp
 * @brief Mifare Classic 1K/4K Reader/Writer Implementation
 */

#include "mifare_tool.h"

// ============================================
// CONSTRUCTOR & INITIALIZATION
// ============================================

MifareTool::MifareTool(bool headless)
    : _nfc(PN532_IRQ, PN532_RF_REST),
      _headless(headless),
      _uid_length(0),
      _sak(0),
      _card_type(CARD_UNKNOWN),
      _total_blocks(0),
      _blocks_read(0),
      _dump_valid(false) {
    
    // Initialize buffers
    memset(_uid, 0, sizeof(_uid));
    memset(_atqa, 0, sizeof(_atqa));
    memset(_dump, 0, sizeof(_dump));
    memset(_sector_auth_success, false, sizeof(_sector_auth_success));
    
    // Initialize key storage
    for (int i = 0; i < MIFARE_4K_SECTORS; i++) {
        memset(_sector_keys[i].key_a, 0, MIFARE_KEY_SIZE);
        memset(_sector_keys[i].key_b, 0, MIFARE_KEY_SIZE);
        _sector_keys[i].key_a_valid = false;
        _sector_keys[i].key_b_valid = false;
    }
    
    LOG_DEBUG("MIFARE", "MifareTool constructor initialized (headless: %d)", headless);
}

MifareTool::~MifareTool() {
    LOG_DEBUG("MIFARE", "MifareTool destructor called");
}

bool MifareTool::begin() {
    LOG_INFO("MIFARE", "Initializing PN532...");
    
    _nfc.begin();
    
    uint32_t versiondata = _nfc.getFirmwareVersion();
    if (!versiondata) {
        LOG_ERROR("MIFARE", "PN532 not found (check wiring and power)");
        return false;
    }
    
    LOG_INFO("MIFARE", "PN532 Firmware v%d.%d", 
             (versiondata >> 16) & 0xFF, 
             (versiondata >> 8) & 0xFF);
    
    _nfc.SAMConfig();
    
    LOG_INFO("MIFARE", "PN532 ready");
    return true;
}

// ============================================
// HEADLESS OPERATIONS - READ
// ============================================

String MifareTool::read_tag_headless(uint32_t timeout_ms) {
    LOG_INFO("MIFARE", "Starting tag read (timeout: %lu ms)", timeout_ms);
    
    unsigned long start = millis();
    
    // Wait for card detection
    while (!detectCard(CARD_DETECT_SHORT_TIMEOUT_MS)) {
        if (millis() - start > timeout_ms) {
            LOG_WARN("MIFARE", "Tag detection timeout");
            return "";
        }
        delay(CARD_DETECT_INTERVAL_MS);
    }
    
    LOG_DEBUG("MIFARE", "Card detected, starting full read...");
    
    _blocks_read = 0;
    int read_result = readAllBlocks();
    
    // Build JSON response
    String json = "{";
    json += "\"uid\":\"" + uidToString() + "\",";
    json += "\"sak\":\"" + String(_sak, HEX) + "\",";
    json += "\"atqa\":\"" + String(_atqa[1], HEX) + String(_atqa[0], HEX) + "\",";
    json += "\"card_type\":\"" + cardTypeToString(_card_type) + "\",";
    json += "\"total_blocks\":" + String(_total_blocks) + ",";
    json += "\"blocks_read\":" + String(_blocks_read) + ",";
    json += "\"auth_success\":" + String(read_result == SUCCESS ? "true" : "false") + ",";
    
    if (_blocks_read > 0) {
        json += "\"dump_preview\":\"" + dumpToHex(0, min(_blocks_read, DUMP_PREVIEW_BLOCKS)) + "\"";
    } else {
        json += "\"dump_preview\":\"\"";
    }
    
    json += "}";
    
    _dump_valid = (read_result == SUCCESS);
    
    LOG_INFO("MIFARE", "Read complete: %d/%d blocks (success: %d)", 
             _blocks_read, _total_blocks, read_result == SUCCESS);
    
    return json;
}

String MifareTool::read_uid_headless(uint32_t timeout_ms) {
    LOG_INFO("MIFARE", "Starting UID-only read (timeout: %lu ms)", timeout_ms);
    
    unsigned long start = millis();
    
    // Wait for card detection
    while (!detectCard(CARD_DETECT_SHORT_TIMEOUT_MS)) {
        if (millis() - start > timeout_ms) {
            LOG_WARN("MIFARE", "UID read timeout");
            return "";
        }
        delay(CARD_DETECT_INTERVAL_MS);
    }
    
    // Build JSON response (UID only)
    String json = "{";
    json += "\"uid\":\"" + uidToString() + "\",";
    json += "\"sak\":\"" + String(_sak, HEX) + "\",";
    json += "\"atqa\":\"" + String(_atqa[1], HEX) + String(_atqa[0], HEX) + "\",";
    json += "\"card_type\":\"" + cardTypeToString(_card_type) + "\"";
    json += "}";
    
    LOG_INFO("MIFARE", "UID read successful: %s", uidToString().c_str());
    
    return json;
}

// ============================================
// HEADLESS OPERATIONS - WRITE
// ============================================

int MifareTool::write_tag_headless(int timeout_sec) {
    if (!_dump_valid) {
        LOG_ERROR("MIFARE", "Write failed: No valid dump loaded");
        return ERROR_INVALID_DATA;
    }
    
    LOG_INFO("MIFARE", "Starting tag write (timeout: %d seconds)", timeout_sec);
    
    unsigned long start = millis();
    uint32_t timeout_ms = timeout_sec * 1000UL;
    
    // Wait for card
    while (!detectCard(CARD_DETECT_SHORT_TIMEOUT_MS)) {
        if (millis() - start > timeout_ms) {
            LOG_ERROR("MIFARE", "Write failed: Tag detection timeout");
            return ERROR_NO_TAG;
        }
        delay(CARD_DETECT_INTERVAL_MS);
    }
    
    // Verify card type matches dump
    if (identifyCardType() != _card_type) {
        LOG_ERROR("MIFARE", "Write failed: Card type mismatch (expected %s, got %s)",
                 cardTypeToString(_card_type).c_str(),
                 cardTypeToString(identifyCardType()).c_str());
        return ERROR_INVALID_DATA;
    }
    
    LOG_DEBUG("MIFARE", "Card type verified, starting sector writes...");
    
    // Write all sectors
    int sector_count = getSectorCount(_card_type);
    for (int sector = 0; sector < sector_count; sector++) {
        int result = writeSector(sector);
        if (result != SUCCESS) {
            LOG_ERROR("MIFARE", "Write failed at sector %d (error: %d)", sector, result);
            return result;
        }
        
        // Check timeout
        if (millis() - start > timeout_ms) {
            LOG_ERROR("MIFARE", "Write timeout after sector %d", sector);
            return ERROR_NO_TAG;
        }
    }
    
    LOG_INFO("MIFARE", "Write complete: %d sectors written successfully", sector_count);
    return SUCCESS;
}

int MifareTool::write_single_block_headless(int block) {
    if (!_dump_valid) {
        LOG_ERROR("MIFARE", "Single block write failed: No valid dump loaded");
        return ERROR_INVALID_DATA;
    }
    
    // Validate block number
    if (block < 0 || block >= _total_blocks) {
        LOG_ERROR("MIFARE", "Invalid block number: %d (max: %d)", block, _total_blocks - 1);
        return ERROR_INVALID_DATA;
    }
    
    // Protect UID block
    if (block == UID_BLOCK) {
        LOG_WARN("MIFARE", "Cannot write block 0 (UID) - use clone_uid_headless() instead");
        return ERROR_INVALID_DATA;
    }
    
    // Determine sector and check if it's a trailer
    int sector = (block < LARGE_SECTOR_BOUNDARY) ? 
                 (block / BLOCKS_PER_SMALL_SECTOR) : 
                 (SMALL_SECTOR_COUNT + (block - LARGE_SECTOR_BOUNDARY) / BLOCKS_PER_LARGE_SECTOR);
    
    int first_block = getFirstBlockOfSector(sector);
    int block_count = getBlockCountInSector(sector);
    int trailer_block = first_block + (block_count - 1);
    
    // Protect sector trailer
    if (block == trailer_block) {
        LOG_WARN("MIFARE", "Cannot write sector trailer (block %d)", block);
        return ERROR_INVALID_DATA;
    }
    
    LOG_INFO("MIFARE", "Writing single block %d (sector %d)...", block, sector);
    
    // Wait for tag
    unsigned long start = millis();
    while (!detectCard(CARD_DETECT_SHORT_TIMEOUT_MS)) {
        if (millis() - start > SINGLE_BLOCK_TIMEOUT_MS) {
            LOG_ERROR("MIFARE", "Single block write timeout");
            return ERROR_NO_TAG;
        }
        delay(CARD_DETECT_INTERVAL_MS);
    }
    
    // Write the block
    int result = writeSingleBlock(block);
    
    if (result == SUCCESS) {
        LOG_INFO("MIFARE", "Block %d written successfully", block);
    } else {
        LOG_ERROR("MIFARE", "Block %d write failed (error: %d)", block, result);
    }
    
    return result;
}

int MifareTool::clone_uid_headless(int timeout_sec) {
    if (!_dump_valid || _uid_length == 0) {
        LOG_ERROR("MIFARE", "Clone UID failed: No valid UID loaded");
        return ERROR_INVALID_DATA;
    }
    
    LOG_INFO("MIFARE", "Starting UID clone (timeout: %d seconds)", timeout_sec);
    
    unsigned long start = millis();
    uint32_t timeout_ms = timeout_sec * 1000UL;
    
    // Wait for card
    while (!detectCard(CARD_DETECT_SHORT_TIMEOUT_MS)) {
        if (millis() - start > timeout_ms) {
            LOG_ERROR("MIFARE", "Clone UID timeout: No tag detected");
            return ERROR_NO_TAG;
        }
        delay(CARD_DETECT_INTERVAL_MS);
    }
    
    // Build block 0 data
    uint8_t block0[MIFARE_BLOCK_SIZE];
    memset(block0, 0, MIFARE_BLOCK_SIZE);
    
    // Copy UID
    for (int i = 0; i < _uid_length; i++) {
        block0[i] = _uid[i];
    }
    
    // Calculate BCC (XOR of UID bytes)
    uint8_t bcc = 0;
    for (int i = 0; i < _uid_length; i++) {
        bcc ^= _uid[i];
    }
    
    // Set BCC, SAK, ATQA
    block0[_uid_length] = bcc;
    block0[_uid_length + 1] = _sak;
    block0[_uid_length + 2] = _atqa[1];
    block0[_uid_length + 3] = _atqa[0];
    
    LOG_DEBUG("MIFARE", "Block 0 prepared: UID=%s, BCC=%02X, SAK=%02X, ATQA=%02X%02X",
             uidToString().c_str(), bcc, _sak, _atqa[1], _atqa[0]);
    
    // Write block 0
    if (writeBlock0(block0)) {
        LOG_INFO("MIFARE", "UID cloned successfully");
        return SUCCESS;
    }
    
    LOG_ERROR("MIFARE", "UID clone failed (magic card required)");
    return ERROR_WRITE_FAILED;
}

// ============================================
// FILE OPERATIONS
// ============================================

String MifareTool::save_file_headless(const String& filename) {
    if (!_dump_valid) {
        LOG_ERROR("MIFARE", "Save failed: No valid dump to save");
        return "";
    }
    
    LOG_INFO("MIFARE", "Saving dump to file: %s", filename.c_str());
    
    String filepath = buildFilePath(filename);
    
    File file = LittleFS.open(filepath, FILE_WRITE);
    if (!file) {
        LOG_ERROR("MIFARE", "Failed to open file for writing: %s", filepath.c_str());
        return "";
    }
    
    writeFileFormat(file);
    file.close();
    
    LOG_INFO("MIFARE", "File saved successfully: %s (%d blocks)", 
             filepath.c_str(), _blocks_read);
    
    return filepath;
}

int MifareTool::load_file_headless(const String& filename) {
    LOG_INFO("MIFARE", "Loading dump from file: %s", filename.c_str());
    
    String filepath = buildFilePath(filename);
    
    if (!LittleFS.exists(filepath)) {
        LOG_ERROR("MIFARE", "File not found: %s", filepath.c_str());
        return ERROR_FILE_ERROR;
    }
    
    File file = LittleFS.open(filepath, FILE_READ);
    if (!file) {
        LOG_ERROR("MIFARE", "Failed to open file: %s", filepath.c_str());
        return ERROR_FILE_ERROR;
    }
    
    bool success = parseFileFormat(file);
    file.close();
    
    if (success) {
        _dump_valid = true;
        extractKeysFromDump();
        
        LOG_INFO("MIFARE", "File loaded successfully: %d blocks, %d sectors", 
                 _blocks_read, getSectorCount(_card_type));
        return SUCCESS;
    }
    
    LOG_ERROR("MIFARE", "File parse failed: %s", filepath.c_str());
    return ERROR_INVALID_DATA;
}

// ============================================
// INTERNAL: CARD DETECTION
// ============================================

bool MifareTool::detectCard(uint32_t timeout_ms) {
    uint8_t uid_buffer[7];
    uint8_t uid_len;
    
    bool detected = _nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid_buffer, &uid_len, timeout_ms);
    
    if (detected) {
        _uid_length = uid_len;
        memcpy(_uid, uid_buffer, uid_len);
        _card_type = identifyCardType();
        
        LOG_DEBUG("MIFARE", "Card detected, identifying type...");
        
        // Re-select card after type detection (workaround for PN532 bug)
        delay(CARD_REACTIVATE_DELAY_MS);
        detected = _nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid_buffer, &uid_len, CARD_RESELECT_TIMEOUT_MS);
        
        if (!detected) {
            LOG_WARN("MIFARE", "Card lost after re-selection");
            return false;
        }
        
        // Check if same card
        if (uid_len != _uid_length || memcmp(uid_buffer, _uid, uid_len) != 0) {
            LOG_WARN("MIFARE", "Different card detected after re-selection");
            _uid_length = uid_len;
            memcpy(_uid, uid_buffer, uid_len);
        }
        
        // Set SAK/ATQA based on card type
        switch (_card_type) {
            case CARD_MIFARE_1K:
                _sak = 0x08;
                _atqa[0] = 0x04;
                _atqa[1] = 0x00;
                break;
            case CARD_MIFARE_4K:
                _sak = 0x18;
                _atqa[0] = 0x02;
                _atqa[1] = 0x00;
                break;
            default:
                _sak = 0x00;
                _atqa[0] = 0x00;
                _atqa[1] = 0x00;
                break;
        }
        
        _total_blocks = (_card_type == CARD_MIFARE_1K) ? MIFARE_1K_BLOCKS : MIFARE_4K_BLOCKS;
        
        LOG_INFO("MIFARE", "Card detected: UID=%d bytes, Type=%s, Blocks=%d",
                 _uid_length, cardTypeToString(_card_type).c_str(), _total_blocks);
        
        return true;
    }
    
    return false;
}

// ============================================
// INTERNAL: CARD TYPE IDENTIFICATION
// ============================================

MifareTool::CardType MifareTool::identifyCardType() {
    uint8_t default_key[MIFARE_KEY_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    // Try to authenticate block 64 (first block of sector 16)
    // If successful, it's a 4K card; otherwise, it's a 1K card
    if (_nfc.mifareclassic_AuthenticateBlock(_uid, _uid_length, 64, 0, default_key)) {
        LOG_DEBUG("MIFARE", "Detected: Mifare Classic 4K");
        return CARD_MIFARE_4K;
    }
    
    LOG_DEBUG("MIFARE", "Detected: Mifare Classic 1K");
    return CARD_MIFARE_1K;
}
// ============================================
// INTERNAL: READ OPERATIONS
// ============================================

int MifareTool::readAllBlocks() {
    LOG_INFO("MIFARE", "Starting full dump read...");
    
    _blocks_read = 0;
    int sector_count = getSectorCount(_card_type);
    
    // Key caching for optimization
    String working_key_a = "";
    String working_key_b = "";
    
    for (int sector = 0; sector < sector_count; sector++) {
        int result = readSectorWithCache(sector, working_key_a, working_key_b);
        
        if (result != SUCCESS) {
            _sector_auth_success[sector] = false;
            LOG_WARN("MIFARE", "Sector %d read failed", sector);
            continue;
        }
        
        _sector_auth_success[sector] = true;
    }
    
    LOG_INFO("MIFARE", "Read complete: %d blocks read", _blocks_read);
    
    return (_blocks_read > 0) ? SUCCESS : ERROR_AUTH_FAILED;
}

int MifareTool::readSectorWithCache(int sector, String& cached_key_a, String& cached_key_b) {
    int first_block = getFirstBlockOfSector(sector);
    int block_count = getBlockCountInSector(sector);
    bool auth_success = false;
    uint8_t key_bytes[MIFARE_KEY_SIZE];
    bool used_key_a = false;
    
    MifareKeysManager::ensureLoaded();
    const auto& keys = MifareKeysManager::getKeys();
    
    LOG_DEBUG("MIFARE", "Sector %d: Attempting authentication (%d keys in database)...", 
             sector, keys.size());
    
    // ============================================
    // STEP 1: Try cached Key A
    // ============================================
    if (!cached_key_a.isEmpty()) {
        MifareKeysManager::keyToBytes(cached_key_a, key_bytes);
        
        if (authenticateBlock(first_block, true, key_bytes) == SUCCESS) {
            auth_success = true;
            used_key_a = true;
            
            // Save Key A
            memcpy(_sector_keys[sector].key_a, key_bytes, MIFARE_KEY_SIZE);
            _sector_keys[sector].key_a_valid = true;
            
            LOG_DEBUG("MIFARE", "Sector %d: Key A authenticated (cached)", sector);
        } else {
            reactivateCard();
        }
    }
    
    // ============================================
    // STEP 2: Try cached Key B
    // ============================================
    if (!auth_success && !cached_key_b.isEmpty()) {
        MifareKeysManager::keyToBytes(cached_key_b, key_bytes);
        
        if (authenticateBlock(first_block, false, key_bytes) == SUCCESS) {
            auth_success = true;
            used_key_a = false;
            
            // Save Key B
            memcpy(_sector_keys[sector].key_b, key_bytes, MIFARE_KEY_SIZE);
            _sector_keys[sector].key_b_valid = true;
            
            LOG_DEBUG("MIFARE", "Sector %d: Key B authenticated (cached)", sector);
        } else {
            reactivateCard();
        }
    }
    
    // ============================================
    // STEP 3: Brute-force with Key A
    // ============================================
    if (!auth_success) {
        LOG_DEBUG("MIFARE", "Sector %d: Trying all keys with Key A...", sector);
        
        for (const String& key_str : keys) {
            MifareKeysManager::keyToBytes(key_str, key_bytes);
            
            if (authenticateBlock(first_block, true, key_bytes) == SUCCESS) {
                auth_success = true;
                used_key_a = true;
                cached_key_a = key_str;
                
                // Save Key A
                memcpy(_sector_keys[sector].key_a, key_bytes, MIFARE_KEY_SIZE);
                _sector_keys[sector].key_a_valid = true;
                
                LOG_DEBUG("MIFARE", "Sector %d: Key A authenticated (%s) - CACHED", 
                         sector, key_str.c_str());
                break;
            } else {
                reactivateCard();
            }
        }
    }
    
    // ============================================
    // STEP 4: Brute-force with Key B
    // ============================================
    if (!auth_success) {
        LOG_DEBUG("MIFARE", "Sector %d: Trying all keys with Key B...", sector);
        
        for (const String& key_str : keys) {
            MifareKeysManager::keyToBytes(key_str, key_bytes);
            
            if (authenticateBlock(first_block, false, key_bytes) == SUCCESS) {
                auth_success = true;
                used_key_a = false;
                cached_key_b = key_str;
                
                // Save Key B
                memcpy(_sector_keys[sector].key_b, key_bytes, MIFARE_KEY_SIZE);
                _sector_keys[sector].key_b_valid = true;
                
                LOG_DEBUG("MIFARE", "Sector %d: Key B authenticated (%s) - CACHED", 
                         sector, key_str.c_str());
                break;
            } else {
                reactivateCard();
            }
        }
    }
    
    if (!auth_success) {
        LOG_WARN("MIFARE", "Sector %d: Authentication failed with all keys", sector);
        return ERROR_AUTH_FAILED;
    }
    
    // ============================================
    // Read all blocks in sector
    // ============================================
    for (int block_offset = 0; block_offset < block_count; block_offset++) {
        int block = first_block + block_offset;
        uint8_t buffer[MIFARE_BLOCK_SIZE];
        
        if (_nfc.mifareclassic_ReadDataBlock(block, buffer)) {
            memcpy(&_dump[block * MIFARE_BLOCK_SIZE], buffer, MIFARE_BLOCK_SIZE);
            _blocks_read++;
        } else {
            LOG_ERROR("MIFARE", "Block %d read failed", block);
            return ERROR_READ_FAILED;
        }
    }
    
    // ============================================
    // Reconstruct sector trailer with extracted keys
    // ============================================
    int trailer_block = first_block + (block_count - 1);
    uint8_t* trailer_data = &_dump[trailer_block * MIFARE_BLOCK_SIZE];
    
    // Overwrite Key A (bytes 0-5) if available
    if (_sector_keys[sector].key_a_valid) {
        memcpy(trailer_data + KEY_A_OFFSET, _sector_keys[sector].key_a, MIFARE_KEY_SIZE);
        LOG_DEBUG("MIFARE", "Sector %d: Key A reconstructed in dump", sector);
    }
    
    // Bytes 6-9 (Access Bits) remain as read from tag
    
    // Overwrite Key B (bytes 10-15) if available
    if (_sector_keys[sector].key_b_valid) {
        memcpy(trailer_data + KEY_B_OFFSET, _sector_keys[sector].key_b, MIFARE_KEY_SIZE);
        LOG_DEBUG("MIFARE", "Sector %d: Key B reconstructed in dump", sector);
    }
    
    return SUCCESS;
}

int MifareTool::authenticateBlock(int block, bool keyA, const uint8_t* key) {
    uint8_t key_type = keyA ? 0 : 1;
    
    if (_nfc.mifareclassic_AuthenticateBlock(_uid, _uid_length, block, key_type, (uint8_t*)key)) {
        return SUCCESS;
    }
    
    return ERROR_AUTH_FAILED;
}

// ============================================
// INTERNAL: WRITE OPERATIONS
// ============================================

int MifareTool::writeSector(int sector) {
    int first_block = getFirstBlockOfSector(sector);
    int block_count = getBlockCountInSector(sector);
    bool auth_success = false;
    uint8_t key_bytes[MIFARE_KEY_SIZE];
    
    LOG_DEBUG("MIFARE", "Writing sector %d...", sector);
    
    // ============================================
    // STEP 1: Try saved keys first (from dump or previous read)
    // ============================================
    
    // Try saved Key A
    if (_sector_keys[sector].key_a_valid) {
        memcpy(key_bytes, _sector_keys[sector].key_a, MIFARE_KEY_SIZE);
        LOG_DEBUG("MIFARE", "Sector %d: Trying saved Key A...", sector);
        
        if (authenticateBlock(first_block, true, key_bytes) == SUCCESS) {
            auth_success = true;
            LOG_DEBUG("MIFARE", "Sector %d: Authenticated with saved Key A", sector);
        } else {
            reactivateCard();
        }
    }
    
    // Try saved Key B
    if (!auth_success && _sector_keys[sector].key_b_valid) {
        memcpy(key_bytes, _sector_keys[sector].key_b, MIFARE_KEY_SIZE);
        LOG_DEBUG("MIFARE", "Sector %d: Trying saved Key B...", sector);
        
        if (authenticateBlock(first_block, false, key_bytes) == SUCCESS) {
            auth_success = true;
            LOG_DEBUG("MIFARE", "Sector %d: Authenticated with saved Key B", sector);
        } else {
            reactivateCard();
        }
    }
    
    // ============================================
    // STEP 2: If saved keys fail, try full database
    // ============================================
    if (!auth_success) {
        LOG_DEBUG("MIFARE", "Sector %d: Saved keys failed, trying database...", sector);
        
        MifareKeysManager::ensureLoaded();
        const auto& keys = MifareKeysManager::getKeys();
        
        // Try all keys with Key A
        for (const String& key_str : keys) {
            MifareKeysManager::keyToBytes(key_str, key_bytes);
            
            if (authenticateBlock(first_block, true, key_bytes) == SUCCESS) {
                auth_success = true;
                LOG_DEBUG("MIFARE", "Sector %d: Authenticated with Key A from database", sector);
                break;
            }
            
            reactivateCard();
        }
        
        // Try all keys with Key B
        if (!auth_success) {
            for (const String& key_str : keys) {
                MifareKeysManager::keyToBytes(key_str, key_bytes);
                
                if (authenticateBlock(first_block, false, key_bytes) == SUCCESS) {
                    auth_success = true;
                    LOG_DEBUG("MIFARE", "Sector %d: Authenticated with Key B from database", sector);
                    break;
                }
                
                reactivateCard();
            }
        }
    }
    
    if (!auth_success) {
        LOG_ERROR("MIFARE", "Sector %d: All authentication attempts failed", sector);
        return ERROR_AUTH_FAILED;
    }
    
    // ============================================
    // Write blocks (skip UID and sector trailer)
    // ============================================
    for (int block_offset = 0; block_offset < block_count; block_offset++) {
        int block = first_block + block_offset;
        
        // Skip UID block (block 0 in sector 0)
        if (sector == 0 && block_offset == 0) {
            LOG_DEBUG("MIFARE", "Skipping block 0 (UID block)");
            continue;
        }
        
        // Skip sector trailer (last block of sector)
        bool is_trailer = (block_offset == (block_count - 1));
        if (is_trailer) {
            LOG_DEBUG("MIFARE", "Skipping sector %d trailer (block %d)", sector, block);
            continue;
        }
        
        // Write block
        uint8_t* data = &_dump[block * MIFARE_BLOCK_SIZE];
        if (!_nfc.mifareclassic_WriteDataBlock(block, data)) {
            LOG_ERROR("MIFARE", "Block %d write failed", block);
            return ERROR_WRITE_FAILED;
        }
        
        LOG_DEBUG("MIFARE", "Block %d written successfully", block);
        
        delay(BLOCK_WRITE_DELAY_MS);
    }
    
    return SUCCESS;
}

int MifareTool::writeSingleBlock(int block) {
    // Determine sector
    int sector = (block < LARGE_SECTOR_BOUNDARY) ? 
                 (block / BLOCKS_PER_SMALL_SECTOR) : 
                 (SMALL_SECTOR_COUNT + (block - LARGE_SECTOR_BOUNDARY) / BLOCKS_PER_LARGE_SECTOR);
    
    int first_block = getFirstBlockOfSector(sector);
    bool auth_success = false;
    uint8_t key_bytes[MIFARE_KEY_SIZE];
    
    LOG_DEBUG("MIFARE", "Writing single block %d (sector %d)...", block, sector);
    
    // ============================================
    // STEP 1: Try saved keys (from dump)
    // ============================================
    
    // Try saved Key A
    if (_sector_keys[sector].key_a_valid) {
        memcpy(key_bytes, _sector_keys[sector].key_a, MIFARE_KEY_SIZE);
        
        if (authenticateBlock(first_block, true, key_bytes) == SUCCESS) {
            auth_success = true;
            LOG_DEBUG("MIFARE", "Authenticated with saved Key A");
        } else {
            reactivateCard();
        }
    }
    
    // Try saved Key B
    if (!auth_success && _sector_keys[sector].key_b_valid) {
        memcpy(key_bytes, _sector_keys[sector].key_b, MIFARE_KEY_SIZE);
        
        if (authenticateBlock(first_block, false, key_bytes) == SUCCESS) {
            auth_success = true;
            LOG_DEBUG("MIFARE", "Authenticated with saved Key B");
        } else {
            reactivateCard();
        }
    }
    
    // ============================================
    // STEP 2: If saved keys fail, try database
    // ============================================
    if (!auth_success) {
        LOG_DEBUG("MIFARE", "Saved keys failed, trying database...");
        
        MifareKeysManager::ensureLoaded();
        const auto& keys = MifareKeysManager::getKeys();
        
        // Try all keys with Key A
        for (const String& key_str : keys) {
            MifareKeysManager::keyToBytes(key_str, key_bytes);
            
            if (authenticateBlock(first_block, true, key_bytes) == SUCCESS) {
                auth_success = true;
                LOG_DEBUG("MIFARE", "Authenticated with Key A from database");
                break;
            }
            
            reactivateCard();
        }
        
        // Try all keys with Key B
        if (!auth_success) {
            for (const String& key_str : keys) {
                MifareKeysManager::keyToBytes(key_str, key_bytes);
                
                if (authenticateBlock(first_block, false, key_bytes) == SUCCESS) {
                    auth_success = true;
                    LOG_DEBUG("MIFARE", "Authenticated with Key B from database");
                    break;
                }
                
                reactivateCard();
            }
        }
    }
    
    if (!auth_success) {
        LOG_ERROR("MIFARE", "Authentication failed for block %d", block);
        return ERROR_AUTH_FAILED;
    }
    
    // ============================================
    // Write block
    // ============================================
    uint8_t* data = &_dump[block * MIFARE_BLOCK_SIZE];
    
    if (!_nfc.mifareclassic_WriteDataBlock(block, data)) {
        LOG_ERROR("MIFARE", "Block %d write failed", block);
        return ERROR_WRITE_FAILED;
    }
    
    LOG_INFO("MIFARE", "Block %d written successfully", block);
    
    delay(BLOCK_WRITE_DELAY_MS);
    return SUCCESS;
}

bool MifareTool::writeBlock0(const uint8_t* data) {
    uint8_t key_bytes[MIFARE_KEY_SIZE];
    bool auth_success = false;
    
    LOG_DEBUG("MIFARE", "Attempting to write block 0 (UID)...");
    
    MifareKeysManager::ensureLoaded();
    const auto& keys = MifareKeysManager::getKeys();
    
    // Try all keys with Key A
    for (const String& key_str : keys) {
        MifareKeysManager::keyToBytes(key_str, key_bytes);
        
        if (_nfc.mifareclassic_AuthenticateBlock(_uid, _uid_length, 0, 0, key_bytes)) {
            auth_success = true;
            LOG_DEBUG("MIFARE", "Block 0 authenticated with Key A");
            break;
        }
        
        reactivateCard();
    }
    
    // Try all keys with Key B
    if (!auth_success) {
        for (const String& key_str : keys) {
            MifareKeysManager::keyToBytes(key_str, key_bytes);
            
            if (_nfc.mifareclassic_AuthenticateBlock(_uid, _uid_length, 0, 1, key_bytes)) {
                auth_success = true;
                LOG_DEBUG("MIFARE", "Block 0 authenticated with Key B");
                break;
            }
            
            reactivateCard();
        }
    }
    
    if (!auth_success) {
        LOG_ERROR("MIFARE", "Cannot authenticate block 0 for UID write");
        return false;
    }
    
    bool write_success = _nfc.mifareclassic_WriteDataBlock(0, (uint8_t*)data);
    
    if (!write_success) {
        LOG_ERROR("MIFARE", "Block 0 write failed (magic card required)");
    } else {
        LOG_INFO("MIFARE", "Block 0 (UID) written successfully");
    }
    
    return write_success;
}

bool MifareTool::reactivateCard() {
    uint8_t uid_buffer[7];
    uint8_t uid_len;
    
    bool detected = _nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid_buffer, &uid_len, CARD_REACTIVATE_DELAY_MS);
    
    if (detected && (uid_len != _uid_length || memcmp(uid_buffer, _uid, uid_len) != 0)) {
        LOG_WARN("MIFARE", "Different card detected during re-activation");
    }
    
    return detected;
}

void MifareTool::extractKeysFromDump() {
    if (!_dump_valid || _blocks_read == 0) {
        LOG_DEBUG("MIFARE", "Cannot extract keys: No valid dump");
        return;
    }
    
    LOG_DEBUG("MIFARE", "Extracting keys from dump...");
    
    int sector_count = getSectorCount(_card_type);
    int keys_extracted = 0;
    
    for (int sector = 0; sector < sector_count; sector++) {
        int first_block = getFirstBlockOfSector(sector);
        int block_count = getBlockCountInSector(sector);
        int trailer_block = first_block + (block_count - 1);
        
        // Check if we have read this sector
        if (trailer_block >= _blocks_read) {
            continue;
        }
        
        uint8_t* trailer_data = &_dump[trailer_block * MIFARE_BLOCK_SIZE];
        
        // Extract Key A (bytes 0-5)
        memcpy(_sector_keys[sector].key_a, trailer_data + KEY_A_OFFSET, MIFARE_KEY_SIZE);
        _sector_keys[sector].key_a_valid = true;
        
        // Extract Key B (bytes 10-15)
        memcpy(_sector_keys[sector].key_b, trailer_data + KEY_B_OFFSET, MIFARE_KEY_SIZE);
        _sector_keys[sector].key_b_valid = true;
        
        keys_extracted++;
    }
    
    LOG_INFO("MIFARE", "Keys extracted from %d sectors", keys_extracted);
}
// ============================================
// FILE FORMAT OPERATIONS
// ============================================

void MifareTool::writeFileFormat(File& file) {
    LOG_DEBUG("MIFARE", "Writing file format...");
    
    // Write header
    file.println("Filetype: Mifare Classic File");
    file.println("Version 1");
    file.println("Device type: " + cardTypeToString(_card_type));
    file.println("# UID, ATQA and SAK are common for all formats");
    file.println("UID: " + uidToString());
    
    // Write SAK
    file.print("SAK: ");
    if (_sak < HEX_PADDING_THRESHOLD) file.print("0");
    file.println(String(_sak, HEX));
    
    // Write ATQA
    file.print("ATQA: ");
    if (_atqa[1] < HEX_PADDING_THRESHOLD) file.print("0");
    file.print(String(_atqa[1], HEX));
    file.print(" ");
    if (_atqa[0] < HEX_PADDING_THRESHOLD) file.print("0");
    file.println(String(_atqa[0], HEX));
    
    // Write memory dump header
    file.println("# Memory dump");
    file.println("Pages total: " + String(_total_blocks));
    
    if (_blocks_read < _total_blocks) {
        file.println("Pages read: " + String(_blocks_read));
    }
    
    // Write all blocks
    for (int block = 0; block < _blocks_read; block++) {
        file.print("Page ");
        file.print(block);
        file.print(": ");
        
        for (int i = 0; i < MIFARE_BLOCK_SIZE; i++) {
            if (_dump[block * MIFARE_BLOCK_SIZE + i] < HEX_PADDING_THRESHOLD) {
                file.print("0");
            }
            file.print(String(_dump[block * MIFARE_BLOCK_SIZE + i], HEX));
            if (i < MIFARE_BLOCK_SIZE - 1) {
                file.print(" ");
            }
        }
        file.println();
    }
    
    LOG_DEBUG("MIFARE", "File format written: %d blocks", _blocks_read);
}

bool MifareTool::parseFileFormat(File& file) {
    LOG_DEBUG("MIFARE", "Parsing file format...");
    
    String line;
    _blocks_read = 0;
    
    while (file.available()) {
        line = file.readStringUntil('\n');
        line.trim();
        
        // Parse device type
        if (line.startsWith("Device type:")) {
            String type = line.substring(13);
            type.trim();
            
            if (type.indexOf("1K") >= 0) {
                _card_type = CARD_MIFARE_1K;
            } else if (type.indexOf("4K") >= 0) {
                _card_type = CARD_MIFARE_4K;
            }
            
            LOG_DEBUG("MIFARE", "Card type: %s", cardTypeToString(_card_type).c_str());
        }
        // Parse UID
        else if (line.startsWith("UID:")) {
            String uid_str = line.substring(5);
            uid_str.trim();
            uid_str.replace(" ", "");
            uid_str.replace(":", "");
            
            _uid_length = uid_str.length() / HEX_CHARS_PER_BYTE;
            hexStringToBytes(uid_str, _uid, MIFARE_UID_MAX_SIZE);
            
            LOG_DEBUG("MIFARE", "UID parsed: %s (%d bytes)", uid_str.c_str(), _uid_length);
        }
        // Parse SAK
        else if (line.startsWith("SAK:")) {
            String sak_str = line.substring(5);
            sak_str.trim();
            _sak = strtoul(sak_str.c_str(), NULL, 16);
            
            LOG_DEBUG("MIFARE", "SAK: %02X", _sak);
        }
        // Parse ATQA
        else if (line.startsWith("ATQA:")) {
            String atqa_str = line.substring(6);
            atqa_str.trim();
            atqa_str.replace(" ", "");
            hexStringToBytes(atqa_str, _atqa, MIFARE_ATQA_SIZE);
            
            LOG_DEBUG("MIFARE", "ATQA: %02X %02X", _atqa[0], _atqa[1]);
        }
        // Parse block data
        else if (line.startsWith("Page ")) {
            int colon_pos = line.indexOf(':');
            if (colon_pos > 0) {
                String data_str = line.substring(colon_pos + 1);
                data_str.trim();
                data_str.replace(" ", "");
                
                // Each block is 16 bytes = 32 hex chars
                if (data_str.length() == MIFARE_BLOCK_SIZE * HEX_CHARS_PER_BYTE) {
                    hexStringToBytes(data_str, &_dump[_blocks_read * MIFARE_BLOCK_SIZE], MIFARE_BLOCK_SIZE);
                    _blocks_read++;
                }
            }
        }
    }
    
    _total_blocks = (_card_type == CARD_MIFARE_1K) ? MIFARE_1K_BLOCKS : MIFARE_4K_BLOCKS;
    
    LOG_INFO("MIFARE", "File parsed: %d blocks loaded", _blocks_read);
    
    return (_blocks_read > 0);
}

String MifareTool::buildFilePath(const String& filename) {
    // If filename already has full path, return as-is
    if (filename.startsWith("/")) {
        return filename;
    }
    
    String base = filename;
    
    // Add .mfc extension if missing
    if (!base.endsWith(".mfc")) {
        base += ".mfc";
    }
    
    return String(NFC_MIFARE_DUMP_FOLDER) + base;
}

// ============================================
// UTILITY FUNCTIONS
// ============================================

String MifareTool::uidToString() {
    String result;
    result.reserve(_uid_length * 3); // Pre-allocate memory
    
    for (int i = 0; i < _uid_length; i++) {
        if (_uid[i] < HEX_PADDING_THRESHOLD) {
            result += "0";
        }
        result += String(_uid[i], HEX);
        if (i < _uid_length - 1) {
            result += " ";
        }
    }
    
    result.toUpperCase();
    return result;
}

String MifareTool::dumpToHex(int start_block, int num_blocks) {
    String result;
    result.reserve(num_blocks * MIFARE_BLOCK_SIZE * HEX_CHARS_PER_BYTE); // Pre-allocate
    
    for (int block = start_block; block < start_block + num_blocks && block < _total_blocks; block++) {
        for (int i = 0; i < MIFARE_BLOCK_SIZE; i++) {
            if (_dump[block * MIFARE_BLOCK_SIZE + i] < HEX_PADDING_THRESHOLD) {
                result += "0";
            }
            result += String(_dump[block * MIFARE_BLOCK_SIZE + i], HEX);
        }
    }
    
    result.toUpperCase();
    return result;
}

void MifareTool::hexStringToBytes(const String& hex, uint8_t* output, size_t max_len) {
    size_t len = min(hex.length() / HEX_CHARS_PER_BYTE, max_len);
    
    for (size_t i = 0; i < len; i++) {
        String byte_str = hex.substring(i * HEX_CHARS_PER_BYTE, i * HEX_CHARS_PER_BYTE + HEX_CHARS_PER_BYTE);
        output[i] = strtoul(byte_str.c_str(), NULL, 16);
    }
}

String MifareTool::cardTypeToString(CardType type) {
    switch (type) {
        case CARD_MIFARE_1K:
            return "Mifare Classic 1K";
        case CARD_MIFARE_4K:
            return "Mifare Classic 4K";
        default:
            return "Unknown";
    }
}

int MifareTool::getSectorCount(CardType type) {
    return (type == CARD_MIFARE_1K) ? MIFARE_1K_SECTORS : MIFARE_4K_SECTORS;
}

int MifareTool::getFirstBlockOfSector(int sector) {
    if (sector < SMALL_SECTOR_COUNT) {
        // Sectors 0-31: 4 blocks each
        return sector * BLOCKS_PER_SMALL_SECTOR;
    } else {
        // Sectors 32-39: 16 blocks each
        return LARGE_SECTOR_BOUNDARY + (sector - SMALL_SECTOR_COUNT) * BLOCKS_PER_LARGE_SECTOR;
    }
}

int MifareTool::getBlockCountInSector(int sector) {
    return (sector < SMALL_SECTOR_COUNT) ? BLOCKS_PER_SMALL_SECTOR : BLOCKS_PER_LARGE_SECTOR;
}
