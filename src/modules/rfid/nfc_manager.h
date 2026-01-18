#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "logger.h"
#include "srix_tool.h"
#include "mifare_tool.h"

/**
 * @file nfc_manager.h
 * @brief Multi-Protocol NFC Handler
 * 
 * NFCManager provides a unified interface for multiple NFC protocols:
 * - SRIX4K (ISO 14443B)
 * - Mifare Classic 1K/4K (ISO 14443A)
 * - Future: NTAG, DESFire
 * 
 * Features:
 * - Protocol abstraction with unified TagInfo structure
 * - Lazy initialization (handlers created on demand)
 * - File management (save/load/list/delete)
 * - Memory management (current tag state)
 * - Selective block write operations
 * 
 * Architecture:
 * - NFCManager acts as coordinator
 * - Protocol-specific handlers (SRIXTool, MifareTool) handle hardware
 * - TagInfo struct provides protocol-agnostic data storage
 * - File operations use protocol-specific folders and extensions
 * 
 * Usage:
 * @code
 * NFCManager nfc;
 * nfc.begin();
 * NFCManager::TagInfo info;
 * NFCManager::Result result = nfc.readSRIX(info, 10);
 * if (result.success) {
 *     nfc.save("dump.srix");
 * }
 * @endcode
 */
class NFCManager {
public:
    // ============================================
    // PROTOCOL ENUM
    // ============================================
    
    /**
     * @brief NFC protocol types
     */
    enum Protocol {
        PROTOCOL_UNKNOWN = 0,        ///< Unknown or undetected protocol
        PROTOCOL_SRIX,               ///< SRIX4K (ISO 14443B)
        PROTOCOL_MIFARE_CLASSIC,     ///< Mifare Classic 1K/4K (ISO 14443A)
        PROTOCOL_NTAG,               ///< NTAG (ISO 14443A) - Future
        PROTOCOL_DESFIRE             ///< DESFire (ISO 14443A) - Future
    };
    
    // ============================================
    // TAG INFO STRUCTURE
    // ============================================
    
    /**
     * @brief Universal tag information container
     * 
     * Protocol-agnostic structure that holds tag data.
     * Uses union to optimize memory for protocol-specific data.
     */
    struct TagInfo {
        Protocol protocol;           ///< Detected protocol
        String protocol_name;        ///< Human-readable protocol name
        uint8_t uid[10];            ///< Universal UID buffer (max 10 bytes)
        uint8_t uid_length;         ///< Actual UID length
        bool valid;                 ///< Data validity flag
        uint32_t timestamp;         ///< Timestamp of read/load (millis())
        
        /**
         * @brief Protocol-specific data storage (union saves RAM)
         */
        union {
            struct {
                uint8_t dump[512];   ///< SRIX: 128 blocks × 4 bytes
            } srix;
            
            struct {
                uint8_t dump[1024];  ///< Mifare Classic 1K: 64 blocks × 16 bytes
                uint8_t sectors;     ///< Number of sectors (16 for 1K, 40 for 4K)
            } mifare_classic;
            
            // Future: NTAG, DESFire, etc.
        } data;
    };
    
    // ============================================
    // OPERATION RESULT
    // ============================================
    
    /**
     * @brief Operation result structure
     */
    struct Result {
        bool success;      ///< Operation success flag
        String message;    ///< Human-readable result message
        int code;          ///< Result code: 0=OK, <0=error, >0=warning
    };
    
    // Constants
    static constexpr uint8_t MAX_UID_LENGTH = 10;           ///< Maximum UID size
    static constexpr size_t SRIX_DUMP_SIZE = 512;           ///< SRIX dump size in bytes
    static constexpr size_t MIFARE_1K_DUMP_SIZE = 1024;     ///< Mifare 1K dump size in bytes
    static constexpr size_t MIFARE_4K_DUMP_SIZE = 4096;     ///< Mifare 4K dump size in bytes
    static constexpr int DEFAULT_READ_TIMEOUT_SEC = 10;     ///< Default read timeout
    static constexpr int DEFAULT_WRITE_TIMEOUT_SEC = 20;    ///< Default write timeout
    static constexpr int DEFAULT_UID_READ_TIMEOUT_SEC = 5;  ///< Default UID-only read timeout
    
    // ============================================
    // CONSTRUCTOR & INITIALIZATION
    // ============================================
    
    /**
     * @brief Constructor
     */
    NFCManager();
    
    /**
     * @brief Destructor
     * 
     * Cleans up protocol handlers.
     */
    ~NFCManager();
    
    /**
     * @brief Initialize NFC Manager
     * @return true if initialization successful
     * 
     * Creates dump directories on LittleFS.
     * Does not initialize protocol handlers (lazy init).
     */
    bool begin();
    
    /**
     * @brief Initialize SRIX handler (lazy init)
     * @return true if handler ready
     * 
     * Called automatically by SRIX operations.
     */
    bool beginSRIX();
    
    /**
     * @brief Initialize Mifare handler (lazy init)
     * @return true if handler ready
     * 
     * Called automatically by Mifare operations.
     */
    bool beginMifare();
    
    /**
     * @brief Check if NFC Manager is initialized
     * @return true if begin() was called successfully
     */
    bool isReady() const { return _initialized; }
    
    /**
     * @brief Check if SRIX handler is ready
     * @return true if SRIX handler initialized
     */
    bool isSRIXReady() const { return _srix_handler != nullptr; }
    
    /**
     * @brief Check if Mifare handler is ready
     * @return true if Mifare handler initialized
     */
    bool isMifareReady() const { return _mifare_handler != nullptr; }
    
    /**
     * @brief Get current active protocol
     * @return Protocol enum
     */
    Protocol getCurrentProtocol() const { return _current_protocol; }
    
    // ============================================
    // SRIX OPERATIONS
    // ============================================
    
    /**
     * @brief Read SRIX tag (UID + full dump)
     * @param info TagInfo structure to fill
     * @param timeout_sec Timeout in seconds
     * @return Result with success/message/code
     */
    Result readSRIX(TagInfo& info, int timeout_sec = DEFAULT_READ_TIMEOUT_SEC);
    
    /**
     * @brief Write SRIX tag from loaded dump
     * @param info TagInfo with data to write
     * @param timeout_sec Timeout in seconds
     * @return Result with success/message/code
     */
    Result writeSRIX(const TagInfo& info, int timeout_sec = DEFAULT_WRITE_TIMEOUT_SEC);
    
    /**
     * @brief Save SRIX dump to file
     * @param info TagInfo to save
     * @param filename Filename (without path, saved to /DUMPS/SRIX/)
     * @return Result with success/message/code
     */
    Result saveSRIX(const TagInfo& info, String filename);
    
    /**
     * @brief Load SRIX dump from file
     * @param info TagInfo to fill
     * @param filename Filename or full path
     * @return Result with success/message/code
     */
    Result loadSRIX(TagInfo& info, String filename);
    
    /**
     * @brief Write single SRIX block
     * @param block_num Block number (0-127)
     * @param data Pointer to 4-byte block data
     * @return Result with success/message/code
     */
    Result writeSRIXBlock(uint8_t block_num, const uint8_t* data);
    
    /**
     * @brief Write selective SRIX blocks
     * @param block_numbers Vector of block numbers to write
     * @return Result with success/message/code
     */
    Result writeSRIXBlocksSelective(const std::vector<uint8_t>& block_numbers);
    
    /**
     * @brief Wait for SRIX tag presence
     * @param timeout_ms Timeout in milliseconds
     * @return true if tag detected
     */
    bool waitForSRIXTag(uint32_t timeout_ms);
    
    // ============================================
    // MIFARE OPERATIONS
    // ============================================
    
    /**
     * @brief Read Mifare tag (UID + full dump)
     * @param info TagInfo structure to fill
     * @param timeout_sec Timeout in seconds
     * @return Result with success/message/code
     */
    Result readMifare(TagInfo& info, int timeout_sec = DEFAULT_READ_TIMEOUT_SEC);
    
    /**
     * @brief Read Mifare UID only (fast, no authentication)
     * @param info TagInfo structure to fill (UID only)
     * @param timeout_sec Timeout in seconds
     * @return Result with success/message/code
     */
    Result readMifareUID(TagInfo& info, int timeout_sec = DEFAULT_UID_READ_TIMEOUT_SEC);
    
    /**
     * @brief Write Mifare tag from loaded dump
     * @param info TagInfo with data to write
     * @param timeout_sec Timeout in seconds
     * @return Result with success/message/code
     */
    Result writeMifare(const TagInfo& info, int timeout_sec = DEFAULT_WRITE_TIMEOUT_SEC);
    
    /**
     * @brief Clone Mifare UID (magic card required)
     * @param info TagInfo with UID to clone
     * @param timeout_sec Timeout in seconds
     * @return Result with success/message/code
     */
    Result cloneMifareUID(const TagInfo& info, int timeout_sec = DEFAULT_READ_TIMEOUT_SEC);
    
    /**
     * @brief Save Mifare dump to file
     * @param info TagInfo to save
     * @param filename Filename (without path, saved to /DUMPS/MIFARE/)
     * @return Result with success/message/code
     */
    Result saveMifare(const TagInfo& info, String filename);
    
    /**
     * @brief Load Mifare dump from file
     * @param info TagInfo to fill
     * @param filename Filename or full path
     * @return Result with success/message/code
     */
    Result loadMifare(TagInfo& info, String filename);
    
    /**
     * @brief Write selective Mifare blocks
     * @param block_numbers Vector of block numbers to write
     * @return Result with success/message/code
     */
    Result writeMifareBlocksSelective(const std::vector<uint8_t>& block_numbers);
    
    // ============================================
    // MEMORY MANAGEMENT
    // ============================================
    
    /**
     * @brief Get current tag data
     * @return TagInfo structure (copy)
     */
    TagInfo getCurrentTag() const { return _current_tag; }
    
    /**
     * @brief Check if valid data is loaded
     * @return true if current tag is valid
     */
    bool hasValidData() const { return _current_tag.valid; }
    
    /**
     * @brief Clear current tag data
     */
    void clearCurrentTag();
    
    /**
     * @brief Restore current tag from backup
     * @param info TagInfo to restore
     * 
     * Useful after selective block writes to maintain state.
     */
    void restoreCurrentTag(const TagInfo& info);
    
    // ============================================
    // FILE OPERATIONS
    // ============================================
    
    /**
     * @brief Auto-save current tag (protocol detected)
     * @param filename Filename (without path or extension)
     * @return Result with success/message/code
     */
    Result save(String filename);
    
    /**
     * @brief Load dump with protocol specification
     * @param filename Filename or full path
     * @param protocol Protocol type (auto-detect if PROTOCOL_UNKNOWN)
     * @return Result with success/message/code
     */
    Result load(String filename, Protocol protocol);
    
    /**
     * @brief List files for protocol
     * @param protocol Protocol type
     * @return Result with success/message/code (code = file count)
     */
    Result listFiles(Protocol protocol = PROTOCOL_SRIX);
    
    /**
     * @brief Delete file
     * @param filename Filename (without path)
     * @param protocol Protocol type
     * @return Result with success/message/code
     */
    Result deleteFile(String filename, Protocol protocol);
    
    // ============================================
    // UTILITY
    // ============================================
    
    /**
     * @brief Convert protocol enum to string
     * @param proto Protocol enum
     * @return Human-readable protocol name
     */
    String protocolToString(Protocol proto);
    
    /**
     * @brief Convert UID to colon-separated hex string
     * @param uid Pointer to UID buffer
     * @param length UID length
     * @return UID string (e.g., "04:A1:B2:C3")
     */
    String uidToString(const uint8_t* uid, uint8_t length);
    
    /**
     * @brief Convert dump to formatted hex string
     * @param data Pointer to data buffer
     * @param length Data length
     * @return Hex string with spacing
     */
    String dumpToHex(const uint8_t* data, size_t length);
    
    /**
     * @brief Get data size for tag protocol
     * @param info TagInfo structure
     * @return Data size in bytes
     */
    size_t getTagDataSize(const TagInfo& info) const;
    
    /**
     * @brief Get pointer to tag data
     * @param info TagInfo structure
     * @return Pointer to data buffer
     */
    const uint8_t* getTagDataPointer(const TagInfo& info) const;

private:
    // ============================================
    // PROTOCOL HANDLERS
    // ============================================
    
    SRIXTool* _srix_handler;        ///< SRIX protocol handler (lazy init)
    MifareTool* _mifare_handler;    ///< Mifare protocol handler (lazy init)
    // Future: NTAGTool* _ntag_handler;
    
    // ============================================
    // STATE
    // ============================================
    
    bool _initialized;              ///< Manager initialization flag
    Protocol _current_protocol;     ///< Currently active protocol
    TagInfo _current_tag;           ///< Current loaded tag data
    
    // ============================================
    // HELPER FUNCTIONS
    // ============================================
    
    /**
     * @brief Convert hex string to UID bytes
     * @param str Input hex string (with or without separators)
     * @param uid Output UID buffer
     * @param length Output UID length
     */
    void stringToUid(const String& str, uint8_t* uid, uint8_t& length);
    
    /**
     * @brief Convert hex string to dump bytes
     * @param hex Input hex string
     * @param data Output data buffer
     * @param length Maximum bytes to write
     */
    void hexToDump(const String& hex, uint8_t* data, size_t length);
    
    /**
     * @brief Get protocol dump folder path
     * @param proto Protocol type
     * @return Folder path string
     */
    String getProtocolFolder(Protocol proto);
    
    /**
     * @brief Get protocol file extension
     * @param proto Protocol type
     * @return Extension string (e.g., ".srix", ".mfc")
     */
    String getFileExtension(Protocol proto);
};
