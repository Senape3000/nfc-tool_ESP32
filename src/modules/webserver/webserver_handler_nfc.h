#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <vector>
#include <esp_task_wdt.h>
#include "modules/rfid/nfc_manager.h"
#include "login_handler.h"
#include "logger.h"

// ============================================
// ASYNC TASK DATA STRUCTURES
// ============================================

/**
 * @brief Data structure for SRIX read task (FreeRTOS)
 * 
 * Contains all parameters and results for non-blocking NFC read operation.
 * Task runs on Core 1 to avoid blocking the web server on Core 0.
 */
struct nfcReadTaskSRIXData {
    NFCManager* nfc;                  // Pointer to NFC manager instance
    int timeout_sec;                  // Read timeout in seconds
    NFCManager::TagInfo tagInfo;      // Output: tag information
    NFCManager::Result result;        // Output: operation result
    bool completed;                   // Flag: task completed
    volatile bool running;            // Flag: task currently executing
};

/**
 * @brief Data structure for SRIX write task (FreeRTOS)
 * 
 * Full tag write can take 90+ seconds (128 blocks with delays).
 */
struct nfcWriteTaskSRIXData {
    NFCManager* nfc;                  // Pointer to NFC manager instance
    NFCManager::TagInfo tagInfo;      // Input: tag data to write
    int timeoutsec;                   // Write timeout in seconds
    NFCManager::Result result;        // Output: operation result
    bool completed;                   // Flag: task completed
    volatile bool running;            // Flag: task currently executing
};

/**
 * @brief Data structure for SRIX selective write task (FreeRTOS)
 * 
 * Writes only specified blocks instead of entire tag.
 * Estimated time: ~2.5s per block.
 */
struct nfcWriteSelectiveTaskSRIXData {
    NFCManager* nfc;                  // Pointer to NFC manager instance
    std::vector<uint8_t> block_numbers; // Blocks to write (0-127)
    NFCManager::Result result;        // Output: operation result
    bool completed;                   // Flag: task completed
    volatile bool running;            // Flag: task currently executing
};

/**
 * @brief Data structure for Mifare read task (FreeRTOS)
 * 
 * Supports both full dump and UID-only read modes.
 */
struct nfcReadTaskMifareData {
    NFCManager* nfc;                  // Pointer to NFC manager instance
    int timeout_sec;                  // Read timeout in seconds
    bool uid_only;                    // true = UID only, false = full dump
    NFCManager::TagInfo tagInfo;      // Output: tag information
    NFCManager::Result result;        // Output: operation result
    bool completed;                   // Flag: task completed
    volatile bool running;            // Flag: task currently executing
};

/**
 * @brief Data structure for Mifare write task (FreeRTOS)
 * 
 * Full Mifare 1K write: 16 sectors with authentication ≈ 30+ seconds.
 */
struct nfcWriteTaskMifareData {
    NFCManager* nfc;                  // Pointer to NFC manager instance
    NFCManager::TagInfo tagInfo;      // Input: tag data to write
    int timeout_sec;                  // Write timeout in seconds
    NFCManager::Result result;        // Output: operation result
    bool completed;                   // Flag: task completed
    volatile bool running;            // Flag: task currently executing
};

/**
 * @brief Data structure for Mifare UID clone task (FreeRTOS)
 * 
 * Clones only the UID to a magic card (Gen2/CUID).
 */
struct nfcCloneTaskMifareData {
    NFCManager* nfc;                  // Pointer to NFC manager instance
    NFCManager::TagInfo tagInfo;      // Input: tag with UID to clone
    int timeout_sec;                  // Clone timeout in seconds
    NFCManager::Result result;        // Output: operation result
    bool completed;                   // Flag: task completed
    volatile bool running;            // Flag: task currently executing
};

/**
 * @brief Data structure for Mifare selective write task (FreeRTOS)
 * 
 * Writes only specified blocks instead of entire tag.
 */
struct nfcWriteSelectiveTaskMifareData {
    NFCManager* nfc;                  // Pointer to NFC manager instance
    std::vector<uint8_t> block_numbers; // Blocks to write
    NFCManager::Result result;        // Output: operation result
    bool completed;                   // Flag: task completed
    volatile bool running;            // Flag: task currently executing
};

// ============================================
// TASK FUNCTION DECLARATIONS
// ============================================

/**
 * @brief FreeRTOS task for SRIX read operation
 * @param parameter Pointer to nfcReadTaskSRIXData structure
 * 
 * Runs on Core 1, self-deletes on completion.
 */
void nfcReadTaskSRIX(void* parameter);

/**
 * @brief FreeRTOS task for SRIX write operation
 * @param parameter Pointer to nfcWriteTaskSRIXData structure
 * 
 * Auto-cleanup: deletes data structure on completion.
 */
void nfcWriteTaskSRIX(void* parameter);

/**
 * @brief FreeRTOS task for SRIX selective write operation
 * @param parameter Pointer to nfcWriteSelectiveTaskSRIXData structure
 */
void nfcWriteSelectiveTaskSRIX(void* parameter);

/**
 * @brief FreeRTOS task for Mifare read operation
 * @param parameter Pointer to nfcReadTaskMifareData structure
 */
void nfcReadTaskMifare(void* parameter);

/**
 * @brief FreeRTOS task for Mifare write operation
 * @param parameter Pointer to nfcWriteTaskMifareData structure
 * 
 * Auto-cleanup: deletes data structure on completion.
 */
void nfcWriteTaskMifare(void* parameter);

/**
 * @brief FreeRTOS task for Mifare UID clone operation
 * @param parameter Pointer to nfcCloneTaskMifareData structure
 * 
 * Auto-cleanup: deletes data structure on completion.
 */
void nfcCloneTaskMifare(void* parameter);

/**
 * @brief FreeRTOS task for Mifare selective write operation
 * @param parameter Pointer to nfcWriteSelectiveTaskMifareData structure
 */
void nfcWriteSelectiveTaskMifare(void* parameter);

// ============================================
// WEB SERVER HANDLER CLASS
// ============================================

/**
 * @brief WebServerHandlerNFC - Handles all NFC-related API routes
 * 
 * Architecture:
 * - Separated from main WebServerHandler for modularity
 * - Supports both SRIX (ISO 15693) and Mifare Classic protocols
 * - Uses FreeRTOS tasks on Core 1 for non-blocking operations
 * - All routes require authentication via LoginHandler
 * 
 * Route Categories:
 * 1. SRIX API: read, write, compare, write-selective
 * 2. Mifare API: read, read-uid, write, clone, compare, write-selective
 * 3. Unified API: save, load, list, delete, status (protocol-agnostic)
 * 4. Static Files: nfc-tab.html, nfc-app.js (frontend assets)
 * 
 * Task Management:
 * - Long-running operations execute on Core 1 via FreeRTOS tasks
 * - Web server polls task completion with watchdog resets
 * - Automatic cleanup for completed tasks
 * - Timeout protection prevents indefinite blocking
 * 
 * Memory Safety:
 * - Task data structures allocated on heap, not stack
 * - Proper cleanup on timeout or completion
 * - Watchdog timer management to prevent resets
 */
class WebServerHandlerNFC {
public:
    /**
     * @brief Construct NFC web handler
     * @param server Reference to AsyncWebServer instance
     * @param nfc Reference to NFCManager for tag operations
     * @param login Reference to LoginHandler for authentication
     */
    explicit WebServerHandlerNFC(AsyncWebServer& server, NFCManager& nfc, LoginHandler& login);

    /**
     * @brief Register all NFC-related HTTP routes
     * 
     * Call this during server initialization to set up all endpoints.
     */
    void setupRoutes();

private:
    // ============================================
    // CONSTANTS
    // ============================================
    
    // Timing constants for task polling
    static constexpr uint32_t POLL_INTERVAL_MS = 50;           // Standard polling interval
    static constexpr uint32_t POLL_INTERVAL_SLOW_MS = 100;     // Slow polling for writes
    static constexpr uint32_t POLL_INTERVAL_VERY_SLOW_MS = 500; // Very slow for long operations
    static constexpr uint32_t TASK_CLEANUP_DELAY_MS = 100;     // Delay before task cleanup
    static constexpr uint32_t REBOOT_DELAY_MS = 1000;          // Delay before system reboot
    
    // Timeout margins (added to user-specified timeouts)
    static constexpr uint32_t TIMEOUT_MARGIN_READ_MS = 2000;   // 2s margin for reads
    static constexpr uint32_t TIMEOUT_MARGIN_WRITE_MS = 5000;  // 5s margin for writes
    static constexpr uint32_t SRIX_FULL_WRITE_EXTRA_MS = 90000; // 90s extra for SRIX full write
    static constexpr uint32_t MIFARE_FULL_WRITE_EXTRA_MS = 40000; // 40s extra for Mifare write
    static constexpr uint32_t SELECTIVE_WRITE_EXTRA_MS = 10000; // 10s extra for selective write
    
    // Retry limits for task completion waiting
    static constexpr int MAX_RETRIES_STANDARD = 10;            // Standard retry count
    static constexpr int MAX_RETRIES_LONG_OP = 30;             // For long operations
    static constexpr int MAX_RETRIES_VERY_LONG_OP = 50;        // For very long operations
    
    // Task configuration
    static constexpr uint32_t TASK_STACK_SIZE_SMALL = 8192;    // 8KB for simple tasks
    static constexpr uint32_t TASK_STACK_SIZE_MEDIUM = 10240;  // 10KB for Mifare (auth)
    static constexpr uint32_t TASK_STACK_SIZE_LARGE = 16384;   // 16KB for selective writes
    static constexpr uint8_t TASK_PRIORITY = 1;                // FreeRTOS task priority
    static constexpr uint8_t TASK_CORE_ID = 1;                 // Core 1 (opposite of web server)
    
    // Block limits
    static constexpr uint8_t SRIX_MAX_BLOCK = 127;             // SRIX block range: 0-127
    
    // Estimated timing for selective writes
    static constexpr uint32_t MS_PER_BLOCK_SRIX = 2600;        // ~2.6s per SRIX block
    
    // Dump preview size
    static constexpr size_t DUMP_PREVIEW_BYTES = 64;           // First 64 bytes for preview
    
    // HTTP status codes
    static constexpr int HTTP_OK = 200;
    static constexpr int HTTP_BAD_REQUEST = 400;
    static constexpr int HTTP_UNAUTHORIZED = 401;
    static constexpr int HTTP_INTERNAL_ERROR = 500;

    // ============================================
    // MEMBER VARIABLES
    // ============================================
    
    AsyncWebServer& _server;          // Reference to web server
    NFCManager& _nfc;                 // Reference to NFC manager
    LoginHandler& _loginHandler;      // Reference to authentication handler

    // ============================================
    // SRIX HANDLERS (ISO 15693)
    // ============================================
    
    /**
     * @brief Handle SRIX tag read request
     * @param request HTTP request
     * @param data JSON body data
     * @param len Data length
     * 
     * JSON input: {timeout: seconds}
     * JSON output: {success, message, protocol, uid, size, dump}
     */
    void handleSRIXRead(AsyncWebServerRequest* request, uint8_t* data, size_t len);

    /**
     * @brief Handle SRIX tag write request
     * @param request HTTP request
     * @param data JSON body data
     * @param len Data length
     * 
     * Writes loaded data to physical tag. Can take 90+ seconds for full write.
     * JSON input: {timeout: seconds}
     */
    void handleSRIXWrite(AsyncWebServerRequest* request, uint8_t* data, size_t len);

    /**
     * @brief Wait for SRIX tag presence (polling)
     * @param request HTTP request
     * 
     * Blocks until tag detected or timeout.
     */
    void handleSRIXWait(AsyncWebServerRequest* request);

    /**
     * @brief Compare loaded dump with physical tag
     * @param request HTTP request
     * @param data JSON body data
     * @param len Data length
     * 
     * Reads physical tag and compares with loaded data.
     * Returns block-by-block differences.
     */
    void handleSRIXCompare(AsyncWebServerRequest* request, uint8_t* data, size_t len);

    /**
     * @brief Write only specified blocks to tag
     * @param request HTTP request
     * @param data JSON body data
     * @param len Data length
     * 
     * JSON input: {blocks: [array of block numbers 0-127]}
     * Estimated time: ~2.6s per block.
     */
    void handleSRIXWriteSelective(AsyncWebServerRequest* request, uint8_t* data, size_t len);

    // ============================================
    // MIFARE HANDLERS
    // ============================================
    
    /**
     * @brief Handle Mifare tag full dump read
     * @param request HTTP request
     * @param data JSON body data
     * @param len Data length
     * 
     * Performs full sector read with authentication.
     * JSON input: {timeout: seconds}
     */
    void handleMifareRead(AsyncWebServerRequest* request, uint8_t* data, size_t len);

    /**
     * @brief Handle Mifare UID-only read
     * @param request HTTP request
     * @param data JSON body data
     * @param len Data length
     * 
     * Fast UID read without authentication or dump.
     * JSON input: {timeout: seconds}
     */
    void handleMifareReadUID(AsyncWebServerRequest* request, uint8_t* data, size_t len);

    /**
     * @brief Handle Mifare tag write request
     * @param request HTTP request
     * @param data JSON body data
     * @param len Data length
     * 
     * Writes loaded data to physical tag with authentication.
     * Mifare 1K: ~30+ seconds for 16 sectors.
     */
    void handleMifareWrite(AsyncWebServerRequest* request, uint8_t* data, size_t len);

    /**
     * @brief Handle Mifare UID clone request
     * @param request HTTP request
     * @param data JSON body data
     * @param len Data length
     * 
     * Clones UID to magic card (Gen2/CUID).
     * Requires compatible writable card.
     */
    void handleMifareClone(AsyncWebServerRequest* request, uint8_t* data, size_t len);

    /**
     * @brief Compare loaded Mifare dump with physical tag
     * @param request HTTP request
     * @param data JSON body data
     * @param len Data length
     * 
     * Reads physical tag and compares with loaded data.
     */
    void handleMifareCompare(AsyncWebServerRequest* request, uint8_t* data, size_t len);

    /**
     * @brief Write only specified blocks to Mifare tag
     * @param request HTTP request
     * @param data JSON body data
     * @param len Data length
     * 
     * JSON input: {blocks: [array of block numbers]}
     */
    void handleMifareWriteSelective(AsyncWebServerRequest* request, uint8_t* data, size_t len);

    // ============================================
    // UNIFIED HANDLERS (PROTOCOL-AGNOSTIC)
    // ============================================
    
    /**
     * @brief Save current tag dump to LittleFS
     * @param request HTTP request with ?filename= parameter
     * 
     * Saves as .srix or .mfc depending on protocol.
     */
    void handleSave(AsyncWebServerRequest* request);

    /**
     * @brief Load tag dump from LittleFS
     * @param request HTTP request with ?filename= parameter
     * 
     * Loads .srix or .mfc file into memory.
     */
    void handleLoad(AsyncWebServerRequest* request);

    /**
     * @brief List all saved tag dumps
     * @param request HTTP request
     * 
     * Returns JSON array of filenames with metadata.
     */
    void handleList(AsyncWebServerRequest* request);

    /**
     * @brief Delete saved tag dump
     * @param request HTTP request with ?filename= parameter
     */
    void handleDelete(AsyncWebServerRequest* request);

    /**
     * @brief Get NFC system status
     * @param request HTTP request
     * 
     * Returns: ready, srix_hw, has_data, protocol, uid.
     */
    void handleStatus(AsyncWebServerRequest* request);

    // ============================================
    // STATIC FILE HANDLERS
    // ============================================
    
    /**
     * @brief Serve NFC tab HTML (gzipped)
     * @param request HTTP request
     */
    void handleNFCTabHTML(AsyncWebServerRequest* request);

    /**
     * @brief Serve NFC app JavaScript (gzipped)
     * @param request HTTP request
     */
    void handleNFCAppJS(AsyncWebServerRequest* request);

    // ============================================
    // HELPER FUNCTIONS
    // ============================================
    
    /**
     * @brief Compare two tag dumps and generate difference report
     * @param loadedData Pointer to loaded dump data
     * @param loadedSize Size of loaded dump
     * @param physicalData Pointer to physical tag data
     * @param physicalSize Size of physical tag data
     * @param responseDoc JSON document to populate with results
     * 
     * Populates responseDoc with:
     * - identical: boolean
     * - total_blocks: number
     * - different_blocks: number
     * - differences: array of {block, loaded, physical}
     */
    void compareTagData(
        const uint8_t* loadedData,
        size_t loadedSize,
        const uint8_t* physicalData,
        size_t physicalSize,
        JsonDocument& responseDoc);
};
