/**
 * @file srix_tool.cpp
 * @brief SRIX4K/SRIX512 Reader/Writer Tool v1.3
 * @author Senape3000
 * @info https://github.com/Senape3000/firmware/blob/main/docs_custom/SRIX/SRIX_Tool_README.md
 * @date 2026-01-01
 */

#include "srix_tool.h"
#include <LittleFS.h>

// ============================================
// CONSTRUCTOR
// ============================================

SRIXTool::SRIXTool(bool headless_mode) {
    LOG_INFO("SRIX", "Initializing SRIX Tool (headless mode: %d)", headless_mode);
    
    // Initialize state flags
    _dump_valid_from_read = false;
    _dump_valid_from_load = false;
    _tag_read = false;
    
    // Clear buffers
    memset(_dump, 0, sizeof(_dump));
    memset(_uid, 0, sizeof(_uid));
    
    // Initialize I2C
    LOG_DEBUG("SRIX", "I2C configuration: SDA=%d, SCL=%d, Clock=%dHz", 
             sda_pin, scl_pin, I2C_CLOCK_SPEED);
    Wire.begin(sda_pin, scl_pin);
    Wire.setClock(I2C_CLOCK_SPEED);
    
    // Create PN532 instance based on hardware configuration
    #if defined(PN532_IRQ) && defined(PN532_RF_REST)
        LOG_INFO("SRIX", "Using hardware IRQ mode: IRQ=%d, RST=%d", PN532_IRQ, PN532_RF_REST);
        nfc = new Arduino_PN532_SRIX(PN532_IRQ, PN532_RF_REST);
    #else
        LOG_INFO("SRIX", "Using polling mode (no IRQ pin defined)");
        nfc = new Arduino_PN532_SRIX(PN532_INVALID_PIN, PN532_INVALID_PIN);
    #endif
    
    // Initialize PN532
    if (nfc) {
        LOG_DEBUG("SRIX", "PN532 object created, initializing...");
        
        if (nfc->init()) {
            LOG_DEBUG("SRIX", "PN532 init successful, configuring...");
            
            // Configure passive activation retries
            nfc->setPassiveActivationRetries(PN532_MAX_RETRIES);
            
            // Initialize SRIX-specific settings
            nfc->SRIX_init();
            
            LOG_INFO("SRIX", "SRIX Tool ready");
        } else {
            LOG_ERROR("SRIX", "PN532 initialization failed (check wiring and power)");
        }
    } else {
        LOG_ERROR("SRIX", "Failed to create PN532 object (out of memory?)");
    }
}

// ============================================
// TAG DETECTION
// ============================================

bool SRIXTool::waitForTagHeadless(uint32_t timeout_ms) {
    if (!nfc) {
        LOG_ERROR("SRIX", "Cannot wait for tag: NFC object is NULL");
        return false;
    }
    
    LOG_DEBUG("SRIX", "Waiting for tag (timeout: %lu ms)", timeout_ms);
    
    uint32_t startTime = millis();
    
    // Poll for tag presence
    while ((millis() - startTime) < timeout_ms) {
        // Try initiate and select
        if (nfc->SRIX_initiate_select()) {
            LOG_INFO("SRIX", "Tag detected after %lu ms", millis() - startTime);
            return true;
        }
        
        // Delay between attempts to prevent I2C bus saturation
        delay(TAG_DETECT_DELAY_MS);
    }
    
    LOG_WARN("SRIX", "Tag detection timeout after %lu ms", timeout_ms);
    return false;
}

// ============================================
// READ OPERATIONS
// ============================================

String SRIXTool::read_tag_headless(int timeout_seconds) {
    if (!nfc) {
        LOG_ERROR("SRIX", "Cannot read: NFC object is NULL");
        return "";
    }
    
    LOG_INFO("SRIX", "Starting tag read (timeout: %d seconds)", timeout_seconds);
    
    uint32_t startTime = millis();
    uint32_t timeout_ms = timeout_seconds * 1000UL;
    
    while ((millis() - startTime) < timeout_ms) {
        // Try to detect tag
        if (!nfc->SRIX_initiate_select()) {
            delay(TAG_DETECT_DELAY_MS);
            continue;
        }
        
        LOG_DEBUG("SRIX", "Tag detected, reading UID...");
        
        // Read UID
        if (!nfc->SRIX_get_uid(_uid)) {
            LOG_WARN("SRIX", "Failed to read UID, retrying...");
            delay(TAG_DETECT_RETRY_DELAY_MS);
            continue;
        }
        
        LOG_DEBUG("SRIX", "UID read successful, reading blocks...");
        
        // Read all blocks
        uint8_t block[SRIX_BLOCK_SIZE];
        bool read_success = true;
        
        for (uint8_t b = 0; b < SRIX_BLOCK_COUNT; b++) {
            if (!nfc->SRIX_read_block(b, block)) {
                LOG_WARN("SRIX", "Failed to read block %d", b);
                read_success = false;
                break;
            }
            
            // Copy block to dump buffer
            const uint16_t offset = (uint16_t)b * SRIX_BLOCK_SIZE;
            _dump[offset + 0] = block[0];
            _dump[offset + 1] = block[1];
            _dump[offset + 2] = block[2];
            _dump[offset + 3] = block[3];
        }
        
        if (!read_success) {
            LOG_WARN("SRIX", "Incomplete read, retrying...");
            delay(TAG_DETECT_DELAY_MS);
            continue;
        }
        
        // Mark dump as valid from physical read
        _dump_valid_from_read = true;
        _dump_valid_from_load = false;
        
        LOG_INFO("SRIX", "Tag read successful: %d blocks", SRIX_BLOCK_COUNT);
        
        // Build UID string (8 bytes with spaces)
        String uid_str = "";
        for (uint8_t i = 0; i < SRIX_UID_SIZE; i++) {
            if (_uid[i] < HEX_PADDING_THRESHOLD) uid_str += "0";
            uid_str += String(_uid[i], HEX);
            if (i < SRIX_UID_SIZE - 1) uid_str += " ";
        }
        uid_str.toUpperCase();
        
        // Build data hex string (1024 hex chars = 512 bytes)
        String dump_str = "";
        dump_str.reserve(SRIX_TOTAL_SIZE * HEX_CHARS_PER_BYTE); // Pre-allocate memory
        
        for (uint16_t i = 0; i < SRIX_TOTAL_SIZE; i++) {
            if (_dump[i] < HEX_PADDING_THRESHOLD) dump_str += "0";
            dump_str += String(_dump[i], HEX);
        }
        dump_str.toUpperCase();
        
        // Build JSON response
        String result = "{";
        result += "\"uid\":\"" + uid_str + "\",";
        result += "\"blocks\":" + String(SRIX_BLOCK_COUNT) + ",";
        result += "\"size\":" + String(SRIX_TOTAL_SIZE) + ",";
        result += "\"data\":\"" + dump_str + "\"";
        result += "}";
        
        LOG_DEBUG("SRIX", "JSON response size: %d bytes", result.length());
        return result;
    }
    
    LOG_ERROR("SRIX", "Read timeout after %d seconds", timeout_seconds);
    return ""; // Timeout
}

// ============================================
// WRITE OPERATIONS
// ============================================

int SRIXTool::write_tag_headless(int timeout_seconds) {
    if (!nfc) {
        LOG_ERROR("SRIX", "Cannot write: NFC object is NULL");
        return ERROR_NULL_NFC;
    }
    
    // Check if dump is loaded
    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        LOG_ERROR("SRIX", "Cannot write: No dump loaded in memory");
        return ERROR_NO_DUMP;
    }
    
    LOG_INFO("SRIX", "Starting tag write (timeout: %d seconds)", timeout_seconds);
    
    // Wait for tag
    if (!waitForTagHeadless((uint32_t)timeout_seconds * 1000UL)) {
        LOG_ERROR("SRIX", "Write failed: Tag detection timeout");
        return ERROR_TIMEOUT;
    }
    
    LOG_INFO("SRIX", "Tag detected, starting write operation...");
    
    uint8_t block[SRIX_BLOCK_SIZE];
    uint8_t blocks_written = 0;
    
    // Write all blocks
    for (uint8_t b = 0; b < SRIX_BLOCK_COUNT; b++) {
        const uint16_t offset = (uint16_t)b * SRIX_BLOCK_SIZE;
        
        // Prepare block data
        block[0] = _dump[offset + 0];
        block[1] = _dump[offset + 1];
        block[2] = _dump[offset + 2];
        block[3] = _dump[offset + 3];
        
        // Write block (critical operation)
        if (!nfc->SRIX_write_block(b, block)) {
            LOG_ERROR("SRIX", "Write failed at block %d (written: %d/%d)", 
                     b, blocks_written, SRIX_BLOCK_COUNT);
            return ERROR_WRITE_FAILED;
        }
        
        blocks_written++;
        
        // Wait for EEPROM write cycle to complete
        delay(SRIX_EEPROM_WRITE_DELAY_MS);
        
        LOG_DEBUG("SRIX", "Block %d/%d written", blocks_written, SRIX_BLOCK_COUNT);
        
        // NOTE: Verify removed - read after write is unreliable on SRIX protocol
        // The tag needs time to complete internal write operations
        
        // Wait for tag to be ready again (required by protocol)
        if (!waitForTagHeadless(TAG_REDETECT_TIMEOUT_MS)) {
            LOG_WARN("SRIX", "Tag lost at block %d (may have completed successfully)", b);
            return blocks_written;
        }
    }
    
    LOG_INFO("SRIX", "Write complete: %d/%d blocks written successfully", 
             blocks_written, SRIX_BLOCK_COUNT);
    
    return SUCCESS; // All blocks written
}

int SRIXTool::write_single_block_headless(uint8_t block_num, const uint8_t *block_data) {
    // Validate NFC object
    if (!nfc) {
        LOG_ERROR("SRIX", "Cannot write block: NFC object is NULL");
        return ERROR_NULL_NFC;
    }
    
    // Validate block number
    if (block_num > SRIX_MAX_BLOCK_NUM) {
        LOG_ERROR("SRIX", "Invalid block number: %d (max: %d)", 
                 block_num, SRIX_MAX_BLOCK_NUM);
        return ERROR_INVALID_BLOCK;
    }
    
    // Validate data pointer
    if (!block_data) {
        LOG_ERROR("SRIX", "Invalid data pointer");
        return ERROR_INVALID_PTR;
    }
    
    LOG_INFO("SRIX", "Writing single block %d: %02X %02X %02X %02X",
             block_num, block_data[0], block_data[1], block_data[2], block_data[3]);
    
    // Wait for tag
    if (!waitForTagHeadless(SINGLE_BLOCK_TIMEOUT_MS)) {
        LOG_ERROR("SRIX", "Tag not found within timeout");
        return ERROR_TIMEOUT;
    }
    
    LOG_DEBUG("SRIX", "Tag ready, sending write command...");
    
    // Write block
    if (nfc->SRIX_write_block(block_num, (uint8_t *)block_data)) {
        // Wait for hardware write to complete
        delay(SRIX_EEPROM_WRITE_DELAY_MS);
        
        LOG_INFO("SRIX", "Write command sent successfully");
        
        // ============================================
        // VERIFY (needs protocol upgrade to be reliable)
        // ============================================
        
        uint8_t readBuffer[SRIX_BLOCK_SIZE];
        
        // Try to re-select tag
        if (!nfc->SRIX_initiate_select()) {
            LOG_WARN("SRIX", "Re-select failed after write (verify skipped)");
            return SUCCESS_VERIFY_SKIP;
        }
        
        delay(VERIFY_DELAY_MS);
        
        // Try to read back written block
        if (nfc->SRIX_read_block(block_num, readBuffer)) {
            delay(VERIFY_READ_DELAY_MS);
            
            // Compare written vs. read data
            if (memcmp(block_data, readBuffer, SRIX_BLOCK_SIZE) == 0) {
                LOG_INFO("SRIX", "Write verified successfully");
                return SUCCESS; // Write + verify OK
            } else {
                LOG_WARN("SRIX", "Verify mismatch (write likely OK, read may be stale)");
                LOG_DEBUG("SRIX", "Written: %02X %02X %02X %02X, Read: %02X %02X %02X %02X",
                         block_data[0], block_data[1], block_data[2], block_data[3],
                         readBuffer[0], readBuffer[1], readBuffer[2], readBuffer[3]);
                return SUCCESS_VERIFY_FAIL; // Write OK, verify failed
            }
        }
        
        LOG_WARN("SRIX", "Verify skipped (read failed, RF state uncertain)");
        return SUCCESS_VERIFY_SKIP; // Write OK, verify not possible
        
    } else {
        LOG_ERROR("SRIX", "Write command failed");
        return ERROR_WRITE_FAILED;
    }
}

// ============================================
// FILE OPERATIONS
// ============================================

String SRIXTool::save_file_headless(String filename) {
    // Check if dump is valid
    if (!_dump_valid_from_read && !_dump_valid_from_load) {
        LOG_ERROR("SRIX", "Cannot save: No valid dump in memory");
        return "";
    }
    
    LOG_INFO("SRIX", "Saving dump to file: %s", filename.c_str());
    
    // Create directories if needed
    if (!LittleFS.exists(NFC_DUMP_FOLDER)) {
        if (LittleFS.mkdir(NFC_DUMP_FOLDER)) {
            LOG_DEBUG("SRIX", "Created directory: %s", NFC_DUMP_FOLDER);
        } else {
            LOG_ERROR("SRIX", "Failed to create directory: %s", NFC_DUMP_FOLDER);
            return "";
        }
    }
    
    if (!LittleFS.exists(NFC_SRIX_DUMP_FOLDER)) {
        if (LittleFS.mkdir(NFC_SRIX_DUMP_FOLDER)) {
            LOG_DEBUG("SRIX", "Created directory: %s", NFC_SRIX_DUMP_FOLDER);
        } else {
            LOG_ERROR("SRIX", "Failed to create directory: %s", NFC_SRIX_DUMP_FOLDER);
            return "";
        }
    }
    
    // Build filepath
    String filepath = NFC_SRIX_DUMP_FOLDER + filename;
    if (!filename.endsWith(".srix")) {
        filepath += ".srix";
    }
    
    // Handle existing file (append number suffix)
    if (LittleFS.exists(filepath)) {
        int i = 1;
        String base = filepath.substring(0, filepath.length() - SRIX_FILE_EXTENSION_LEN);
        
        while (LittleFS.exists(base + "_" + String(i) + ".srix")) {
            i++;
        }
        
        filepath = base + "_" + String(i) + ".srix";
        LOG_INFO("SRIX", "File exists, using: %s", filepath.c_str());
    }
    
    // Open file for writing
    File file = LittleFS.open(filepath, FILE_WRITE);
    if (!file) {
        LOG_ERROR("SRIX", "Failed to open file for writing: %s", filepath.c_str());
        return "";
    }
    
    // Build UID string (16 hex chars, no spaces)
    String uid_str = "";
    for (uint8_t i = 0; i < SRIX_UID_SIZE; i++) {
        if (_uid[i] < HEX_PADDING_THRESHOLD) uid_str += "0";
        uid_str += String(_uid[i], HEX);
    }
    uid_str.toUpperCase();
    
    // Write header (Flipper-compatible format)
    file.println("Filetype: SRIX Dump");
    file.println("UID: " + uid_str);
    file.println("Blocks: " + String(SRIX_BLOCK_COUNT));
    file.println("Data size: " + String(SRIX_TOTAL_SIZE));
    file.println("# Data:");
    
    // Write blocks in format: [XX] YYYYYYYY
    for (uint8_t block = 0; block < SRIX_BLOCK_COUNT; block++) {
        uint16_t offset = block * SRIX_BLOCK_SIZE;
        
        // Block number in hex
        String line = "[";
        if (block < HEX_PADDING_THRESHOLD) line += "0";
        line += String(block, HEX);
        line += "] ";
        
        // Block data (4 bytes = 8 hex chars)
        for (uint8_t i = 0; i < SRIX_BLOCK_SIZE; i++) {
            if (_dump[offset + i] < HEX_PADDING_THRESHOLD) line += "0";
            line += String(_dump[offset + i], HEX);
        }
        
        line.toUpperCase();
        file.println(line);
    }
    
    file.close();
    
    LOG_INFO("SRIX", "File saved successfully: %s (%d blocks)", filepath.c_str(), SRIX_BLOCK_COUNT);
    return filepath;
}

int SRIXTool::load_file_headless(String filename) {
    if (!nfc) {
        LOG_ERROR("SRIX", "Cannot load: NFC object is NULL");
        return ERROR_TIMEOUT;
    }
    
    LOG_INFO("SRIX", "Loading dump from file: %s", filename.c_str());
    
    // Build filepath
    String filepath = NFC_SRIX_DUMP_FOLDER + filename;
    if (!filename.endsWith(".srix")) {
        filepath += ".srix";
    }
    
    // Check if file exists
    if (!LittleFS.exists(filepath)) {
        LOG_ERROR("SRIX", "File not found: %s", filepath.c_str());
        return ERROR_FILE_NOT_FOUND;
    }
    
    // Open file
    File file = LittleFS.open(filepath, FILE_READ);
    if (!file) {
        LOG_ERROR("SRIX", "Failed to open file: %s", filepath.c_str());
        return ERROR_TIMEOUT;
    }
    
    // Reset buffers
    memset(_dump, 0, sizeof(_dump));
    memset(_uid, 0, sizeof(_uid));
    
    bool header_passed = false;
    int blocks_loaded = 0;
    
    LOG_DEBUG("SRIX", "Parsing file...");
    
    // Parse file line by line
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        // Skip empty lines
        if (line.isEmpty()) continue;
        
        // Parse UID from header
        if (line.startsWith("UID:")) {
            String uid_str = line.substring(5);
            uid_str.trim();
            uid_str.replace(" ", "");
            
            // Convert hex string to bytes
            for (uint8_t i = 0; i < SRIX_UID_SIZE && i * HEX_CHARS_PER_BYTE < uid_str.length(); i++) {
                String byteStr = uid_str.substring(i * HEX_CHARS_PER_BYTE, i * HEX_CHARS_PER_BYTE + HEX_CHARS_PER_BYTE);
                _uid[i] = strtoul(byteStr.c_str(), NULL, 16);
            }
            
            LOG_DEBUG("SRIX", "UID loaded: %02X%02X%02X%02X%02X%02X%02X%02X",
                     _uid[0], _uid[1], _uid[2], _uid[3], _uid[4], _uid[5], _uid[6], _uid[7]);
            continue;
        }
        
        // Skip header until "# Data:" marker
        if (!header_passed) {
            if (line.startsWith("# Data:")) {
                header_passed = true;
            }
            continue;
        }
        
        // Parse blocks in format: [XX] YYYYYYYY
        if (line.startsWith("[") && line.indexOf("]") > 0) {
            int bracket_end = line.indexOf("]");
            String block_num_str = line.substring(1, bracket_end);
            String data_str = line.substring(bracket_end + 1);
            data_str.trim();
            data_str.replace(" ", "");
            
            uint8_t block_num = strtoul(block_num_str.c_str(), NULL, 16);
            
            // Skip invalid block numbers
            if (block_num >= SRIX_BLOCK_COUNT) {
                LOG_WARN("SRIX", "Invalid block number: %d", block_num);
                continue;
            }
            
            // Convert 8 hex chars to 4 bytes
            if (data_str.length() >= SRIX_BLOCK_SIZE * HEX_CHARS_PER_BYTE) {
                uint16_t offset = block_num * SRIX_BLOCK_SIZE;
                
                for (uint8_t i = 0; i < SRIX_BLOCK_SIZE; i++) {
                    String byteStr = data_str.substring(i * HEX_CHARS_PER_BYTE, i * HEX_CHARS_PER_BYTE + HEX_CHARS_PER_BYTE);
                    _dump[offset + i] = strtoul(byteStr.c_str(), NULL, 16);
                }
                
                blocks_loaded++;
            }
        }
    }
    
    file.close();
    
    // Check if dump is complete
    if (blocks_loaded < SRIX_BLOCK_COUNT) {
        LOG_ERROR("SRIX", "Incomplete dump: only %d/%d blocks loaded", 
                 blocks_loaded, SRIX_BLOCK_COUNT);
        return ERROR_INCOMPLETE_DUMP;
    }
    
    // Mark dump as valid from loaded file
    _dump_valid_from_load = true;
    _dump_valid_from_read = false;
    
    LOG_INFO("SRIX", "File loaded successfully: %d blocks", blocks_loaded);
    return SUCCESS;
}
