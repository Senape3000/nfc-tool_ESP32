#include "bt_security.h"
#include <Arduino.h>
#include <esp_random.h>

// ========================================
// CONSTRUCTOR & DESTRUCTOR
// ========================================

BTSecurity::BTSecurity(SecurityLevel level)
    : _security_level(level)
    , _checksum_threshold(BT_CHECKSUM_THRESHOLD)
    , _verbose_logging(false) {
    
    // Reset statistics
    memset(&_stats, 0, sizeof(_stats));
    memset(&_current_transfer, 0, sizeof(_current_transfer));
    
    _stats.uptime_ms = millis();
    
    LOG_INFO("BT_SEC", "Bluetooth Security initialized (Level: %d)", _security_level);
}

BTSecurity::~BTSecurity() {
    if (_current_transfer.active) {
        cancelTransfer();
    }
    
    LOG_INFO("BT_SEC", "Bluetooth Security destroyed");
}

// ========================================
// CHECKSUM OPERATIONS
// ========================================

BTSecurity::SecurityResult BTSecurity::calculateChecksum(const uint8_t* data, size_t length, uint8_t* checksum) {
    if (!data || !checksum || length == 0) {
        logSecurity("calculateChecksum", SEC_ERROR, "Invalid parameters");
        return SEC_ERROR;
    }
    
    int ret = mbedtls_sha256(data, length, checksum, 0);  // 0 = SHA-256
    
    if (ret != 0) {
        logSecurity("calculateChecksum", SEC_ERROR, "mbedtls_sha256 failed");
        return SEC_ERROR;
    }
    
    _stats.checksums_calculated++;
    _stats.bytes_processed += length;
    
    logSecurity("calculateChecksum", SEC_SUCCESS, String(length) + " bytes");
    
    return SEC_SUCCESS;
}

BTSecurity::SecurityResult BTSecurity::verifyChecksum(const uint8_t* data, size_t length, const uint8_t* expected_checksum) {
    if (!data || !expected_checksum || length == 0) {
        logSecurity("verifyChecksum", SEC_ERROR, "Invalid parameters");
        return SEC_ERROR;
    }
    
    uint8_t calculated_checksum[32];
    SecurityResult result = calculateChecksum(data, length, calculated_checksum);
    
    if (result != SEC_SUCCESS) {
        return result;
    }
    
    bool matches = (memcmp(calculated_checksum, expected_checksum, 32) == 0);
    
    if (matches) {
        _stats.checksums_validated++;
        logSecurity("verifyChecksum", SEC_SUCCESS, "Checksum matches");
        return SEC_SUCCESS;
    } else {
        _stats.checksum_failures++;
        logSecurity("verifyChecksum", SEC_CHECKSUM_MISMATCH, "Checksum mismatch");
        return SEC_CHECKSUM_MISMATCH;
    }
}

BTSecurity::SecurityResult BTSecurity::calculateChecksumJSON(const JsonDocument& doc, uint8_t* checksum) {
    if (!checksum) {
        return SEC_ERROR;
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    return calculateChecksum((const uint8_t*)jsonString.c_str(), jsonString.length(), checksum);
}

BTSecurity::SecurityResult BTSecurity::verifyChecksumJSON(const JsonDocument& doc, const uint8_t* expected_checksum) {
    if (!expected_checksum) {
        return SEC_ERROR;
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    return verifyChecksum((const uint8_t*)jsonString.c_str(), jsonString.length(), expected_checksum);
}

// ========================================
// DATA ENCODING
// ========================================

BTSecurity::SecurityResult BTSecurity::encodeBase64(const uint8_t* data, size_t length, String& encoded) {
    SecurityResult result = bytesToBase64(data, length, encoded);
    
    if (result == SEC_SUCCESS) {
        _stats.data_encoded++;
        logSecurity("encodeBase64", SEC_SUCCESS, String(length) + " bytes to " + String(encoded.length()) + " chars");
    }
    
    return result;
}

BTSecurity::SecurityResult BTSecurity::decodeBase64(const String& encoded, uint8_t* data, size_t max_length, size_t& decoded_length) {
    SecurityResult result = base64ToBytes(encoded, data, max_length, decoded_length);
    
    if (result == SEC_SUCCESS) {
        _stats.data_decoded++;
        logSecurity("decodeBase64", SEC_SUCCESS, String(encoded.length()) + " chars to " + String(decoded_length) + " bytes");
    }
    
    return result;
}

BTSecurity::SecurityResult BTSecurity::encodeHex(const uint8_t* data, size_t length, String& hex) {
    if (!data || length == 0) {
        return SEC_ERROR;
    }
    
    hex = "";
    hex.reserve(length * 2);
    
    for (size_t i = 0; i < length; i++) {
        char byteStr[4];
        snprintf(byteStr, sizeof(byteStr), "%02X", data[i]);
        hex += byteStr;
    }
    
    return SEC_SUCCESS;
}

BTSecurity::SecurityResult BTSecurity::decodeHex(const String& hex, uint8_t* data, size_t max_length, size_t& decoded_length) {
    if (hex.length() % 2 != 0 || !data || max_length == 0) {
        return SEC_ERROR;
    }
    
    size_t hexLen = hex.length();
    decoded_length = min(hexLen / 2, max_length);
    
    for (size_t i = 0; i < decoded_length; i++) {
        uint8_t byte = 0;
        char high = hex[i * 2];
        char low = hex[i * 2 + 1];
        
        if (high >= '0' && high <= '9') byte = (high - '0') << 4;
        else if (high >= 'A' && high <= 'F') byte = (high - 'A' + 10) << 4;
        else if (high >= 'a' && high <= 'f') byte = (high - 'a' + 10) << 4;
        else return SEC_INVALID_DATA;
        
        if (low >= '0' && low <= '9') byte |= (low - '0');
        else if (low >= 'A' && low <= 'F') byte |= (low - 'A' + 10);
        else if (low >= 'a' && low <= 'f') byte |= (low - 'a' + 10);
        else return SEC_INVALID_DATA;
        
        data[i] = byte;
    }
    
    return SEC_SUCCESS;
}

// ========================================
// SECURE TRANSFER
// ========================================

BTSecurity::SecurityResult BTSecurity::prepareSecureResponse(JsonDocument& response, size_t data_size, EncodingType encoding) {
    if (_security_level < LEVEL_CHECKSUM || data_size < _checksum_threshold) {
        return SEC_SUCCESS;  // No security needed
    }
    
    // Calculate checksum of current response
    uint8_t checksum[32];
    SecurityResult result = calculateChecksumJSON(response, checksum);
    
    if (result != SEC_SUCCESS) {
        return result;
    }
    
    // Add security metadata
    response["security"] = JsonObject();
    JsonObject security = response["security"].to<JsonObject>();
    security["checksum"] = "";
    security["checksum_size"] = 32;
    security["data_size"] = data_size;
    security["encoding"] = encoding == ENCODING_BASE64 ? "base64" : (encoding == ENCODING_HEX ? "hex" : "none");
    security["security_level"] = (int)_security_level;
    
    // Convert checksum to hex string
    String checksumHex;
    encodeHex(checksum, 32, checksumHex);
    security["checksum"] = checksumHex;
    
    _stats.transfers_prepared++;
    
    logSecurity("prepareSecureResponse", SEC_SUCCESS, "Added security metadata");
    
    return SEC_SUCCESS;
}

BTSecurity::SecurityResult BTSecurity::validateSecureResponse(JsonDocument& response) {
    if (!response["security"].is<JsonObject>()) {
        return SEC_SUCCESS;  // No security metadata to validate
    }
    
    JsonObject security = response["security"];
    
    if (!security["checksum"].is<const char*>() || !security["checksum_size"].is<size_t>()) {
        LOG_ERROR("BT_SEC", "Invalid security fields");
        return SEC_INVALID_DATA;
    }
    
    // Remove security metadata temporarily for checksum calculation
    JsonDocument tempDoc;
    tempDoc.set(response);
    tempDoc.remove("security");
    
    // Calculate checksum of response without security metadata
    uint8_t calculated_checksum[32];
    SecurityResult result = calculateChecksumJSON(tempDoc, calculated_checksum);
    
    if (result != SEC_SUCCESS) {
        return result;
    }
    
    // Compare with expected checksum
    String expectedChecksumHex = security["checksum"];
    uint8_t expected_checksum[32];
    size_t decoded_length = 0;
    
    if (decodeHex(expectedChecksumHex, expected_checksum, 32, decoded_length) != SEC_SUCCESS || decoded_length != 32) {
        LOG_ERROR("BT_SEC", "Invalid checksum format");
        return SEC_INVALID_DATA;
    }
    
    return verifyChecksum(calculated_checksum, 32, expected_checksum);
}

BTSecurity::SecurityResult BTSecurity::prepareSecureTransfer(const uint8_t* file_data, size_t file_size, 
                                                            size_t chunk_size, JsonDocument& response) {
    if (!file_data || file_size == 0) {
        return SEC_ERROR;
    }
    
    // Calculate total chunks needed
    uint32_t total_chunks = (file_size + chunk_size - 1) / chunk_size;
    
    // Calculate overall checksum
    uint8_t overall_checksum[32];
    SecurityResult result = calculateChecksum(file_data, file_size, overall_checksum);
    
    if (result != SEC_SUCCESS) {
        return result;
    }
    
    // Generate transfer ID
    String transferId;
    generateSecureId(transferId, 16);
    
    // Initialize transfer context
    result = initTransfer(transferId, file_size, total_chunks);
    if (result != SEC_SUCCESS) {
        return result;
    }
    
    // Prepare response with transfer metadata
    response["transfer"] = JsonObject();
    JsonObject transfer = response["transfer"].to<JsonObject>();
    transfer["id"] = transferId;
    transfer["total_size"] = file_size;
    transfer["chunk_size"] = chunk_size;
    transfer["total_chunks"] = total_chunks;
    transfer["overall_checksum"] = "";
    transfer["security_level"] = (int)_security_level;
    
    // Convert checksum to hex
    String checksumHex;
    encodeHex(overall_checksum, 32, checksumHex);
    transfer["overall_checksum"] = checksumHex;
    
    // Add first chunk if requested
    transfer["ready"] = true;
    
    _stats.transfers_prepared++;
    
    logSecurity("prepareSecureTransfer", SEC_SUCCESS, "Transfer prepared: " + transferId);
    
    return SEC_SUCCESS;
}

BTSecurity::SecurityResult BTSecurity::processTransferChunk(const uint8_t* chunk_data, size_t chunk_size,
                                                            uint32_t chunk_index, uint32_t total_chunks,
                                                            const String& transfer_id, JsonDocument& response) {
    if (!chunk_data || chunk_size == 0 || !_current_transfer.active) {
        return SEC_ERROR;
    }
    
    if (transfer_id != _current_transfer.transfer_id) {
        logSecurity("processTransferChunk", SEC_ERROR, "Transfer ID mismatch");
        return SEC_ERROR;
    }
    
    if (chunk_index >= total_chunks) {
        logSecurity("processTransferChunk", SEC_ERROR, "Invalid chunk index");
        return SEC_ERROR;
    }
    
    // Update transfer progress
    SecurityResult result = updateTransfer(chunk_data, chunk_size, chunk_index);
    if (result != SEC_SUCCESS) {
        return result;
    }
    
    // Prepare response with progress information
    response["transfer_progress"] = JsonObject();
    JsonObject progress = response["transfer_progress"].to<JsonObject>();
    progress["transfer_id"] = transfer_id;
    progress["chunk_index"] = chunk_index;
    progress["received_chunks"] = _current_transfer.received_chunks;
    progress["total_chunks"] = total_chunks;
    progress["received_size"] = _current_transfer.received_size;
    progress["total_size"] = _current_transfer.total_size;
    progress["complete"] = (_current_transfer.received_chunks == total_chunks);
    
    // Check if transfer is complete
    if (_current_transfer.received_chunks == total_chunks) {
        result = finalizeTransfer();
        if (result != SEC_SUCCESS) {
            progress["validation"] = "failed";
            progress["validation_error"] = resultToString(result);
        } else {
            progress["validation"] = "success";
        }
    }
    
    _stats.transfers_validated++;
    
    logSecurity("processTransferChunk", SEC_SUCCESS, "Chunk " + String(chunk_index) + " processed");
    
    return result;
}

// ========================================
// VALIDATION UTILITIES
// ========================================

BTSecurity::SecurityResult BTSecurity::validateCommand(const JsonDocument& command, const char** required_fields, size_t field_count) {
    if (!required_fields || field_count == 0) {
        return SEC_SUCCESS;
    }
    
    for (size_t i = 0; i < field_count; i++) {
        if (!command[required_fields[i]].is<const char*>()) {
            logSecurity("validateCommand", SEC_INVALID_DATA, "Missing required field: " + String(required_fields[i]));
            return SEC_INVALID_DATA;
        }
    }
    
    return SEC_SUCCESS;
}

BTSecurity::SecurityResult BTSecurity::validateFilename(const String& filename) {
    if (filename.length() == 0 || filename.length() > 255) {
        logSecurity("validateFilename", SEC_INVALID_DATA, "Invalid filename length");
        return SEC_INVALID_DATA;
    }
    
    if (containsDangerousChars(filename)) {
        logSecurity("validateFilename", SEC_INVALID_DATA, "Dangerous characters in filename");
        return SEC_INVALID_DATA;
    }
    
    // Check for directory traversal
    if (filename.indexOf("..") != -1 || filename.indexOf("//") != -1) {
        logSecurity("validateFilename", SEC_INVALID_DATA, "Directory traversal attempt");
        return SEC_INVALID_DATA;
    }
    
    return SEC_SUCCESS;
}

BTSecurity::SecurityResult BTSecurity::sanitizeString(const String& input, String& sanitized) {
    sanitized = input;
    removeDangerousChars(sanitized);
    
    // Limit length
    if (sanitized.length() > 1024) {
        sanitized = sanitized.substring(0, 1024);
    }
    
    return SEC_SUCCESS;
}

BTSecurity::SecurityResult BTSecurity::generateSecureId(String& id, size_t length) {
    if (length < 8 || length > 64) {
        return SEC_ERROR;
    }
    
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    const size_t charset_size = sizeof(charset) - 1;
    
    uint8_t random_bytes[32];
    size_t bytes_needed = (length + 1) / 2;
    
    SecurityResult result = generateRandomBytes(random_bytes, bytes_needed);
    if (result != SEC_SUCCESS) {
        return result;
    }
    
    id = "";
    id.reserve(length);
    
    for (size_t i = 0; i < length; i++) {
        size_t byte_index = i / 2;
        size_t bit_shift = (i % 2) * 4;
        uint8_t random_value = (random_bytes[byte_index] >> bit_shift) & 0x0F;
        id += charset[random_value % charset_size];
    }
    
    return SEC_SUCCESS;
}

// ========================================
// STATISTICS
// ========================================

JsonDocument BTSecurity::getStatistics() {
    JsonDocument stats;
    
    stats["security_level"] = (int)_security_level;
    stats["checksum_threshold"] = _checksum_threshold;
    stats["checksums_calculated"] = _stats.checksums_calculated;
    stats["checksums_validated"] = _stats.checksums_validated;
    stats["checksum_failures"] = _stats.checksum_failures;
    stats["data_encoded"] = _stats.data_encoded;
    stats["data_decoded"] = _stats.data_decoded;
    stats["transfers_prepared"] = _stats.transfers_prepared;
    stats["transfers_validated"] = _stats.transfers_validated;
    stats["validation_failures"] = _stats.validation_failures;
    stats["bytes_processed"] = _stats.bytes_processed;
    stats["uptime_ms"] = _stats.uptime_ms;
    stats["active_transfer"] = _current_transfer.active;
    
    if (_current_transfer.active) {
        stats["current_transfer"] = JsonObject();
        JsonObject current = stats["current_transfer"].to<JsonObject>();
        current["id"] = _current_transfer.transfer_id;
        current["progress"] = (float)_current_transfer.received_chunks / _current_transfer.total_chunks * 100.0f;
        current["received_size"] = _current_transfer.received_size;
        current["total_size"] = _current_transfer.total_size;
    }
    
    return stats;
}

void BTSecurity::resetStatistics() {
    memset(&_stats, 0, sizeof(_stats));
    _stats.uptime_ms = millis();
    
    LOG_INFO("BT_SEC", "Security statistics reset");
}

// ========================================
// PRIVATE HELPER METHODS
// ========================================

BTSecurity::SecurityResult BTSecurity::bytesToBase64(const uint8_t* data, size_t length, String& base64) {
    if (!data || length == 0) {
        return SEC_ERROR;
    }
    
    const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    base64 = "";
    base64.reserve(((length + 2) / 3) * 4);
    
    for (size_t i = 0; i < length; i += 3) {
        uint32_t triple = 0;
        
        // Pack 3 bytes into a 24-bit value
        triple |= ((uint32_t)data[i]) << 16;
        if (i + 1 < length) triple |= ((uint32_t)data[i + 1]) << 8;
        if (i + 2 < length) triple |= ((uint32_t)data[i + 2]);
        
        // Extract 6-bit values and map to Base64 characters
        base64 += alphabet[(triple >> 18) & 0x3F];
        base64 += alphabet[(triple >> 12) & 0x3F];
        base64 += (i + 1 < length) ? alphabet[(triple >> 6) & 0x3F] : '=';  
        base64 += (i + 2 < length) ? alphabet[triple & 0x3F] : '=';
    }
    
    return SEC_SUCCESS;
}

BTSecurity::SecurityResult BTSecurity::base64ToBytes(const String& base64, uint8_t* data, size_t max_length, size_t& decoded_length) {
    if (base64.length() % 4 != 0 || !data || max_length == 0) {
        return SEC_ERROR;
    }
    
    decoded_length = 0;
    
    for (size_t i = 0; i < base64.length(); i += 4) {
        if (decoded_length + 3 > max_length) {
            return SEC_BUFFER_TOO_SMALL;
        }
        
        uint32_t quadruple = 0;
        int padding = 0;
        
        // Convert 4 Base64 characters to 24-bit value
        for (int j = 0; j < 4; j++) {
            char c = base64[i + j];
            
            if (c == '=') {
                padding++;
                quadruple <<= 6;
            } else {
                int8_t value = base64Value(c);
                if (value < 0) {
                    return SEC_INVALID_DATA;
                }
                quadruple = (quadruple << 6) | value;
            }
        }
        
        // Extract 3 bytes from 24-bit value
        if (padding < 3) {
            data[decoded_length++] = (quadruple >> 16) & 0xFF;
        }
        if (padding < 2) {
            data[decoded_length++] = (quadruple >> 8) & 0xFF;
        }
        if (padding < 1) {
            data[decoded_length++] = quadruple & 0xFF;
        }
    }
    
    return SEC_SUCCESS;
}

char BTSecurity::base64Char(uint8_t value) {
    const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    return alphabet[value & 0x3F];
}

int8_t BTSecurity::base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return 0;  // Padding
    return -1;
}

BTSecurity::SecurityResult BTSecurity::initTransfer(const String& transfer_id, size_t total_size, uint32_t total_chunks) {
    // Cancel any existing transfer
    if (_current_transfer.active) {
        cancelTransfer();
    }
    
    _current_transfer.transfer_id = transfer_id;
    _current_transfer.total_size = total_size;
    _current_transfer.received_size = 0;
    _current_transfer.total_chunks = total_chunks;
    _current_transfer.received_chunks = 0;
    _current_transfer.start_time = millis();
    _current_transfer.active = true;
    
    // Initialize checksum buffer
    memset(_current_transfer.overall_checksum, 0, 32);
    
    logSecurity("initTransfer", SEC_SUCCESS, "Transfer initialized: " + transfer_id);
    
    return SEC_SUCCESS;
}

BTSecurity::SecurityResult BTSecurity::updateTransfer(const uint8_t* chunk_data, size_t chunk_size, uint32_t chunk_index) {
    if (!_current_transfer.active || !chunk_data) {
        return SEC_ERROR;
    }
    
    // Update progress
    _current_transfer.received_size += chunk_size;
    _current_transfer.received_chunks++;
    
    // Update checksum incrementally (simplified - in production would use streaming SHA-256)
    // For now, we'll just validate chunk boundaries
    if (_current_transfer.received_size > _current_transfer.total_size) {
        logSecurity("updateTransfer", SEC_ERROR, "Transfer size exceeded");
        return SEC_ERROR;
    }
    
    return SEC_SUCCESS;
}

BTSecurity::SecurityResult BTSecurity::finalizeTransfer() {
    if (!_current_transfer.active) {
        return SEC_ERROR;
    }
    
    // Validate transfer completeness
    if (_current_transfer.received_chunks != _current_transfer.total_chunks ||
        _current_transfer.received_size != _current_transfer.total_size) {
        logSecurity("finalizeTransfer", SEC_ERROR, "Transfer incomplete");
        _stats.validation_failures++;
        return SEC_ERROR;
    }
    
    uint32_t duration = millis() - _current_transfer.start_time;
    
    logSecurity("finalizeTransfer", SEC_SUCCESS, 
               "Transfer completed: " + _current_transfer.transfer_id + 
               " (" + String(duration) + "ms)");
    
    // Reset transfer context
    memset(&_current_transfer, 0, sizeof(_current_transfer));
    
    return SEC_SUCCESS;
}

void BTSecurity::cancelTransfer() {
    if (_current_transfer.active) {
        logSecurity("cancelTransfer", SEC_SUCCESS, "Transfer cancelled: " + _current_transfer.transfer_id);
        memset(&_current_transfer, 0, sizeof(_current_transfer));
        _stats.validation_failures++;
    }
}

String BTSecurity::resultToString(SecurityResult result) {
    switch (result) {
        case SEC_SUCCESS: return "success";
        case SEC_ERROR: return "error";
        case SEC_CHECKSUM_MISMATCH: return "checksum_mismatch";
        case SEC_INVALID_DATA: return "invalid_data";
        case SEC_BUFFER_TOO_SMALL: return "buffer_too_small";
        case SEC_NOT_IMPLEMENTED: return "not_implemented";
        case SEC_TIMEOUT: return "timeout";
        default: return "unknown";
    }
}

void BTSecurity::logSecurity(const String& operation, SecurityResult result, const String& details) {
    if (_verbose_logging || result != SEC_SUCCESS) {
        LOG_INFO("BT_SEC", "%s: %s - %s", operation.c_str(), resultToString(result).c_str(), details.c_str());
    }
}

bool BTSecurity::containsDangerousChars(const String& str) {
    const char* dangerous = "<>\"'\\/&;`|(){}[]$*";
    for (const char* c = dangerous; *c; c++) {
        if (str.indexOf(*c) != -1) {
            return true;
        }
    }
    return false;
}

void BTSecurity::removeDangerousChars(String& str) {
    const char* dangerous = "<>\"'\\/&;`|(){}[]$*";
    for (const char* c = dangerous; *c; c++) {
        str.replace(String(*c), "");
    }
}

BTSecurity::SecurityResult BTSecurity::generateRandomBytes(uint8_t* buffer, size_t length) {
    if (!buffer || length == 0) {
        return SEC_ERROR;
    }
    
    // Use ESP32 hardware random number generator
    for (size_t i = 0; i < length; i++) {
        buffer[i] = (uint8_t)esp_random();
    }
    
    return SEC_SUCCESS;
}