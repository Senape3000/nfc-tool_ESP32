#include "mifare_keys_manager.h"

// ============================================
// STATIC MEMBER INITIALIZATION
// ============================================

std::set<String> MifareKeysManager::_keys;
bool MifareKeysManager::_loaded = false;

// ============================================
// PUBLIC METHODS
// ============================================

bool MifareKeysManager::begin() {
    LOG_INFO("MFC-KEYS", "Initializing key database...");

    // Create directory if it doesn't exist
    if (!LittleFS.exists(KEYS_DIR)) {
        if (!LittleFS.mkdir(KEYS_DIR)) {
            LOG_ERROR("MFC-KEYS", "Failed to create directory: %s", KEYS_DIR);
            return false;
        }
        LOG_DEBUG("MFC-KEYS", "Created directory: %s", KEYS_DIR);
    }

    // Load keys (lazy loading)
    ensureLoaded();
    
    LOG_INFO("MFC-KEYS", "Ready with %d keys", _keys.size());
    return true;
}

void MifareKeysManager::ensureLoaded() {
    if (_loaded) {
        return;
    }

    if (LittleFS.exists(KEYS_PATH)) {
        LOG_DEBUG("MFC-KEYS", "Loading keys from file...");
        loadFromFile();
    } else {
        LOG_INFO("MFC-KEYS", "Keys file not found, creating default database");
        createDefaultFile();
    }

    _loaded = true;
}

bool MifareKeysManager::addKey(const String& key) {
    // Clean and normalize key
    String cleanKey = key;
    cleanKey.toUpperCase();
    cleanKey.replace(" ", "");

    // Validate format
    if (!isValidHexKey(cleanKey)) {
        LOG_WARN("MFC-KEYS", "Invalid key format: %s", cleanKey.c_str());
        return false;
    }

    ensureLoaded();

    // Check for duplicates (std::set handles this automatically)
    if (_keys.find(cleanKey) != _keys.end()) {
        LOG_DEBUG("MFC-KEYS", "Key already exists: %s", cleanKey.c_str());
        return false;
    }

    // Add to in-memory set
    _keys.insert(cleanKey);
    
    // Append to file
    appendToFile(cleanKey);
    
    LOG_INFO("MFC-KEYS", "Key added: %s", cleanKey.c_str());
    return true;
}

bool MifareKeysManager::removeKey(const String& key) {
    ensureLoaded();

    auto it = _keys.find(key);
    if (it == _keys.end()) {
        LOG_WARN("MFC-KEYS", "Key not found: %s", key.c_str());
        return false;
    }

    // Remove from in-memory set
    _keys.erase(it);
    
    // Rewrite entire file (necessary for removal)
    saveToFile();
    
    LOG_INFO("MFC-KEYS", "Key removed: %s", key.c_str());
    return true;
}

bool MifareKeysManager::hasKey(const String& key) {
    ensureLoaded();
    return _keys.find(key) != _keys.end();
}

void MifareKeysManager::clear() {
    // Clear in-memory set
    _keys.clear();

    // Delete file if exists
    if (LittleFS.exists(KEYS_PATH)) {
        if (LittleFS.remove(KEYS_PATH)) {
            LOG_INFO("MFC-KEYS", "Keys file deleted");
        } else {
            LOG_ERROR("MFC-KEYS", "Failed to delete keys file");
        }
    }

    // Reset loaded flag
    _loaded = false;
    
    LOG_INFO("MFC-KEYS", "All keys cleared");
}

bool MifareKeysManager::isValidHexKey(const String& key) {
    // Check length (must be exactly 12 hex characters)
    if (key.length() != KEY_HEX_LENGTH) {
        return false;
    }

    // Check if all characters are valid hexadecimal
    for (size_t i = 0; i < key.length(); i++) {
        if (!isxdigit(key[i])) {
            return false;
        }
    }

    return true;
}

bool MifareKeysManager::keyToBytes(const String& key, uint8_t* bytes) {
    // Validate format before conversion
    if (!isValidHexKey(key)) {
        LOG_ERROR("MFC-KEYS", "Cannot convert invalid key to bytes: %s", key.c_str());
        return false;
    }

    // Convert each pair of hex chars to a byte
    for (size_t i = 0; i < KEY_BYTE_LENGTH; i++) {
        String byte_str = key.substring(i * CHARS_PER_BYTE, i * CHARS_PER_BYTE + CHARS_PER_BYTE);
        bytes[i] = strtoul(byte_str.c_str(), NULL, 16);
    }

    // Debug: verify conversion
    LOG_DEBUG("MFC-KEYS", "Key to bytes: %s -> %02X %02X %02X %02X %02X %02X",
             key.c_str(), bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);

    return true;
}

String MifareKeysManager::bytesToKey(const uint8_t* bytes) {
    String result;
    result.reserve(KEY_HEX_LENGTH); // Pre-allocate memory

    // Convert each byte to 2-character hex string
    for (size_t i = 0; i < KEY_BYTE_LENGTH; i++) {
        // Add leading zero for values < 0x10
        if (bytes[i] < HEX_PADDING_THRESHOLD) {
            result += "0";
        }
        result += String(bytes[i], HEX);
    }

    result.toUpperCase();
    return result;
}

// ============================================
// PRIVATE METHODS
// ============================================

void MifareKeysManager::loadFromFile() {
    File file = LittleFS.open(KEYS_PATH, FILE_READ);
    if (!file) {
        LOG_ERROR("MFC-KEYS", "Failed to open keys file: %s", KEYS_PATH);
        return;
    }

    _keys.clear();
    int loaded = 0;
    int skipped = 0;

    // Parse file line by line
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();

        // Skip empty lines and comments
        if (line.length() == 0 || line.startsWith("//") || line.startsWith("#")) {
            continue;
        }

        // Normalize key
        line.toUpperCase();
        line.replace(" ", "");

        // Validate and add
        if (isValidHexKey(line)) {
            _keys.insert(line); // std::set prevents duplicates automatically
            loaded++;
        } else {
            LOG_WARN("MFC-KEYS", "Invalid key skipped: %s", line.c_str());
            skipped++;
        }
    }

    file.close();

    if (skipped > 0) {
        LOG_INFO("MFC-KEYS", "Loaded %d keys (%d invalid skipped)", loaded, skipped);
    } else {
        LOG_INFO("MFC-KEYS", "Loaded %d keys", loaded);
    }
}

void MifareKeysManager::saveToFile() {
    File file = LittleFS.open(KEYS_PATH, FILE_WRITE);
    if (!file) {
        LOG_ERROR("MFC-KEYS", "Failed to save keys to file: %s", KEYS_PATH);
        return;
    }

    // Write header
    file.println("# MIFARE CLASSIC KEYS DATABASE");
    file.println("# One key per line (12 hex chars = 6 bytes)");
    file.println("#");
    file.println("# STANDARD KEYS");

    // Write all keys
    for (const auto& key : _keys) {
        file.println(key);
    }

    // Write footer
    file.println("#");
    file.println("# Add your custom keys below");

    file.close();
    
    LOG_INFO("MFC-KEYS", "Saved %d keys to file", _keys.size());
}

void MifareKeysManager::appendToFile(const String& key) {
    // If file doesn't exist, create with full structure
    if (!LittleFS.exists(KEYS_PATH)) {
        LOG_DEBUG("MFC-KEYS", "File doesn't exist, creating with saveToFile()");
        saveToFile();
        return;
    }

    // Try to append
    File file = LittleFS.open(KEYS_PATH, FILE_APPEND);
    if (!file) {
        LOG_WARN("MFC-KEYS", "Append failed, rewriting full file");
        saveToFile();
        return;
    }

    file.println(key);
    file.close();
    
    LOG_DEBUG("MFC-KEYS", "Key appended to file: %s", key.c_str());
}

void MifareKeysManager::createDefaultFile() {
    LOG_INFO("MFC-KEYS", "Creating default keys database");

    // Most common standard keys
    _keys.insert("FFFFFFFFFFFF"); // Factory default (NXP)
    _keys.insert("A0A1A2A3A4A5"); // MAD (Mifare Application Directory) key
    _keys.insert("D3F7D3F7D3F7"); // NDEF (NFC Data Exchange Format) key
    _keys.insert("000000000000"); // Common blank key
    _keys.insert("B0B1B2B3B4B5"); // Alternative default

    // Write to file
    saveToFile();
    
    LOG_INFO("MFC-KEYS", "Default database created with %d keys", _keys.size());
}
