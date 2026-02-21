#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include "config.h"
#include "logger.h"

/**
 * @file bt_security.h
 * @brief Bluetooth Security and Data Validation Module
 * 
 * Provides secure data transfer functionality for Bluetooth communication:
 * - SHA-256 checksum calculation and verification
 * - Data integrity validation
 * - Secure file transfer protocols
 * - Encryption utilities (future extension)
 * 
 * Features:
 * - Automatic checksum for transfers > 1KB
 * - Base64 encoding for binary data
 * - Chunked transfer for large files
 * - Progress tracking and validation
 * 
 * Usage:
 * @code
 * BTSecurity security;
 * uint8_t checksum[32];
 * security.calculateChecksum(data, size, checksum);
 * 
 * if (security.verifyChecksum(data, size, receivedChecksum)) {
 *     // Data is valid
 * }
 * @endcode
 */

class BTSecurity {
public:
    /**
     * @brief Security operation results
     */
    enum SecurityResult {
        SEC_SUCCESS = 0,           ///< Operation successful
        SEC_ERROR = -1,             ///< General error
        SEC_CHECKSUM_MISMATCH = -2, ///< Checksum validation failed
        SEC_INVALID_DATA = -3,      ///< Invalid data format
        SEC_BUFFER_TOO_SMALL = -4,  ///< Output buffer too small
        SEC_NOT_IMPLEMENTED = -5,   ///< Feature not implemented
        SEC_TIMEOUT = -6           ///< Operation timeout
    };

    /**
     * @brief Data encoding options
     */
    enum EncodingType {
        ENCODING_NONE = 0,         ///< No encoding
        ENCODING_BASE64 = 1,       ///< Base64 encoding
        ENCODING_HEX = 2           ///< Hexadecimal encoding
    };

    /**
     * @brief Transfer security levels
     */
    enum SecurityLevel {
        LEVEL_NONE = 0,            ///< No security
        LEVEL_CHECKSUM = 1,        ///< Checksum validation only
        LEVEL_FULL = 2             ///< Full security (future: encryption)
    };

    // ============================================
    // CONSTRUCTOR
    // ============================================
    
    /**
     * @brief Constructor
     * @param level Security level for operations
     */
    BTSecurity(SecurityLevel level = LEVEL_CHECKSUM);

    /**
     * @brief Destructor
     */
    ~BTSecurity();

    // ============================================
    // CHECKSUM OPERATIONS
    // ============================================
    
    /**
     * @brief Calculate SHA-256 checksum
     * @param data Pointer to data buffer
     * @param length Data length in bytes
     * @param checksum Output 32-byte checksum buffer
     * @return SecurityResult
     */
    SecurityResult calculateChecksum(const uint8_t* data, size_t length, uint8_t* checksum);

    /**
     * @brief Verify SHA-256 checksum
     * @param data Pointer to data buffer
     * @param length Data length in bytes
     * @param expected_checksum Expected checksum (32 bytes)
     * @return SecurityResult
     */
    SecurityResult verifyChecksum(const uint8_t* data, size_t length, const uint8_t* expected_checksum);

    /**
     * @brief Calculate checksum for JSON document
     * @param doc JSON document
     * @param checksum Output 32-byte checksum buffer
     * @return SecurityResult
     */
    SecurityResult calculateChecksumJSON(const JsonDocument& doc, uint8_t* checksum);

    /**
     * @brief Verify checksum for JSON document
     * @param doc JSON document
     * @param expected_checksum Expected checksum (32 bytes)
     * @return SecurityResult
     */
    SecurityResult verifyChecksumJSON(const JsonDocument& doc, const uint8_t* expected_checksum);

    // ============================================
    // DATA ENCODING
    // ============================================
    
    /**
     * @brief Encode data to Base64
     * @param data Pointer to data buffer
     * @param length Data length in bytes
     * @param encoded Output string
     * @return SecurityResult
     */
    SecurityResult encodeBase64(const uint8_t* data, size_t length, String& encoded);

    /**
     * @brief Decode Base64 to data
     * @param encoded Base64 encoded string
     * @param data Output buffer
     * @param max_length Maximum bytes to write
     * @param decoded_length Output actual decoded length
     * @return SecurityResult
     */
    SecurityResult decodeBase64(const String& encoded, uint8_t* data, size_t max_length, size_t& decoded_length);

    /**
     * @brief Encode data to hex string
     * @param data Pointer to data buffer
     * @param length Data length in bytes
     * @param hex Output string
     * @return SecurityResult
     */
    SecurityResult encodeHex(const uint8_t* data, size_t length, String& hex);

    /**
     * @brief Decode hex string to data
     * @param hex Hex string
     * @param data Output buffer
     * @param max_length Maximum bytes to write
     * @param decoded_length Output actual decoded length
     * @return SecurityResult
     */
    SecurityResult decodeHex(const String& hex, uint8_t* data, size_t max_length, size_t& decoded_length);

    // ============================================
    // SECURE TRANSFER
    // ============================================
    
    /**
     * @brief Prepare secure JSON response
     * @param response JSON document to secure
     * @param data_size Size of data being transferred
     * @param encoding Encoding type for binary data
     * @return SecurityResult
     */
    SecurityResult prepareSecureResponse(JsonDocument& response, size_t data_size, EncodingType encoding = ENCODING_BASE64);

    /**
     * @brief Validate secure JSON response
     * @param response JSON document to validate
     * @return SecurityResult
     */
    SecurityResult validateSecureResponse(JsonDocument& response);

    /**
     * @brief Prepare secure file transfer
     * @param file_data Pointer to file data
     * @param file_size File size in bytes
     * @param chunk_size Preferred chunk size
     * @param response JSON document to populate
     * @return SecurityResult
     */
    SecurityResult prepareSecureTransfer(const uint8_t* file_data, size_t file_size, 
                                         size_t chunk_size, JsonDocument& response);

    /**
     * @brief Process secure file transfer chunk
     * @param chunk_data Chunk data
     * @param chunk_size Chunk size
     * @param chunk_index Chunk index (0-based)
     * @param total_chunks Total number of chunks
     * @param transfer_id Unique transfer ID
     * @param response JSON document to populate
     * @return SecurityResult
     */
    SecurityResult processTransferChunk(const uint8_t* chunk_data, size_t chunk_size,
                                        uint32_t chunk_index, uint32_t total_chunks,
                                        const String& transfer_id, JsonDocument& response);

    // ============================================
    // VALIDATION UTILITIES
    // ============================================
    
    /**
     * @brief Validate command format and parameters
     * @param command JSON document to validate
     * @param required_fields Array of required field names
     * @param field_count Number of required fields
     * @return SecurityResult
     */
    SecurityResult validateCommand(const JsonDocument& command, const char** required_fields, size_t field_count);

    /**
     * @brief Validate filename for security
     * @param filename Filename to validate
     * @return SecurityResult
     */
    SecurityResult validateFilename(const String& filename);

    /**
     * @brief Sanitize string for security
     * @param input String to sanitize
     * @param sanitized Output sanitized string
     * @return SecurityResult
     */
    SecurityResult sanitizeString(const String& input, String& sanitized);

    /**
     * @brief Generate secure random ID
     * @param id Output ID string
     * @param length ID length in characters
     * @return SecurityResult
     */
    SecurityResult generateSecureId(String& id, size_t length = 16);

    // ============================================
    // CONFIGURATION
    // ============================================
    
    /**
     * @brief Set security level
     * @param level New security level
     */
    void setSecurityLevel(SecurityLevel level) { _security_level = level; }

    /**
     * @brief Get current security level
     * @return Current security level
     */
    SecurityLevel getSecurityLevel() const { return _security_level; }

    /**
     * @brief Set checksum threshold
     * @param threshold Use checksum for transfers > this size
     */
    void setChecksumThreshold(size_t threshold) { _checksum_threshold = threshold; }

    /**
     * @brief Get checksum threshold
     * @return Current threshold
     */
    size_t getChecksumThreshold() const { return _checksum_threshold; }

    /**
     * @brief Enable/disable verbose logging
     * @param enabled true to enable verbose logging
     */
    void setVerboseLogging(bool enabled) { _verbose_logging = enabled; }

    /**
     * @brief Check if verbose logging is enabled
     * @return true if verbose logging enabled
     */
    bool isVerboseLogging() const { return _verbose_logging; }

    // ============================================
    // STATISTICS
    // ============================================
    
    /**
     * @brief Get security statistics
     * @return JSON document with statistics
     */
    JsonDocument getStatistics();

    /**
     * @brief Reset security statistics
     */
    void resetStatistics();

private:
    // ============================================
    // MEMBER VARIABLES
    // ============================================
    
    SecurityLevel _security_level;        ///< Current security level
    size_t _checksum_threshold;            ///< Checksum usage threshold
    bool _verbose_logging;                  ///< Verbose logging flag
    
    // Statistics
    struct {
        uint32_t checksums_calculated;     ///< Total checksums calculated
        uint32_t checksums_validated;      ///< Total checksums validated
        uint32_t checksum_failures;        ///< Total checksum validation failures
        uint32_t data_encoded;             ///< Total data encoding operations
        uint32_t data_decoded;             ///< Total data decoding operations
        uint32_t transfers_prepared;        ///< Total secure transfers prepared
        uint32_t transfers_validated;      ///< Total transfers validated
        uint32_t validation_failures;      ///< Total validation failures
        uint32_t bytes_processed;           ///< Total bytes processed
        uint32_t uptime_ms;                ///< Security module uptime
    } _stats;

    // Active transfer tracking
    struct TransferContext {
        String transfer_id;                 ///< Unique transfer ID
        size_t total_size;                  ///< Total transfer size
        size_t received_size;               ///< Size received so far
        uint32_t total_chunks;             ///< Total number of chunks
        uint32_t received_chunks;          ///< Chunks received so far
        uint8_t overall_checksum[32];       ///< Overall transfer checksum
        uint32_t start_time;                ///< Transfer start time
        bool active;                        ///< Transfer is active
    } _current_transfer;

    // ============================================
    // HELPER METHODS
    // ============================================
    
    /**
     * @brief Convert bytes to Base64
     * @param data Pointer to data
     * @param length Data length
     * @param base64 Output Base64 string
     * @return SecurityResult
     */
    SecurityResult bytesToBase64(const uint8_t* data, size_t length, String& base64);

    /**
     * @brief Convert Base64 to bytes
     * @param base64 Base64 string
     * @param data Output buffer
     * @param max_length Maximum bytes to write
     * @param decoded_length Output decoded length
     * @return SecurityResult
     */
    SecurityResult base64ToBytes(const String& base64, uint8_t* data, size_t max_length, size_t& decoded_length);

    /**
     * @brief Get Base64 character from 6-bit value
     * @param value 6-bit value (0-63)
     * @return Base64 character
     */
    char base64Char(uint8_t value);

    /**
     * @brief Get 6-bit value from Base64 character
     * @param c Base64 character
     * @return 6-bit value (-1 if invalid)
     */
    int8_t base64Value(char c);

    /**
     * @brief Initialize transfer context
     * @param transfer_id Unique transfer ID
     * @param total_size Total transfer size
     * @param total_chunks Total number of chunks
     * @return SecurityResult
     */
    SecurityResult initTransfer(const String& transfer_id, size_t total_size, uint32_t total_chunks);

    /**
     * @brief Update transfer progress
     * @param chunk_data Chunk data received
     * @param chunk_size Chunk size
     * @param chunk_index Chunk index
     * @return SecurityResult
     */
    SecurityResult updateTransfer(const uint8_t* chunk_data, size_t chunk_size, uint32_t chunk_index);

    /**
     * @brief Finalize transfer and validate
     * @return SecurityResult
     */
    SecurityResult finalizeTransfer();

    /**
     * @brief Cancel active transfer
     */
    void cancelTransfer();

    /**
     * @brief Get security result as string
     * @param result SecurityResult enum
     * @return String representation
     */
    String resultToString(SecurityResult result);

    /**
     * @brief Log security operation
     * @param operation Operation description
     * @param result Operation result
     * @param details Additional details
     */
    void logSecurity(const String& operation, SecurityResult result, const String& details = "");

    /**
     * @brief Check if string contains dangerous characters
     * @param str String to check
     * @return true if dangerous
     */
    bool containsDangerousChars(const String& str);

    /**
     * @brief Remove dangerous characters from string
     * @param str String to clean (modified in place)
     */
    void removeDangerousChars(String& str);

    /**
     * @brief Generate random bytes
     * @param buffer Output buffer
     * @param length Number of bytes to generate
     * @return SecurityResult
     */
    SecurityResult generateRandomBytes(uint8_t* buffer, size_t length);
};