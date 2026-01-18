/**
 * @file srix_tool.h
 * @brief SRIX4K/SRIX512 Reader/Writer Tool v1.3
 * @author Senape3000
 * @info https://github.com/Senape3000/firmware/blob/main/docs_custom/SRIX/SRIX_Tool_README.md
 * @date 2026-01-01
 */

#ifndef __SRIX_TOOL_H__
#define __SRIX_TOOL_H__

#include "pn532_srix.h"
#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "logger.h"

/**
 * @brief SRIX (ISO 15693) NFC Tag Reader/Writer Tool
 * 
 * Features:
 * - Read/Write SRIX4K and SRIX512 tags via PN532
 * - Headless mode for integration with web/serial interfaces
 * - Dump file management (save/load .srix format)
 * - Single block write support for selective updates
 * - Hardware IRQ/RST support for embedded PN532
 * 
 * Architecture:
 * - Uses Arduino_PN532_SRIX library for low-level operations
 * - RAM-based dump buffer (512 bytes)
 * - Validates dump origin (read vs. loaded)
 * - I2C communication at 100kHz
 * 
 * Supported Tags:
 * - SRIX4K (512 bytes, 128 blocks)
 * - SRIX512 (64 bytes, 16 blocks) - partial support
 * 
 * Hardware Support:
 * - IRQ mode (if PN532_IRQ and PN532_RF_REST defined)
 * - Polling mode (fallback if no hardware pins)
 * 
 * Usage:
 * @code
 * SRIXTool srix(true);  // Headless mode
 * String json = srix.read_tag_headless(10);  // Read with 10s timeout
 * srix.save_file_headless("dump.srix");
 * @endcode
 */
class SRIXTool {
public:
    // ============================================
    // CONSTANTS
    // ============================================
    
    // Tag specifications
    static constexpr uint8_t SRIX_BLOCK_COUNT = 128;       // Total blocks (SRIX4K)
    static constexpr uint8_t SRIX_BLOCK_SIZE = 4;          // Bytes per block
    static constexpr uint16_t SRIX_TOTAL_SIZE = 512;       // Total memory (bytes)
    static constexpr uint8_t SRIX_UID_SIZE = 8;            // UID length (bytes)
    static constexpr uint8_t SRIX_MAX_BLOCK_NUM = 127;     // Maximum block number (0-127)
    
    // I2C configuration
    static constexpr uint32_t I2C_CLOCK_SPEED = 100000;    // 100kHz
    
    // PN532 configuration
    static constexpr uint8_t PN532_INVALID_PIN = 255;      // Invalid pin marker
    static constexpr uint8_t PN532_MAX_RETRIES = 0xFF;     // Passive activation retries
    
    // Timing constants
    static constexpr uint32_t TAG_DETECT_DELAY_MS = 50;         // Delay between detection attempts
    static constexpr uint32_t TAG_DETECT_RETRY_DELAY_MS = 100;  // Delay after failed UID read
    static constexpr uint32_t TAG_REDETECT_TIMEOUT_MS = 600;    // Tag re-detection timeout
    static constexpr uint32_t SINGLE_BLOCK_TIMEOUT_MS = 2500;   // Single block write timeout
    static constexpr uint32_t VERIFY_DELAY_MS = 10;             // Delay before verify
    static constexpr uint32_t VERIFY_READ_DELAY_MS = 5;         // Delay after verify read
    
    // File format constants
    static constexpr size_t SRIX_FILE_EXTENSION_LEN = 5;   // Length of ".srix"
    static constexpr uint8_t HEX_PADDING_THRESHOLD = 0x10; // Values < 0x10 need leading zero
    static constexpr size_t HEX_CHARS_PER_BYTE = 2;        // 2 hex chars per byte
    
    // Error codes
    static constexpr int ERROR_TIMEOUT = -1;               // Tag detection timeout
    static constexpr int ERROR_NO_DUMP = -2;               // No dump loaded in memory
    static constexpr int ERROR_INVALID_PTR = -3;           // Invalid pointer
    static constexpr int ERROR_INVALID_BLOCK = -4;         // Block number out of range
    static constexpr int ERROR_WRITE_FAILED = -5;          // Write operation failed
    static constexpr int ERROR_NULL_NFC = -6;              // NFC object is NULL
    static constexpr int ERROR_FILE_NOT_FOUND = -2;        // File not found (reuses NO_DUMP)
    static constexpr int ERROR_INCOMPLETE_DUMP = -3;       // Incomplete dump file
    
    // Success codes
    static constexpr int SUCCESS = 0;                       // Operation successful
    static constexpr int SUCCESS_VERIFY_FAIL = 1;          // Write OK, verify failed
    static constexpr int SUCCESS_VERIFY_SKIP = 2;          // Write OK, verify skipped

    // ============================================
    // PUBLIC METHODS
    // ============================================
    
    /**
     * @brief Construct SRIX tool in headless mode
     * @param headless_mode If true, no UI/loop (for server integration)
     * 
     * Initializes I2C, creates PN532 object, configures passive activation.
     * Uses hardware IRQ/RST pins if defined, otherwise polling mode.
     */
    SRIXTool(bool headless_mode);
    
    /**
     * @brief Get pointer to PN532 NFC object
     * @return Pointer to Arduino_PN532_SRIX instance
     */
    Arduino_PN532_SRIX* getNFC() { return nfc; }
    
    /**
     * @brief Mark dump as valid from loaded file
     * 
     * Used after loading dump from file to indicate data origin.
     */
    void setDumpValidFromLoad() {
        _dump_valid_from_load = true;
        _dump_valid_from_read = false;
    }
    
    /**
     * @brief Read tag and return JSON with UID and data
     * @param timeout_seconds Maximum time to wait for tag (seconds)
     * @return JSON string with {uid, blocks, size, data} or empty on timeout
     * 
     * Blocks until tag detected or timeout.
     * Reads all 128 blocks and returns as hex string.
     */
    String read_tag_headless(int timeout_seconds);
    
    /**
     * @brief Write loaded dump to tag
     * @param timeout_seconds Maximum time to wait for tag (seconds)
     * @return 0 on success, negative error code on failure
     * 
     * Error codes:
     * - ERROR_NULL_NFC (-6): NFC object not initialized
     * - ERROR_NO_DUMP (-2): No dump loaded in memory
     * - ERROR_TIMEOUT (-1): Tag detection timeout
     * - ERROR_WRITE_FAILED (-5): Write operation failed
     * - Positive: Number of blocks written if tag lost mid-write
     * 
     * NOTE: Verify is removed as it's unreliable on SRIX protocol.
     */
    int write_tag_headless(int timeout_seconds);
    
    /**
     * @brief Save current dump to file
     * @param filename Output filename (without .srix extension)
     * @return Full filepath if successful, empty string on error
     * 
     * Creates directories if needed.
     * Appends "_N" suffix if file exists.
     * Format: Flipper-compatible .srix dump format.
     */
    String save_file_headless(String filename);
    
    /**
     * @brief Load dump from file
     * @param filename Input filename (with or without .srix extension)
     * @return 0 on success, negative error code on failure
     * 
     * Error codes:
     * - ERROR_TIMEOUT (-1): NFC object not initialized
     * - ERROR_FILE_NOT_FOUND (-2): File not found
     * - ERROR_INCOMPLETE_DUMP (-3): File has less than 128 blocks
     * 
     * Parses Flipper-compatible .srix format.
     */
    int load_file_headless(String filename);
    
    /**
     * @brief Write single block to tag
     * @param block_num Block number (0-127)
     * @param block_data Pointer to 4-byte block data
     * @return Result code (see constants)
     * 
     * Return codes:
     * - SUCCESS (0): Write and verify successful
     * - SUCCESS_VERIFY_FAIL (1): Write OK, verify failed
     * - SUCCESS_VERIFY_SKIP (2): Write OK, verify not possible
     * - ERROR_NULL_NFC (-6): NFC not initialized
     * - ERROR_INVALID_BLOCK (-4): Invalid block number
     * - ERROR_INVALID_PTR (-3): Invalid data pointer
     * - ERROR_TIMEOUT (-1): Tag not found
     * - ERROR_WRITE_FAILED (-5): Write command failed
     */
    int write_single_block_headless(uint8_t block_num, const uint8_t *block_data);
    
    /**
     * @brief Wait for tag presence
     * @param timeout_ms Timeout in milliseconds
     * @return true if tag detected, false on timeout
     * 
     * Polls tag at TAG_DETECT_DELAY_MS intervals.
     */
    bool waitForTagHeadless(uint32_t timeout_ms);
    
    /**
     * @brief Get pointer to dump buffer
     * @return Pointer to 512-byte dump array
     */
    uint8_t *getDump() { return _dump; }
    
    /**
     * @brief Get pointer to UID buffer
     * @return Pointer to 8-byte UID array
     */
    uint8_t *getUID() { return _uid; }
    
    /**
     * @brief Check if dump is valid
     * @return true if dump was read or loaded successfully
     */
    bool isDumpValid() { return _dump_valid_from_read || _dump_valid_from_load; }

private:
    // ============================================
    // PRIVATE MEMBERS
    // ============================================
    
    // Hardware configuration
    #if defined(PN532_IRQ) && defined(PN532_RF_REST)
        Arduino_PN532_SRIX *nfc;      // PN532 instance (IRQ mode)
        bool _has_hardware_pins = true;
    #else
        Arduino_PN532_SRIX *nfc;      // PN532 instance (polling mode)
        bool _has_hardware_pins = false;
    #endif
    
    // State flags
    bool _tag_read = false;                 // Tag has been read successfully
    bool _dump_valid_from_read = false;     // Dump obtained from physical tag
    bool _dump_valid_from_load = false;     // Dump loaded from file
    
    // Data buffers
    uint8_t _dump[SRIX_TOTAL_SIZE];         // RAM storage for 128 blocks (512 bytes)
    uint8_t _uid[SRIX_UID_SIZE];            // Tag UID (8 bytes)
};

/**
 * @brief External function for PN532 SRIX operations
 * 
 * Legacy function declaration (may be unused).
 */
void PN532_SRIX();

#endif // __SRIX_TOOL_H__
