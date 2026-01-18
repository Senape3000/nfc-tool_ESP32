#ifndef __MIFARE_KEYS_MANAGER_H__
#define __MIFARE_KEYS_MANAGER_H__

#include <Arduino.h>
#include <LittleFS.h>
#include <set>
#include <string>
#include "logger.h"

/**
 * @brief Manages Mifare Classic keys database
 * 
 * Features:
 * - Optimized for LittleFS with lazy loading
 * - Automatic duplicate prevention using std::set
 * - Default keys database creation
 * - Hex key validation
 * - Byte array conversion utilities
 * 
 * Architecture:
 * - Static singleton pattern (no instance needed)
 * - Keys stored in /mifare_keys.txt (one per line)
 * - Supports comments (lines starting with # or //)
 * - Thread-safe for single-core access
 * 
 * Key Format:
 * - 12 hexadecimal characters (case-insensitive)
 * - Represents 6 bytes (48 bits)
 * - Example: "FFFFFFFFFFFF" (factory default)
 * 
 * Usage:
 * @code
 * MifareKeysManager::begin();  // Initialize in setup()
 * MifareKeysManager::addKey("A0A1A2A3A4A5");
 * if (MifareKeysManager::hasKey("FFFFFFFFFFFF")) {
 *     // Key exists in database
 * }
 * @endcode
 */
class MifareKeysManager {
public:
    // ============================================
    // CONSTANTS
    // ============================================
    
    // File paths
    static constexpr const char* KEYS_PATH = "/mifare_keys.txt";
    static constexpr const char* KEYS_DIR = "/";
    
    // Key format constants
    static constexpr size_t KEY_HEX_LENGTH = 12;     // 12 hex chars = 6 bytes
    static constexpr size_t KEY_BYTE_LENGTH = 6;     // 6 bytes = 48 bits
    static constexpr size_t CHARS_PER_BYTE = 2;      // 2 hex chars per byte
    static constexpr uint8_t HEX_PADDING_THRESHOLD = 0x10; // Values < 0x10 need leading zero
    
    // ============================================
    // PUBLIC METHODS
    // ============================================
    
    /**
     * @brief Initialize key manager (call in setup)
     * @return true if initialization successful, false otherwise
     * 
     * Creates keys directory if it doesn't exist.
     * Loads existing keys or creates default database.
     */
    static bool begin();
    
    /**
     * @brief Ensure keys are loaded (lazy loading)
     * 
     * Loads keys from file on first access.
     * Subsequent calls are no-op if already loaded.
     */
    static void ensureLoaded();
    
    /**
     * @brief Add a key to the database
     * @param key Hex string (12 characters, case-insensitive)
     * @return true if added successfully, false if invalid or duplicate
     * 
     * Automatically converts to uppercase and removes spaces.
     * Prevents duplicates using std::set.
     * Appends to file immediately.
     */
    static bool addKey(const String& key);
    
    /**
     * @brief Remove a key from the database
     * @param key Hex string to remove
     * @return true if removed, false if not found
     * 
     * Rewrites entire file after removal.
     */
    static bool removeKey(const String& key);
    
    /**
     * @brief Check if a key exists in the database
     * @param key Hex string to search
     * @return true if key exists, false otherwise
     */
    static bool hasKey(const String& key);
    
    /**
     * @brief Clear all keys from database
     * 
     * Removes file and clears in-memory set.
     * Marks as not loaded for future lazy loading.
     */
    static void clear();
    
    /**
     * @brief Get reference to all keys
     * @return Const reference to std::set of keys
     * 
     * Ensures keys are loaded before returning.
     */
    static const std::set<String>& getKeys() { 
        ensureLoaded(); 
        return _keys; 
    }
    
    /**
     * @brief Get number of keys in database
     * @return Key count
     */
    static size_t getKeyCount() { 
        ensureLoaded(); 
        return _keys.size(); 
    }
    
    /**
     * @brief Validate hex key format
     * @param key String to validate
     * @return true if valid (12 hex chars), false otherwise
     */
    static bool isValidHexKey(const String& key);
    
    /**
     * @brief Convert hex string to byte array
     * @param key Hex string (12 chars)
     * @param bytes Output buffer (must be at least 6 bytes)
     * @return true if conversion successful, false if invalid format
     * 
     * Used for PN532 library compatibility.
     */
    static bool keyToBytes(const String& key, uint8_t* bytes);
    
    /**
     * @brief Convert byte array to hex string
     * @param bytes Input buffer (6 bytes)
     * @return Uppercase hex string (12 characters)
     */
    static String bytesToKey(const uint8_t* bytes);

private:
    // ============================================
    // PRIVATE MEMBERS
    // ============================================
    
    static std::set<String> _keys;    // In-memory key database (sorted, unique)
    static bool _loaded;              // Lazy loading flag
    
    // ============================================
    // PRIVATE METHODS
    // ============================================
    
    /**
     * @brief Load keys from file
     * 
     * Parses file line by line.
     * Skips comments (# or //) and empty lines.
     * Validates each key before adding to set.
     */
    static void loadFromFile();
    
    /**
     * @brief Save all keys to file (overwrites)
     * 
     * Creates file with header comments.
     * Writes all keys from in-memory set.
     */
    static void saveToFile();
    
    /**
     * @brief Append single key to file
     * @param key Key to append
     * 
     * Uses append mode for efficiency.
     * Falls back to full rewrite if append fails.
     */
    static void appendToFile(const String& key);
    
    /**
     * @brief Create default keys database
     * 
     * Populates with common factory and test keys:
     * - FFFFFFFFFFFF (factory default)
     * - A0A1A2A3A4A5 (MAD key)
     * - D3F7D3F7D3F7 (NDEF key)
     * - 000000000000 (blank)
     * - And other common keys
     */
    static void createDefaultFile();
};

#endif // __MIFARE_KEYS_MANAGER_H__
