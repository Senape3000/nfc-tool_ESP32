#include "webserver_handler.h"
#include "webFiles.h"
#include "modules/wifi/wifi_manager.h"
#include "webserver_handler_nfc.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// ============================================
// CONSTRUCTOR
// ============================================

WebServerHandler::WebServerHandler(AsyncWebServer& server, WiFiManager& wifiMgr, NFCManager& nfc)
    : _server(server), _wifiMgr(wifiMgr), _nfc(nfc), _nfcHandler(nullptr) 
{
    LOG_DEBUG("WEB", "WebServerHandler instance created");
}

// ============================================
// INITIALIZATION
// ============================================

void WebServerHandler::begin() {
    LOG_INFO("WEB", "Initializing web server...");
    
    // Register all HTTP routes
    setupRoutes();
    
    // Initialize NFC-specific routes (separate module)
    _nfcHandler = new WebServerHandlerNFC(_server, _nfc, _loginHandler);
    if (_nfcHandler) {
        _nfcHandler->setupRoutes();
        LOG_DEBUG("WEB", "NFC routes registered");
    } else {
        LOG_ERROR("WEB", "Failed to allocate NFC handler");
    }
    
    // Start AsyncWebServer
    _server.begin();
    LOG_INFO("WEB", "Web server started on port %d", WEB_SERVER_PORT);
}

// ============================================
// ROUTE REGISTRATION
// ============================================

void WebServerHandler::setupRoutes() {
    LOG_DEBUG("WEB", "Registering HTTP routes...");
    
    // ===== STATIC ASSETS (GZIPPED) =====
    
    _server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        LOG_DEBUG("WEB", "Serving style.css (gzipped)");
        auto *res = request->beginResponse(HTTP_OK, "text/css", style_web, style_web_size);
        res->addHeader("Content-Encoding", "gzip");
        request->send(res);
    });

    _server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        LOG_DEBUG("WEB", "Serving app.js (gzipped)");
        auto *res = request->beginResponse(HTTP_OK, "application/javascript", app_web, app_web_size);
        res->addHeader("Content-Encoding", "gzip");
        request->send(res);
    });

    // ===== AUTHENTICATION ROUTES =====
    
    _server.on("/login", HTTP_GET, [this](AsyncWebServerRequest *request) {
        handleLoginPage(request);
    });

    _server.on("/login", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleLoginPost(request);
    });

    _server.on("/logout", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleLogout(request);
    });

    // ===== MAIN APPLICATION PAGE (PROTECTED) =====
    
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
        // Check authentication before serving main page
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_DEBUG("WEB", "Unauthenticated access to /, redirecting to login");
            request->redirect("/login");
            return;
        }

        LOG_DEBUG("WEB", "Serving main page (index.html)");
        auto *res = request->beginResponse(HTTP_OK, "text/html", index_web, index_web_size);
        res->addHeader("Content-Encoding", "gzip");
        request->send(res);
    });

    // ===== FILE EDITOR API (PROTECTED) =====
    
    // GET /api/files/read - Read file content for editor
    _server.on("/api/files/read", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("WEB", "Unauthorized access to /api/files/read");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleReadFile(request);
    });

    // PUT /api/files/update - Update file content from editor
    _server.on("/api/files/update", HTTP_PUT, [this](AsyncWebServerRequest *request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("WEB", "Unauthorized access to /api/files/update");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleUpdateFile(request);
    });

    // ===== FILE MANAGER API (PROTECTED) =====
    
    // POST /api/files/rename - Rename or move file
    _server.on("/api/files/rename", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("WEB", "Unauthorized access to /api/files/rename");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleRenameFile(request);
    });

    // POST /api/files/mkdir - Create directory
    _server.on("/api/files/mkdir", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("WEB", "Unauthorized access to /api/files/mkdir");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleCreateDir(request);
    });

    // POST /api/files - Create new file
    _server.on("/api/files", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("WEB", "Unauthorized access to POST /api/files");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleCreateFile(request);
    });

    // GET /api/files - List directory contents
    _server.on("/api/files", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("WEB", "Unauthorized access to GET /api/files");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleListFiles(request);
    });

    // DELETE /api/files/delete - Delete file or directory
    _server.on("/api/files/delete", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("WEB", "Unauthorized access to /api/files/delete");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleDeleteFile(request);
    });

    // GET /download - Download file
    _server.on("/download", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("WEB", "Unauthorized access to /download");
            request->send(HTTP_UNAUTHORIZED, "text/plain", "Unauthorized");
            return;
        }
        handleDownload(request);
    });

    // POST /upload - Upload file (multipart/form-data handler)
    _server.on("/upload", HTTP_POST,
        // Final callback - called after all chunks received
        [this](AsyncWebServerRequest *request) {
            request->send(HTTP_OK, "application/json", "{\"success\":true}");
        },
        // Chunk callback - called for each data chunk
        [this](AsyncWebServerRequest *request, String filename, size_t index,
               uint8_t *data, size_t len, bool final) {
            // Authentication check (early exit if not authenticated)
            if (!_loginHandler.isAuthenticated(request)) {
                LOG_WARN("WEB", "Unauthorized upload attempt");
                return;
            }
            handleUpload(request, filename, index, data, len, final);
        }
    );

    // ===== SETTINGS API (PROTECTED) =====
    
    // GET /api/status - System status information
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("WEB", "Unauthorized access to /api/status");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleStatus(request);
    });

    // POST /api/wifi/add - Add WiFi credentials
    _server.on("/api/wifi/add", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("WEB", "Unauthorized access to /api/wifi/add");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleWifiAdd(request);
    });

    // POST /api/wifi/clear - Clear WiFi database
    _server.on("/api/wifi/clear", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("WEB", "Unauthorized access to /api/wifi/clear");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleWifiClear(request);
    });

    // POST /api/reboot - Reboot device
    _server.on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("WEB", "Unauthorized access to /api/reboot");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleReboot(request);
    });

    // POST /api/format - Format LittleFS
    _server.on("/api/format", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("WEB", "Unauthorized access to /api/format");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleFormat(request);
    });

    // GET /api/backup - Download full filesystem backup as ZIP
    _server.on("/api/backup", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("WEB", "Unauthorized access to /api/backup");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleBackup(request);
    });

    // ===== 404 HANDLER =====
    _server.onNotFound([](AsyncWebServerRequest *request) {
        LOG_WARN("WEB", "404 Not Found: %s", request->url().c_str());
        request->send(HTTP_NOT_FOUND, "text/plain", "Not Found");
    });
    
    LOG_INFO("WEB", "All routes registered successfully");
}
// ============================================
// AUTHENTICATION HANDLERS
// ============================================

void WebServerHandler::handleLoginPage(AsyncWebServerRequest *request) {
    // Redirect to main page if already authenticated
    if (_loginHandler.isAuthenticated(request)) {
        LOG_DEBUG("WEB", "Already authenticated, redirecting to /");
        request->redirect("/");
        return;
    }

    LOG_DEBUG("WEB", "Serving login page");
    auto *res = request->beginResponse(HTTP_OK, "text/html", login_web, login_web_size);
    res->addHeader("Content-Encoding", "gzip");
    request->send(res);
}

void WebServerHandler::handleLoginPost(AsyncWebServerRequest *request) {
    // Validate required parameters
    if (!request->hasParam("user", true) || !request->hasParam("pass", true)) {
        LOG_WARN("WEB", "Login attempt with missing credentials");
        request->redirect("/login?error=1");
        return;
    }

    String user = request->getParam("user", true)->value();
    String pass = request->getParam("pass", true)->value();

    LOG_INFO("WEB", "Login attempt for user: %s", user.c_str());

    // Authenticate using LoginHandler
    if (_loginHandler.authenticate(user, pass)) {
        // Create session and get token
        String token = _loginHandler.createSession();

        LOG_INFO("WEB", "Login successful, creating session");

        // Send redirect response with session cookie
        AsyncWebServerResponse *response = request->beginResponse(HTTP_FOUND);
        response->addHeader("Location", "/");
        _loginHandler.setSessionCookie(response, token);
        request->send(response);
    } else {
        LOG_WARN("WEB", "Login failed for user: %s", user.c_str());
        request->redirect("/login?error=1");
    }
}

void WebServerHandler::handleLogout(AsyncWebServerRequest *request) {
    LOG_INFO("WEB", "Logout requested");

    // Terminate session using LoginHandler
    _loginHandler.terminateSession(request);

    // Redirect to login page with cleared cookie
    AsyncWebServerResponse *response = request->beginResponse(HTTP_FOUND);
    response->addHeader("Location", "/login");
    _loginHandler.clearSessionCookie(response);
    request->send(response);
}

// ============================================
// FILE MANAGER API HANDLERS
// ============================================

void WebServerHandler::handleListFiles(AsyncWebServerRequest *request) {
    // Default to root directory if no path specified
    String path = "/";
    
    if (request->hasParam("path")) {
        path = request->getParam("path")->value();
        // Ensure path starts with /
        if (!path.startsWith("/")) {
            path = "/" + path;
        }
    }

    LOG_DEBUG("WEB", "Listing files in: %s", path.c_str());
    String json = listDirectoryJson(path);
    request->send(HTTP_OK, "application/json", json);
}

String WebServerHandler::listDirectoryJson(const String& path) {
    JsonDocument doc;
    JsonArray files = doc["files"].to<JsonArray>();

    // Open directory in read mode
    File root = LittleFS.open(path, "r");
    if (!root || !root.isDirectory()) {
        LOG_ERROR("WEB", "Failed to open directory: %s", path.c_str());
        
        // Return empty list with storage info
        doc["total"] = LittleFS.totalBytes();
        doc["used"] = LittleFS.usedBytes();
        
        String output;
        serializeJson(doc, output);
        return output;
    }

    LOG_DEBUG("WEB", "Scanning directory: %s", path.c_str());

    // Iterate through directory entries
    File file = root.openNextFile();
    while (file) {
        String fileName = String(file.name());
        bool isDir = file.isDirectory();
        size_t fileSize = file.size();

        // Close file immediately to free resources
        file.close();

        // Skip hidden files and system files
        if (!fileName.endsWith("/.keep") && !fileName.endsWith("/.")) {
            JsonObject fileObj = files.add<JsonObject>();

            // Extract display name (without full path)
            int lastSlash = fileName.lastIndexOf('/');
            String displayName = (lastSlash >= 0) ? fileName.substring(lastSlash + 1) : fileName;

            fileObj["name"] = displayName;
            fileObj["size"] = fileSize;
            fileObj["isDir"] = isDir;

            LOG_DEBUG("WEB", "  %s [%s, %d bytes]", 
                     displayName.c_str(), isDir ? "DIR" : "FILE", fileSize);
        }

        file = root.openNextFile();
    }

    // Close directory handle
    root.close();

    // Add filesystem storage information
    doc["total"] = LittleFS.totalBytes();
    doc["used"] = LittleFS.usedBytes();

    // Pre-allocate memory to avoid reallocation
    String output;
    output.reserve(files.size() * 80 + 100);
    serializeJson(doc, output);

    LOG_DEBUG("WEB", "Listed %d files, JSON size: %d bytes", files.size(), output.length());
    return output;
}

void WebServerHandler::handleCreateFile(AsyncWebServerRequest *request) {
    // Validate required parameter
    if (!request->hasParam("name", true)) {
        LOG_WARN("WEB", "Create file request missing 'name' parameter");
        request->send(HTTP_BAD_REQUEST, "application/json", "{\"error\":\"Missing name parameter\"}");
        return;
    }

    String filename = request->getParam("name", true)->value();
    String content = "";
    
    if (request->hasParam("content", true)) {
        content = request->getParam("content", true)->value();
    }

    // Ensure filename starts with /
    if (!filename.startsWith("/")) {
        filename = "/" + filename;
    }

    LOG_INFO("WEB", "Creating file: %s (%d bytes)", filename.c_str(), content.length());

    // Create and write file
    File file = LittleFS.open(filename, FILE_WRITE);
    if (!file) {
        LOG_ERROR("WEB", "Failed to create file: %s", filename.c_str());
        request->send(HTTP_INTERNAL_ERROR, "application/json", "{\"error\":\"Failed to create file\"}");
        return;
    }

    if (content.length() > 0) {
        file.print(content);
    }

    file.close();
    LOG_INFO("WEB", "File created successfully: %s", filename.c_str());
    request->send(HTTP_OK, "application/json", "{\"success\":true}");
}

void WebServerHandler::handleCreateDir(AsyncWebServerRequest *request) {
    // Validate required parameter
    if (!request->hasParam("name", true)) {
        LOG_WARN("WEB", "Create directory request missing 'name' parameter");
        request->send(HTTP_BAD_REQUEST, "application/json", "{\"error\":\"Missing name parameter\"}");
        return;
    }

    String dirname = request->getParam("name", true)->value();

    // Ensure dirname starts with /
    if (!dirname.startsWith("/")) {
        dirname = "/" + dirname;
    }

    LOG_INFO("WEB", "Creating directory: %s", dirname.c_str());

    if (LittleFS.mkdir(dirname)) {
        LOG_INFO("WEB", "Directory created successfully: %s", dirname.c_str());
        request->send(HTTP_OK, "application/json", "{\"success\":true}");
    } else {
        LOG_ERROR("WEB", "Failed to create directory: %s", dirname.c_str());
        request->send(HTTP_INTERNAL_ERROR, "application/json", "{\"error\":\"Failed to create directory\"}");
    }
}

void WebServerHandler::handleDeleteFile(AsyncWebServerRequest *request) {
    // Validate required parameter
    if (!request->hasParam("path")) {
        LOG_WARN("WEB", "Delete request missing 'path' parameter");
        request->send(HTTP_BAD_REQUEST, "application/json", "{\"error\":\"Missing path parameter\"}");
        return;
    }

    String path = request->getParam("path")->value();

    // Ensure path starts with /
    if (!path.startsWith("/")) {
        path = "/" + path;
    }

    LOG_INFO("WEB", "Deleting: %s", path.c_str());

    // Try to remove as file first
    if (LittleFS.remove(path)) {
        LOG_INFO("WEB", "File deleted successfully: %s", path.c_str());
        request->send(HTTP_OK, "application/json", "{\"success\":true}");
        return;
    }

    // If failed, try to remove as directory
    if (LittleFS.rmdir(path)) {
        LOG_INFO("WEB", "Directory deleted successfully: %s", path.c_str());
        request->send(HTTP_OK, "application/json", "{\"success\":true}");
        return;
    }

    LOG_ERROR("WEB", "Failed to delete: %s", path.c_str());
    request->send(HTTP_INTERNAL_ERROR, "application/json", "{\"error\":\"Failed to delete\"}");
}

void WebServerHandler::handleRenameFile(AsyncWebServerRequest *request) {
    String oldPath = "";
    String newPath = "";

    // Check both POST body and query parameters
    // POST parameters (from body)
    if (request->hasParam("oldPath", true)) {
        oldPath = request->getParam("oldPath", true)->value();
    } else if (request->hasParam("oldPath", false)) { // Query parameters
        oldPath = request->getParam("oldPath", false)->value();
    }

    if (request->hasParam("newPath", true)) {
        newPath = request->getParam("newPath", true)->value();
    } else if (request->hasParam("newPath", false)) {
        newPath = request->getParam("newPath", false)->value();
    }

    // Validate parameters
    if (oldPath.isEmpty() || newPath.isEmpty()) {
        LOG_WARN("WEB", "Rename request missing parameters");
        request->send(HTTP_BAD_REQUEST, "application/json", "{\"error\":\"Missing parameters\"}");
        return;
    }

    // Normalize paths (ensure they start with /)
    if (!oldPath.startsWith("/")) oldPath = "/" + oldPath;
    if (!newPath.startsWith("/")) newPath = "/" + newPath;

    LOG_INFO("WEB", "Renaming: %s -> %s", oldPath.c_str(), newPath.c_str());

    if (LittleFS.rename(oldPath, newPath)) {
        LOG_INFO("WEB", "Rename successful");
        request->send(HTTP_OK, "application/json", "{\"success\":true}");
    } else {
        LOG_ERROR("WEB", "Rename failed");
        request->send(HTTP_INTERNAL_ERROR, "application/json", "{\"error\":\"Rename failed\"}");
    }
}

void WebServerHandler::handleDownload(AsyncWebServerRequest *request) {
    // Validate required parameter
    if (!request->hasParam("path")) {
        LOG_WARN("WEB", "Download request missing 'path' parameter");
        request->send(HTTP_BAD_REQUEST, "text/plain", "Missing path parameter");
        return;
    }

    String path = request->getParam("path")->value();

    // Ensure path starts with /
    if (!path.startsWith("/")) {
        path = "/" + path;
    }

    LOG_INFO("WEB", "Download requested: %s", path.c_str());

    // Check if file exists
    if (!LittleFS.exists(path)) {
        LOG_ERROR("WEB", "File not found: %s", path.c_str());
        request->send(HTTP_NOT_FOUND, "text/plain", "File not found");
        return;
    }

    // Send file with download flag (Content-Disposition: attachment)
    request->send(LittleFS, path, String(), true);
    LOG_INFO("WEB", "File download initiated: %s", path.c_str());
}

void WebServerHandler::handleUpload(AsyncWebServerRequest *request, String filename,
                                    size_t index, uint8_t *data, size_t len, bool final) {
    // First chunk - open file for writing
    if (index == 0) {
        // Read target path from query parameter
        String targetPath = "/";
        if (request->hasParam("path", false)) { // false = query parameter
            targetPath = request->getParam("path", false)->value();
            LOG_DEBUG("WEB", "Upload target path: %s", targetPath.c_str());
        }

        // Ensure path ends with /
        if (!targetPath.endsWith("/")) {
            targetPath += "/";
        }

        String fullPath = targetPath + filename;
        LOG_INFO("WEB", "Upload started: %s", fullPath.c_str());

        _uploadFile = LittleFS.open(fullPath, FILE_WRITE);
        if (!_uploadFile) {
            LOG_ERROR("WEB", "Failed to open file for upload: %s", fullPath.c_str());
            return;
        }
    }

    // Write data chunk
    if (_uploadFile && len > 0) {
        size_t written = _uploadFile.write(data, len);
        if (written != len) {
            LOG_ERROR("WEB", "Write error: expected %d, wrote %d bytes", len, written);
        }
    }

    // Final chunk - close file
    if (final) {
        if (_uploadFile) {
            _uploadFile.flush();
            _uploadFile.close();
        }
        LOG_INFO("WEB", "Upload complete: %s (%d bytes total)", filename.c_str(), index + len);
    }
}

// ============================================
// FILE EDITOR API HANDLERS
// ============================================

void WebServerHandler::handleReadFile(AsyncWebServerRequest *request) {
    // Validate required parameter
    if (!request->hasParam("path")) {
        LOG_WARN("WEB", "Read file request missing 'path' parameter");
        request->send(HTTP_BAD_REQUEST, "application/json", "{\"error\":\"Missing path parameter\"}");
        return;
    }

    String path = request->getParam("path")->value();
    LOG_INFO("WEB", "Read file requested: %s", path.c_str());

    // Ensure path starts with /
    if (!path.startsWith("/")) {
        path = "/" + path;
    }

    // Check if file exists
    if (!LittleFS.exists(path)) {
        LOG_ERROR("WEB", "File not found: %s", path.c_str());
        request->send(HTTP_NOT_FOUND, "application/json", "{\"error\":\"File not found\"}");
        return;
    }

    // Open file in read mode
    File file = LittleFS.open(path, FILE_READ);
    if (!file) {
        LOG_ERROR("WEB", "Failed to open file: %s", path.c_str());
        request->send(HTTP_INTERNAL_ERROR, "application/json", "{\"error\":\"Failed to open file\"}");
        return;
    }

    size_t fileSize = file.size();
    LOG_DEBUG("WEB", "File size: %d bytes", fileSize);

    // Check size limit to prevent OOM
    if (fileSize > MAX_FILE_READ_SIZE) {
        file.close();
        LOG_ERROR("WEB", "File too large for editor: %d bytes (max: %d)", 
                 fileSize, MAX_FILE_READ_SIZE);
        request->send(HTTP_PAYLOAD_TOO_LARGE, "application/json", 
                     "{\"error\":\"File too large (max 20KB)\"}");
        return;
    }

    // Check available memory
    size_t freeHeap = ESP.getFreeHeap();
    LOG_DEBUG("WEB", "Free heap: %d bytes", freeHeap);

    // Need at least 3x file size (content + JSON encoding + buffer)
    size_t requiredHeap = (fileSize * HEAP_SAFETY_MULTIPLIER) + HEAP_SAFETY_MARGIN;
    if (freeHeap < requiredHeap) {
        file.close();
        LOG_ERROR("WEB", "Insufficient memory: need %d, have %d bytes", 
                 requiredHeap, freeHeap);
        request->send(HTTP_INSUFFICIENT_STORAGE, "application/json", 
                     "{\"error\":\"Insufficient memory\"}");
        return;
    }

    // Read file content
    String content = file.readString();
    file.close();
    LOG_DEBUG("WEB", "Content read: %d bytes", content.length());

    // Build JSON response
    JsonDocument doc;
    doc["path"] = path;
    doc["size"] = content.length();
    doc["content"] = content;

    String output;
    // Pre-allocate memory: path (100) + size (10) + content (2x for JSON escaping) + overhead (200)
    size_t estimatedSize = 300 + (fileSize * 2);
    output.reserve(estimatedSize);
    serializeJson(doc, output);

    LOG_DEBUG("WEB", "JSON response size: %d bytes", output.length());

    // Send response with explicit Content-Length
    AsyncWebServerResponse *response = request->beginResponse(HTTP_OK, "application/json", output);
    response->addHeader("Content-Length", String(output.length()));
    request->send(response);

    LOG_INFO("WEB", "File read successfully: %s", path.c_str());
}

void WebServerHandler::handleUpdateFile(AsyncWebServerRequest *request) {
    // Validate required parameters
    if (!request->hasParam("path", true) || !request->hasParam("content", true)) {
        LOG_WARN("WEB", "Update file request missing parameters");
        request->send(HTTP_BAD_REQUEST, "application/json", "{\"error\":\"Missing parameters\"}");
        return;
    }

    String path = request->getParam("path", true)->value();
    String content = request->getParam("content", true)->value();

    // Ensure path starts with /
    if (!path.startsWith("/")) {
        path = "/" + path;
    }

    LOG_INFO("WEB", "Updating file: %s (%d bytes)", path.c_str(), content.length());

    // Check size limit
    if (content.length() > MAX_FILE_WRITE_SIZE) {
        LOG_ERROR("WEB", "Content too large: %d bytes (max: %d)", 
                 content.length(), MAX_FILE_WRITE_SIZE);
        request->send(HTTP_PAYLOAD_TOO_LARGE, "application/json", 
                     "{\"error\":\"Content too large (max 100KB)\"}");
        return;
    }

    // Open file for writing (overwrites existing content)
    File file = LittleFS.open(path, FILE_WRITE);
    if (!file) {
        LOG_ERROR("WEB", "Failed to open file for writing: %s", path.c_str());
        request->send(HTTP_INTERNAL_ERROR, "application/json", "{\"error\":\"Failed to open file\"}");
        return;
    }

    file.print(content);
    file.close();

    LOG_INFO("WEB", "File updated successfully: %s", path.c_str());
    request->send(HTTP_OK, "application/json", "{\"success\":true}");
}
// ============================================
// SETTINGS API HANDLERS
// ============================================

void WebServerHandler::handleStatus(AsyncWebServerRequest *request) {
    LOG_DEBUG("WEB", "Status request received");

    JsonDocument doc;
    
    // Network information
    doc["ip"] = WiFi.localIP().toString();
    doc["wifiStatus"] = WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED";
    
    // Hardware information
    doc["chipModel"] = ESP.getChipModel();
    doc["freeHeap"] = ESP.getFreeHeap();
    
    // System uptime (in seconds)
    doc["uptime"] = millis() / 1000;
    
    // Session information
    doc["activeSessions"] = _loginHandler.getActiveSessionCount();
    
    // Debug mode indicator
    #if DEBUG_SKIP_AUTH
        doc["debugMode"] = true;
        LOG_DEBUG("WEB", "Debug mode flag included in status");
    #else
        doc["debugMode"] = false;
    #endif

    String output;
    serializeJson(doc, output);
    
    LOG_DEBUG("WEB", "Status response: %d bytes", output.length());
    request->send(HTTP_OK, "application/json", output);
}

void WebServerHandler::handleWifiAdd(AsyncWebServerRequest *request) {
    // Validate required parameter
    if (!request->hasParam("ssid", true)) {
        LOG_WARN("WEB", "WiFi add request missing 'ssid' parameter");
        request->send(HTTP_BAD_REQUEST, "application/json", "{\"error\":\"Missing SSID\"}");
        return;
    }

    String ssid = request->getParam("ssid", true)->value();
    String pass = request->hasParam("pass", true) ? request->getParam("pass", true)->value() : "";

    LOG_INFO("WEB", "Adding WiFi credential for SSID: %s", ssid.c_str());

    // Use WiFiManager to add/update credential
    WiFiManager::Cred cred;
    cred.ssid = ssid;
    cred.pass = pass;

    if (_wifiMgr.addOrUpdateCred(cred)) {
        LOG_INFO("WEB", "WiFi credential added successfully");
        request->send(HTTP_OK, "application/json", "{\"success\":true}");
    } else {
        LOG_ERROR("WEB", "Failed to save WiFi credential");
        request->send(HTTP_INTERNAL_ERROR, "application/json", 
                     "{\"error\":\"Failed to save credentials\"}");
    }
}

void WebServerHandler::handleWifiClear(AsyncWebServerRequest *request) {
    LOG_WARN("WEB", "Clearing all WiFi credentials");
    
    // Use WiFiManager to clear credentials
    _wifiMgr.clearCredentials();
    
    LOG_INFO("WEB", "WiFi credentials cleared successfully");
    request->send(HTTP_OK, "application/json", "{\"success\":true}");
}

void WebServerHandler::handleReboot(AsyncWebServerRequest *request) {
    LOG_WARN("WEB", "System reboot requested");
    
    // Send response before rebooting
    request->send(HTTP_OK, "application/json", 
                 "{\"success\":true,\"message\":\"Rebooting...\"}");
    
    // Wait for response to be sent
    delay(REBOOT_DELAY_MS);
    
    LOG_CRITICAL("WEB", "Rebooting system now");
    ESP.restart();
}

void WebServerHandler::handleFormat(AsyncWebServerRequest *request) {
    LOG_CRITICAL("WEB", "Filesystem format requested");
    
    if (LittleFS.format()) {
        LOG_INFO("WEB", "Filesystem formatted successfully");
        request->send(HTTP_OK, "application/json", 
                     "{\"success\":true,\"message\":\"Filesystem formatted\"}");
    } else {
        LOG_ERROR("WEB", "Filesystem format failed");
        request->send(HTTP_INTERNAL_ERROR, "application/json", 
                     "{\"error\":\"Format failed\"}");
    }
}

// ============================================
// ZIP BACKUP IMPLEMENTATION
// ============================================

/**
 * @brief Write 32-bit unsigned integer in little-endian format
 * @param stream Output stream
 * @param value Value to write
 */
void writeUInt32LE(File& file, uint32_t value) {
    file.write((uint8_t)(value & 0xFF));
    file.write((uint8_t)((value >> 8) & 0xFF));
    file.write((uint8_t)((value >> 16) & 0xFF));
    file.write((uint8_t)((value >> 24) & 0xFF));
}

/**
 * @brief Write 16-bit unsigned integer in little-endian format
 * @param stream Output stream
 * @param value Value to write
 */
void writeUInt16LE(File& file, uint16_t value) {
    file.write((uint8_t)(value & 0xFF));
    file.write((uint8_t)((value >> 8) & 0xFF));
}

/**
 * @brief Calculate CRC32 checksum for ZIP format
 * @param data Data buffer
 * @param length Buffer length
 * @return CRC32 checksum
 * 
 * Uses standard CRC32 polynomial (0xEDB88320) as per ZIP specification
 */
uint32_t calculateCRC32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    return ~crc;
}

/**
 * @brief File information structure for ZIP backup
 */
struct FileInfo {
    String path;          // Path in ZIP archive (without leading /)
    uint32_t offset;      // Offset in ZIP file
    uint32_t size;        // File size in bytes
    uint32_t crc;         // CRC32 checksum
    bool isDir;           // True if directory
};

// Task to clean up temporary backup file after transfer completes
void cleanupBackupTask(void* parameter) {
    String* tempPath = (String*)parameter;
    
    // Wait 20 seconds to ensure file transfer is complete
    // Even on slow connections, 30KB should transfer in <30s
    delay(TIMEOUT_DELETE_TEMP_ZIP);
    
    if (LittleFS.exists(*tempPath)) {
        if (LittleFS.remove(*tempPath)) {
            LOG_INFO("WEB", "Temporary backup file deleted: %s", tempPath->c_str());
        } else {
            LOG_WARN("WEB", "Failed to delete temporary backup file: %s", tempPath->c_str());
        }
    }
    
    delete tempPath; // Free allocated string
    vTaskDelete(NULL); // Terminate cleanup task
}

void WebServerHandler::handleBackup(AsyncWebServerRequest *request) {
    LOG_INFO("WEB", "Full filesystem backup requested");

    // Determine backup filename
    String zipFilename = "ESP32_Backup.zip";
    if (request->hasParam("filename")) {
        zipFilename = request->getParam("filename")->value();
    }

    // Define temporary file path
    String tempPath = "/temp_backup.zip";
    
    LOG_INFO("WEB", "Creating backup: %s (temp: %s)", zipFilename.c_str(), tempPath.c_str());

    // Open temporary file for writing
    File backupFile = LittleFS.open(tempPath, FILE_WRITE);
    if (!backupFile) {
        LOG_ERROR("WEB", "Failed to create temporary backup file");
        request->send(HTTP_INTERNAL_ERROR, "application/json", 
                     "{\"error\":\"Failed to create backup file\"}");
        return;
    }

    // Use vector for dynamic file list (no fixed limit)
    std::vector<FileInfo> fileInfos;
    uint32_t currentOffset = 0;

    // Recursive directory processing lambda
    std::function<void(const String&)> processDirectory;
    processDirectory = [&](const String& path) {
        File dir = LittleFS.open(path);
        if (!dir || !dir.isDirectory()) {
            LOG_WARN("WEB", "Failed to open directory: %s", path.c_str());
            return;
        }

        LOG_DEBUG("WEB", "Processing directory: %s", path.c_str());

        File file = dir.openNextFile();
        while (file) {
            // Yield to other tasks to prevent watchdog timeout
            delay(1);
            // Build full path (handles both ESP32 Core 2.x and 3.x)
            String fileName = String(file.name());
            String fullPath;
            
            if (fileName.startsWith("/")) {
                // Core 2.x style - already absolute path
                fullPath = fileName;
            } else {
                // Core 3.x style - relative path, need to construct full path
                fullPath = path;
                if (!fullPath.endsWith("/")) fullPath += "/";
                fullPath += fileName;
            }

            // Skip hidden files, system files, and the temp backup itself
            if (fileName.startsWith(".") || fullPath.endsWith("/.keep") || 
                fullPath.endsWith("/temp_backup.zip") || fullPath == "/temp_backup.zip") {
                file = dir.openNextFile();
                continue;
            }

            if (file.isDirectory()) {
                // Process directory
                LOG_DEBUG("WEB", "Adding directory: %s", fullPath.c_str());

                // Normalize path for ZIP (remove leading /, add trailing /)
                String zipPath = fullPath;
                if (zipPath.startsWith("/")) zipPath = zipPath.substring(1);
                if (!zipPath.endsWith("/")) zipPath += "/";

                // Save directory info
                FileInfo info;
                info.path = zipPath;
                info.offset = currentOffset;
                info.size = 0;
                info.crc = 0;
                info.isDir = true;
                fileInfos.push_back(info);

                // Write ZIP Local File Header for directory
                writeUInt32LE(backupFile, ZIP_LOCAL_HEADER_SIG);
                writeUInt16LE(backupFile, ZIP_VERSION);           // Version needed to extract
                writeUInt16LE(backupFile, 0);                     // General purpose bit flag
                writeUInt16LE(backupFile, 0);                     // Compression method (0 = stored)
                writeUInt16LE(backupFile, 0);                     // Last mod file time
                writeUInt16LE(backupFile, 0);                     // Last mod file date
                writeUInt32LE(backupFile, 0);                     // CRC-32
                writeUInt32LE(backupFile, 0);                     // Compressed size
                writeUInt32LE(backupFile, 0);                     // Uncompressed size
                writeUInt16LE(backupFile, zipPath.length());      // Filename length
                writeUInt16LE(backupFile, 0);                     // Extra field length
                backupFile.print(zipPath);

                currentOffset += ZIP_LOCAL_HEADER_SIZE + zipPath.length();

                // Close current file before recursing (prevents file descriptor exhaustion)
                file.close();
                
                // Recurse into subdirectory
                processDirectory(fullPath);
                
                // Reopen directory to continue iteration
                file = dir.openNextFile();
                continue;
            }

            // Process regular file
            size_t fileSize = file.size();

            // Skip files that are too large (memory constraint)
            if (fileSize > MAX_BACKUP_FILE_SIZE) {
                LOG_WARN("WEB", "Skipping large file: %s (%d bytes)", fullPath.c_str(), fileSize);
                file = dir.openNextFile();
                continue;
            }

            LOG_DEBUG("WEB", "Adding file: %s (%d bytes)", fullPath.c_str(), fileSize);

            // Normalize path for ZIP (remove leading /)
            String zipPath = fullPath;
            if (zipPath.startsWith("/")) zipPath = zipPath.substring(1);

            // Read file content into buffer
            uint8_t* buffer = new uint8_t[fileSize];
            if (!buffer) {
                LOG_ERROR("WEB", "Memory allocation failed for file: %s", fullPath.c_str());
                file = dir.openNextFile();
                continue;
            }

            file.read(buffer, fileSize);
            file.close(); // Close immediately after reading

            // Calculate CRC32 checksum
            uint32_t crc = calculateCRC32(buffer, fileSize);

            // Save file info for Central Directory
            FileInfo info;
            info.path = zipPath;
            info.offset = currentOffset;
            info.size = fileSize;
            info.crc = crc;
            info.isDir = false;
            fileInfos.push_back(info);

            // Write ZIP Local File Header for file
            writeUInt32LE(backupFile, ZIP_LOCAL_HEADER_SIG);
            writeUInt16LE(backupFile, ZIP_VERSION);           // Version needed
            writeUInt16LE(backupFile, 0);                     // Flags
            writeUInt16LE(backupFile, 0);                     // Compression (0 = stored)
            writeUInt16LE(backupFile, 0);                     // Time
            writeUInt16LE(backupFile, 0);                     // Date
            writeUInt32LE(backupFile, crc);                   // CRC-32
            writeUInt32LE(backupFile, fileSize);              // Compressed size
            writeUInt32LE(backupFile, fileSize);              // Uncompressed size
            writeUInt16LE(backupFile, zipPath.length());      // Filename length
            writeUInt16LE(backupFile, 0);                     // Extra field length
            backupFile.print(zipPath);                       // Filename
            backupFile.write(buffer, fileSize);              // File content

            currentOffset += ZIP_LOCAL_HEADER_SIZE + zipPath.length() + fileSize;

            delete[] buffer;
            file = dir.openNextFile();
        }

        dir.close(); // Important: close directory when done
    };

    // Start recursive processing from root
    processDirectory("/");

    LOG_INFO("WEB", "Processed %d entries, writing Central Directory", fileInfos.size());

    // ==========================================
    // WRITE CENTRAL DIRECTORY
    // ==========================================

    uint32_t centralDirStart = currentOffset;
    uint32_t centralDirSize = 0;

    for (const auto& info : fileInfos) {
        // Write Central Directory File Header
        writeUInt32LE(backupFile, ZIP_CENTRAL_HEADER_SIG);
        writeUInt16LE(backupFile, ZIP_VERSION);           // Version made by
        writeUInt16LE(backupFile, ZIP_VERSION);           // Version needed to extract
        writeUInt16LE(backupFile, 0);                     // General purpose bit flag
        writeUInt16LE(backupFile, 0);                     // Compression method
        writeUInt16LE(backupFile, 0);                     // Last mod file time
        writeUInt16LE(backupFile, 0);                     // Last mod file date
        writeUInt32LE(backupFile, info.crc);              // CRC-32
        writeUInt32LE(backupFile, info.size);             // Compressed size
        writeUInt32LE(backupFile, info.size);             // Uncompressed size
        writeUInt16LE(backupFile, info.path.length());    // Filename length
        writeUInt16LE(backupFile, 0);                     // Extra field length
        writeUInt16LE(backupFile, 0);                     // File comment length
        writeUInt16LE(backupFile, 0);                     // Disk number start
        writeUInt16LE(backupFile, 0);                     // Internal file attributes
        writeUInt32LE(backupFile, info.isDir ? 0x10 : 0); // External file attributes (0x10 = directory)
        writeUInt32LE(backupFile, info.offset);           // Relative offset of local header
        backupFile.print(info.path);                     // Filename

        centralDirSize += ZIP_CENTRAL_HEADER_SIZE + info.path.length();
    }

    LOG_DEBUG("WEB", "Central Directory size: %d bytes", centralDirSize);

    // ==========================================
    // WRITE END OF CENTRAL DIRECTORY RECORD
    // ==========================================

    writeUInt32LE(backupFile, ZIP_END_CENTRAL_SIG);
    writeUInt16LE(backupFile, 0);                     // Number of this disk
    writeUInt16LE(backupFile, 0);                     // Disk where central directory starts
    writeUInt16LE(backupFile, fileInfos.size());      // Number of entries on this disk
    writeUInt16LE(backupFile, fileInfos.size());      // Total number of entries
    writeUInt32LE(backupFile, centralDirSize);        // Size of central directory
    writeUInt32LE(backupFile, centralDirStart);       // Offset of central directory
    writeUInt16LE(backupFile, 0);                     // Comment length

    // Close backup file
    backupFile.close();

    LOG_INFO("WEB", "Backup complete: %d entries, %d bytes total", 
             fileInfos.size(), currentOffset + centralDirSize);

    // Send file to client with custom filename
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, tempPath, "application/zip", true);
    response->addHeader("Content-Disposition", "attachment; filename=\"" + zipFilename + "\"");
    request->send(response);
    
    LOG_INFO("WEB", "Backup file sent to client, scheduling cleanup");
    
    // Schedule cleanup task to delete temp file after transfer completes
    String* pathCopy = new String(tempPath); // Allocate on heap for task
    xTaskCreate(
        cleanupBackupTask,    // Task function
        "BackupCleanup",      // Task name
        2048,                 // Small stack
        (void*)pathCopy,      // Pass path as parameter
        1,                    // Low priority
        NULL                  // No handle needed
    );
}
