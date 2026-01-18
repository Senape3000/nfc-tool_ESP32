#ifndef __MIFARE_TOOL_H__
#define __MIFARE_TOOL_H__

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <LittleFS.h>
#include "config.h"
#include "mifare_keys_manager.h"
#include "logger.h"

/**
 * @brief Mifare Classic 1K/4K Reader/Writer Tool
 * 
 * Features:
 * - Full dump read/write with automatic key detection
 * - UID-only fast read (no authentication)
 * - UID cloning support (magic cards)
 * - Single block write with selective updates
 * - Key caching for optimized multi-sector operations
 * - Flipper-compatible file format (.mfc)
 * 
 * Architecture:
 * - Uses Adafruit_PN532 library (I2C mode)
 * - Integrates with MifareKeysManager for key database
 * - Headless mode for server integration
 * - Automatic sector trailer reconstruction with extracted keys
 * 
 * Supported Cards:
 * - Mifare Classic 1K (16 sectors, 64 blocks, 1024 bytes)
 * - Mifare Classic 4K (40 sectors, 256 blocks, 4096 bytes)
 * 
 * Authentication Strategy:
 * 1. Try saved/extracted keys from previous reads
 * 2. Try cached working keys
 * 3. Brute-force key database (MifareKeysManager)
 * 4. Card re-activation between failed attempts
 * 
 * Usage:
 * @code
 * MifareTool mifare(true);  // Headless mode
 * mifare.begin();
 * String json = mifare.read_tag_headless(5000);  // 5s timeout
 * mifare.save_file_headless("dump.mfc");
 * @endcode
 */
class MifareTool {
public:
    // ============================================
    // ENUMS & CONSTANTS
    // ============================================
    
    /**
     * @brief Mifare Classic card type identification
     */
    enum CardType {
        CARD_UNKNOWN = 0,
        CARD_MIFARE_1K = 1,  ///< 16 sectors × 4 blocks = 64 blocks (1024 bytes)
        CARD_MIFARE_4K = 2   ///< 40 sectors (32×4 + 8×16) = 256 blocks (4096 bytes)
    };
    
    /**
     * @brief Operation return codes
     */
    enum ReturnCode {
        SUCCESS = 0,              ///< Operation successful
        ERROR_NO_TAG = -1,        ///< No tag detected within timeout
        ERROR_AUTH_FAILED = -2,   ///< Authentication failed
        ERROR_READ_FAILED = -3,   ///< Block read operation failed
        ERROR_WRITE_FAILED = -4,  ///< Block write operation failed
        ERROR_FILE_ERROR = -5,    ///< File I/O error
        ERROR_INVALID_DATA = -6   ///< Invalid data or dump not loaded
    };
    
    // Card specifications
    static constexpr int MIFARE_1K_SECTORS = 16;        // Sectors in 1K card
    static constexpr int MIFARE_4K_SECTORS = 40;        // Sectors in 4K card
    static constexpr int MIFARE_1K_BLOCKS = 64;         // Total blocks in 1K card
    static constexpr int MIFARE_4K_BLOCKS = 256;        // Total blocks in 4K card
    static constexpr int MIFARE_1K_SIZE = 1024;         // 1K card size in bytes
    static constexpr int MIFARE_4K_SIZE = 4096;         // 4K card size in bytes
    static constexpr int MIFARE_BLOCK_SIZE = 16;        // Bytes per block
    static constexpr int MIFARE_KEY_SIZE = 6;           // Bytes per key
    static constexpr int MIFARE_UID_MAX_SIZE = 10;      // Max UID size
    static constexpr int MIFARE_ATQA_SIZE = 2;          // ATQA size
    
    // Sector layout
    static constexpr int BLOCKS_PER_SMALL_SECTOR = 4;   // Blocks in sectors 0-31
    static constexpr int BLOCKS_PER_LARGE_SECTOR = 16;  // Blocks in sectors 32-39 (4K only)
    static constexpr int SMALL_SECTOR_COUNT = 32;       // Number of 4-block sectors
    static constexpr int LARGE_SECTOR_BOUNDARY = 128;   // First block of large sectors
    
    // Timing constants
    static constexpr uint32_t CARD_DETECT_INTERVAL_MS = 50;     // Polling interval for card detection
    static constexpr uint32_t CARD_DETECT_SHORT_TIMEOUT_MS = 100;  // Short detection timeout
    static constexpr uint32_t CARD_RESELECT_TIMEOUT_MS = 500;   // Timeout for card re-selection
    static constexpr uint32_t CARD_REACTIVATE_DELAY_MS = 50;    // Delay for card re-activation
    static constexpr uint32_t BLOCK_WRITE_DELAY_MS = 10;        // Delay after block write
    static constexpr uint32_t SINGLE_BLOCK_TIMEOUT_MS = 5000;   // Timeout for single block operation
    
    // File format constants
    static constexpr size_t FILE_EXTENSION_LEN = 4;     // Length of ".mfc"
    static constexpr uint8_t HEX_PADDING_THRESHOLD = 0x10; // Values < 0x10 need leading zero
    static constexpr size_t HEX_CHARS_PER_BYTE = 2;     // 2 hex chars per byte
    static constexpr int DUMP_PREVIEW_BLOCKS = 16;      // Number of blocks in JSON preview
    
    // Block protection
    static constexpr int UID_BLOCK = 0;                 // Block 0 contains UID
    
    // Key storage indices
    static constexpr int KEY_A_OFFSET = 0;              // Key A position in sector trailer
    static constexpr int ACCESS_BITS_OFFSET = 6;        // Access bits position
    static constexpr int KEY_B_OFFSET = 10;             // Key B position in sector trailer

    // ============================================
    // CONSTRUCTOR & INIT
    // ============================================
    
    /**
     * @brief Construct Mifare tool
     * @param headless If true, suppresses serial output (for server mode)
     */
    explicit MifareTool(bool headless = false);
    
    /**
     * @brief Destructor
     */
    ~MifareTool();
    
    /**
     * @brief Initialize PN532 hardware
     * @return true if initialization successful, false otherwise
     * 
     * Checks firmware version and configures SAM.
     */
    bool begin();
    
    /**
     * @brief Get pointer to PN532 object
     * @return Pointer to Adafruit_PN532 instance
     */
    Adafruit_PN532* getNFC() { return &_nfc; }

    // ============================================
    // HEADLESS OPERATIONS (for NFCManager)
    // ============================================
    
    /**
     * @brief Read UID and full dump from Mifare Classic tag
     * @param timeout_ms Timeout in milliseconds
     * @return JSON string with {uid, sak, atqa, card_type, dump_hex, sectors_read, auth_success}
     * 
     * Attempts to read all sectors with automatic key detection.
     * Returns JSON even if some sectors fail authentication.
     */
    String read_tag_headless(uint32_t timeout_ms);
    
    /**
     * @brief Read only UID (fast, no authentication)
     * @param timeout_ms Timeout in milliseconds
     * @return JSON string with {uid, sak, atqa, card_type}
     * 
     * Quick UID-only read without full dump.
     */
    String read_uid_headless(uint32_t timeout_ms);
    
    /**
     * @brief Write loaded dump to physical tag
     * @param timeout_sec Timeout in seconds
     * @return SUCCESS (0) on success, error code on failure
     * 
     * Writes all non-protected blocks.
     * Skips UID block (0) and sector trailers.
     */
    int write_tag_headless(int timeout_sec);
    
    /**
     * @brief Clone UID to writable tag (magic card)
     * @param timeout_sec Timeout in seconds
     * @return ReturnCode
     * 
     * Writes UID, SAK, and ATQA to block 0.
     * Requires magic card with backdoor command support.
     */
    int clone_uid_headless(int timeout_sec);
    
    /**
     * @brief Save dump to LittleFS file
     * @param filename Filename (without path, saved to /DUMPS/MIFARE/)
     * @return Full filepath on success, empty string on error
     * 
     * Appends "_N" suffix if file exists.
     * Format: Flipper-compatible .mfc format.
     */
    String save_file_headless(const String& filename);
    
    /**
     * @brief Load dump from LittleFS file
     * @param filename Filename (with or without .mfc extension)
     * @return ReturnCode
     * 
     * Parses Flipper-compatible .mfc format.
     * Extracts keys from sector trailers for future writes.
     */
    int load_file_headless(const String& filename);
    
    /**
     * @brief Write single block from loaded dump
     * @param block Block number (0-63 for 1K, 0-255 for 4K)
     * @return ReturnCode
     * 
     * Protections:
     * - Block 0 (UID): use clone_uid_headless() instead
     * - Sector trailers: cannot be written
     */
    int write_single_block_headless(int block);

    // ============================================
    // DATA ACCESSORS (for NFCManager)
    // ============================================
    
    /**
     * @brief Get pointer to dump buffer
     * @return Pointer to 4096-byte dump array
     */
    uint8_t* getDump() { return _dump; }
    
    /**
     * @brief Get pointer to UID buffer
     * @return Pointer to UID array
     */
    uint8_t* getUID() { return _uid; }
    
    /**
     * @brief Get UID length
     * @return UID length in bytes (4 or 7)
     */
    uint8_t getUIDLength() const { return _uid_length; }
    
    /**
     * @brief Get detected card type
     * @return CardType enum
     */
    CardType getCardType() const { return _card_type; }
    
    /**
     * @brief Get total blocks for detected card type
     * @return Block count (64 for 1K, 256 for 4K)
     */
    int getTotalBlocks() const { return _total_blocks; }
    
    /**
     * @brief Get number of successfully read blocks
     * @return Blocks read count
     */
    int getBlocksRead() const { return _blocks_read; }
    
    /**
     * @brief Check if dump is valid
     * @return true if dump was read or loaded successfully
     */
    bool isDumpValid() const { return _dump_valid; }
    
    /**
     * @brief Mark dump as valid from loaded file
     * 
     * Used after manual dump loading.
     */
    void setDumpValidFromLoad() { _dump_valid = true; }

    // ============================================
    // UTILITY
    // ============================================
    
    /**
     * @brief Convert CardType enum to string
     * @param type CardType enum
     * @return Human-readable card type string
     */
    String cardTypeToString(CardType type);
    
    /**
     * @brief Get sector count for card type
     * @param type CardType enum
     * @return Number of sectors (16 for 1K, 40 for 4K)
     */
    static int getSectorCount(CardType type);
    
    /**
     * @brief Get first block number of sector
     * @param sector Sector number (0-15 for 1K, 0-39 for 4K)
     * @return First block number
     */
    static int getFirstBlockOfSector(int sector);
    
    /**
     * @brief Get number of blocks in sector
     * @param sector Sector number
     * @return Block count (4 for sectors 0-31, 16 for sectors 32-39)
     */
    static int getBlockCountInSector(int sector);

private:
    // ============================================
    // HARDWARE
    // ============================================
    
    Adafruit_PN532 _nfc;          ///< PN532 instance (I2C mode)
    bool _headless;               ///< Suppress serial output if true
    
    // ============================================
    // CARD DATA
    // ============================================
    
    uint8_t _uid[MIFARE_UID_MAX_SIZE];   ///< Card UID (4 or 7 bytes)
    uint8_t _uid_length;                  ///< UID length in bytes
    uint8_t _sak;                         ///< Select Acknowledge (card type indicator)
    uint8_t _atqa[MIFARE_ATQA_SIZE];      ///< Answer To Request Type A
    CardType _card_type;                  ///< Detected card type
    
    // ============================================
    // DUMP STORAGE
    // ============================================
    
    uint8_t _dump[MIFARE_4K_SIZE];        ///< RAM storage (max 4K card size)
    int _total_blocks;                    ///< Total blocks for detected card
    int _blocks_read;                     ///< Successfully read blocks
    bool _dump_valid;                     ///< Dump validity flag
    
    // ============================================
    // AUTHENTICATION STATE
    // ============================================
    
    bool _sector_auth_success[MIFARE_4K_SECTORS];  ///< Per-sector auth status
    
    /**
     * @brief Sector key storage structure
     */
    struct SectorKeys {
        uint8_t key_a[MIFARE_KEY_SIZE];   ///< Key A (6 bytes)
        uint8_t key_b[MIFARE_KEY_SIZE];   ///< Key B (6 bytes)
        bool key_a_valid;                  ///< Key A is valid/extracted
        bool key_b_valid;                  ///< Key B is valid/extracted
    };
    
    SectorKeys _sector_keys[MIFARE_4K_SECTORS];  ///< Extracted keys per sector
    
    // ============================================
    // INTERNAL OPERATIONS
    // ============================================
    
    /**
     * @brief Detect and identify card
     * @param timeout_ms Detection timeout
     * @return true if card detected and identified
     */
    bool detectCard(uint32_t timeout_ms);
    
    /**
     * @brief Identify card type (1K vs 4K)
     * @return CardType enum
     * 
     * Attempts authentication on block 64 to distinguish 1K from 4K.
     */
    CardType identifyCardType();
    
    /**
     * @brief Read all blocks from card
     * @return ReturnCode
     * 
     * Uses optimized key caching for multi-sector reads.
     */
    int readAllBlocks();
    
    /**
     * @brief Read single sector
     * @param sector Sector number
     * @return ReturnCode
     */
    int readSector(int sector);
    
    /**
     * @brief Read sector with key caching
     * @param sector Sector number
     * @param cached_key_a Reference to cached Key A (updated on success)
     * @param cached_key_b Reference to cached Key B (updated on success)
     * @return ReturnCode
     * 
     * Optimization: tries cached keys first before database brute-force.
     */
    int readSectorWithCache(int sector, String& cached_key_a, String& cached_key_b);
    
    /**
     * @brief Authenticate block with key
     * @param block Block number
     * @param keyA true for Key A, false for Key B
     * @param key Pointer to 6-byte key
     * @return ReturnCode
     */
    int authenticateBlock(int block, bool keyA, const uint8_t* key);
    
    /**
     * @brief Write all blocks in sector
     * @param sector Sector number
     * @return ReturnCode
     * 
     * Skips UID block and sector trailers.
     */
    int writeSector(int sector);
    
    /**
     * @brief Write block 0 (UID cloning)
     * @param data Pointer to 16-byte block data
     * @return true on success, false otherwise
     * 
     * Requires magic card with backdoor command support.
     */
    bool writeBlock0(const uint8_t* data);
    
    /**
     * @brief Extract keys from loaded dump
     * 
     * Reads sector trailers and extracts Key A/B for future writes.
     */
    void extractKeysFromDump();
    
    /**
     * @brief Write single block
     * @param block Block number
     * @return ReturnCode
     */
    int writeSingleBlock(int block);
    
    // ============================================
    // FILE OPERATIONS
    // ============================================
    
    /**
     * @brief Build full file path
     * @param filename Input filename (with or without path/extension)
     * @return Full path with /DUMPS/MIFARE/ prefix and .mfc extension
     */
    String buildFilePath(const String& filename);
    
    /**
     * @brief Parse Flipper-compatible .mfc file
     * @param file Open file handle
     * @return true on success, false on parse error
     */
    bool parseFileFormat(File& file);
    
    /**
     * @brief Write Flipper-compatible .mfc file
     * @param file Open file handle
     */
    void writeFileFormat(File& file);
    
    // ============================================
    // HELPERS
    // ============================================
    
    /**
     * @brief Convert UID to hex string
     * @return UID as spaced hex string (e.g., "04 A1 B2 C3")
     */
    String uidToString();
    
    /**
     * @brief Convert dump blocks to hex string
     * @param start_block Starting block number
     * @param num_blocks Number of blocks to convert
     * @return Hex string representation
     */
    String dumpToHex(int start_block, int num_blocks);
    
    /**
     * @brief Convert hex string to bytes
     * @param hex Input hex string
     * @param output Output buffer
     * @param max_len Maximum bytes to write
     */
    void hexStringToBytes(const String& hex, uint8_t* output, size_t max_len);
    
    /**
     * @brief Re-activate card (workaround for PN532 auth bug)
     * @return true if card re-activated successfully
     * 
     * Required between failed authentication attempts.
     */
    bool reactivateCard();
};

#endif // __MIFARE_TOOL_H__
