#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>
#include <functional>
#include "login_handler.h"
#include "logger.h"
#include "config.h"

// Forward declarations
class WiFiManager;
class NFCManager;
class WebServerHandlerNFC;

/**
 * @brief Main HTTP web server handler for NFC Tool web interface
 * 
 * Manages all HTTP routes including:
 * - Static asset serving (HTML/CSS/JS with gzip compression)
 * - Authentication (login/logout with session management)
 * - File manager API (CRUD operations on LittleFS)
 * - File editor API (read/write file contents)
 * - System settings API (WiFi, reboot, format)
 * - Full filesystem backup (ZIP download)
 * - NFC operations (delegated to WebServerHandlerNFC)
 * 
 * Architecture:
 * - All routes use session-based authentication via LoginHandler
 * - File operations limited by size constraints (configurable)
 * - JSON API for frontend communication
 * - Async handlers for non-blocking operations
 * 
 * Security Notes:
 * - All API endpoints require authentication (except /login)
 * - File paths normalized to prevent directory traversal
 * - Size limits on uploads and file reads to prevent OOM
 * - DEBUG_SKIP_AUTH flag for development (unsafe for production)
 */

// Backup cleanup timeout (place BEFORE class definition)
constexpr uint32_t TIMEOUT_DELETE_TEMP_ZIP = 20000;  // 20 seconds

class WebServerHandler {
public:
    /**
     * @brief Construct WebServerHandler with required managers
     * @param server Reference to AsyncWebServer instance
     * @param wifiMgr Reference to WiFiManager for network operations
     * @param nfc Reference to NFCManager for NFC operations
     */
    explicit WebServerHandler(AsyncWebServer& server, WiFiManager& wifiMgr, NFCManager& nfc);

    /**
     * @brief Initialize web server and register all routes
     * 
     * Call this after LittleFS.begin() and WiFi initialization.
     * Sets up all HTTP routes and starts the AsyncWebServer.
     */
    void begin();

private:
    // ============================================
    // CONSTANTS
    // ============================================
    
    // File size limits
    static constexpr size_t MAX_FILE_READ_SIZE = 20000;      // 20KB max for editor
    static constexpr size_t MAX_FILE_WRITE_SIZE = 102400;    // 100KB max for uploads
    static constexpr size_t MAX_BACKUP_FILE_SIZE = 25000;    // 25KB max per file in ZIP
    
    // Memory safety thresholds
    static constexpr size_t HEAP_SAFETY_MULTIPLIER = 3;      // Need 3x file size in RAM
    static constexpr size_t HEAP_SAFETY_MARGIN = 10000;      // 10KB safety margin
    
    // Zip backup constants
    static constexpr uint32_t ZIP_LOCAL_HEADER_SIG = 0x04034b50;
    static constexpr uint32_t ZIP_CENTRAL_HEADER_SIG = 0x02014b50;
    static constexpr uint32_t ZIP_END_CENTRAL_SIG = 0x06054b50;
    static constexpr uint16_t ZIP_VERSION = 20;
    static constexpr size_t ZIP_LOCAL_HEADER_SIZE = 30;
    static constexpr size_t ZIP_CENTRAL_HEADER_SIZE = 46;

    // HTTP status codes (for clarity)
    static constexpr int HTTP_OK = 200;
    static constexpr int HTTP_FOUND = 302;
    static constexpr int HTTP_BAD_REQUEST = 400;
    static constexpr int HTTP_UNAUTHORIZED = 401;
    static constexpr int HTTP_NOT_FOUND = 404;
    static constexpr int HTTP_PAYLOAD_TOO_LARGE = 413;
    static constexpr int HTTP_INTERNAL_ERROR = 500;
    static constexpr int HTTP_INSUFFICIENT_STORAGE = 507;
    
    // System operation delays
    static constexpr uint32_t REBOOT_DELAY_MS = 1000;  // 1 second before restart

    // ============================================
    // MEMBER VARIABLES
    // ============================================
    
    AsyncWebServer& _server;              // Reference to web server
    WiFiManager& _wifiMgr;                // Reference to WiFi manager
    LoginHandler _loginHandler;           // Authentication handler
    NFCManager& _nfc;                     // Reference to NFC manager
    WebServerHandlerNFC* _nfcHandler;     // NFC-specific route handler
    bool _loggedIn = false;               // Legacy flag (deprecated, use LoginHandler)
    File _uploadFile;                     // Current upload file handle

    // ============================================
    // ROUTE SETUP
    // ============================================
    
    /**
     * @brief Register all HTTP routes with server
     * 
     * Routes are organized into sections:
     * - Static assets: /style.css, /app.js (gzipped)
     * - Authentication: /login (GET/POST), /logout
     * - Main interface: / (requires auth)
     * - File manager API: /api/files/* (CRUD operations)
     * - File editor API: /api/files/read, /api/files/update
     * - Upload/download: /upload, /download
     * - Settings API: /api/status, /api/wifi/*, /api/reboot, /api/format
     * - Backup: /api/backup (full filesystem ZIP)
     * - 404 handler
     */
    void setupRoutes();

    // ============================================
    // AUTHENTICATION HANDLERS
    // ============================================
    
    /**
     * @brief Serve login page or redirect if already authenticated
     * @param request HTTP request
     */
    void handleLoginPage(AsyncWebServerRequest *request);

    /**
     * @brief Process login form submission
     * @param request HTTP request with POST data (user, pass)
     * 
     * On success: creates session, sets cookie, redirects to /
     * On failure: redirects to /login?error=1
     */
    void handleLoginPost(AsyncWebServerRequest *request);

    /**
     * @brief Terminate user session and clear cookie
     * @param request HTTP request with session cookie
     */
    void handleLogout(AsyncWebServerRequest *request);

    // ============================================
    // FILE MANAGER API HANDLERS
    // ============================================
    
    /**
     * @brief List files in directory as JSON
     * @param request HTTP request with optional ?path= parameter
     * 
     * Returns JSON: {files: [{name, size, isDir}], total, used}
     */
    void handleListFiles(AsyncWebServerRequest *request);

    /**
     * @brief Create new file
     * @param request HTTP request with POST data (name, content)
     */
    void handleCreateFile(AsyncWebServerRequest *request);

    /**
     * @brief Create new directory
     * @param request HTTP request with POST data (name)
     */
    void handleCreateDir(AsyncWebServerRequest *request);

    /**
     * @brief Delete file or directory
     * @param request HTTP request with ?path= parameter
     * 
     * Tries remove() first, then rmdir() for directories
     */
    void handleDeleteFile(AsyncWebServerRequest *request);

    /**
     * @brief Rename or move file/directory
     * @param request HTTP request with POST/query params (oldPath, newPath)
     */
    void handleRenameFile(AsyncWebServerRequest *request);

    /**
     * @brief Serve file download
     * @param request HTTP request with ?path= parameter
     * 
     * Sets Content-Disposition: attachment to force download
     */
    void handleDownload(AsyncWebServerRequest *request);

    /**
     * @brief Handle file upload (multipart/form-data)
     * @param request HTTP request
     * @param filename Original filename
     * @param index Current byte offset
     * @param data Chunk data buffer
     * @param len Chunk length
     * @param final True if last chunk
     * 
     * Handles chunked uploads for large files.
     * Target path specified via ?path= query parameter.
     */
    void handleUpload(AsyncWebServerRequest *request, String filename,
                     size_t index, uint8_t *data, size_t len, bool final);

    // ============================================
    // FILE EDITOR API HANDLERS
    // ============================================
    
    /**
     * @brief Read file content for editor
     * @param request HTTP request with ?path= parameter
     * 
     * Returns JSON: {path, size, content}
     * Limited to MAX_FILE_READ_SIZE to prevent OOM
     */
    void handleReadFile(AsyncWebServerRequest *request);

    /**
     * @brief Update file content from editor
     * @param request HTTP request with POST data (path, content)
     * 
     * Limited to MAX_FILE_WRITE_SIZE to prevent OOM
     */
    void handleUpdateFile(AsyncWebServerRequest *request);

    // ============================================
    // SETTINGS API HANDLERS
    // ============================================
    
    /**
     * @brief Get system status
     * @param request HTTP request
     * 
     * Returns JSON with IP, heap, WiFi status, uptime, etc.
     */
    void handleStatus(AsyncWebServerRequest *request);

    /**
     * @brief Add WiFi credentials
     * @param request HTTP request with POST data (ssid, pass)
     */
    void handleWifiAdd(AsyncWebServerRequest *request);

    /**
     * @brief Clear all WiFi credentials
     * @param request HTTP request
     */
    void handleWifiClear(AsyncWebServerRequest *request);

    /**
     * @brief Reboot device
     * @param request HTTP request
     * 
     * Sends response, waits REBOOT_DELAY_MS, then calls ESP.restart()
     */
    void handleReboot(AsyncWebServerRequest *request);

    /**
     * @brief Format LittleFS filesystem
     * @param request HTTP request
     * 
     * WARNING: Destroys all data!
     */
    void handleFormat(AsyncWebServerRequest *request);

    // ============================================
    // BACKUP HANDLER
    // ============================================
    
    /**
     * @brief Generate and download full filesystem backup as ZIP
     * @param request HTTP request with optional ?filename= parameter
     * 
     * Creates ZIP archive with entire LittleFS contents.
     * Uses streaming response to avoid RAM constraints.
     * 
     * Implementation details:
     * - Recursively walks directory tree
     * - Skips files > MAX_BACKUP_FILE_SIZE
     * - Skips hidden files (starts with .)
     * - Generates standard ZIP format (no compression)
     * - Includes central directory and end-of-central-directory records
     */
    void handleBackup(AsyncWebServerRequest *request);

    // ============================================
    // HELPER FUNCTIONS
    // ============================================
    
    /**
     * @brief Generate JSON list of directory contents
     * @param path Directory path to list
     * @return JSON string with files array and storage info
     */
    String listDirectoryJson(const String& path);

    /**
     * @brief Check if user is authenticated (deprecated)
     * @return Always returns _loggedIn flag
     * @deprecated Use _loginHandler.isAuthenticated() instead
     */
    bool isAuthenticated() { return _loggedIn; }
};
