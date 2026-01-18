#include "webserver_handler_nfc.h"
#include "webFiles.h"
#include <LittleFS.h>

// ============================================
// FREERTOS TASK FUNCTIONS (NON-BLOCKING)
// ============================================

/**
 * @brief SRIX read task - runs on Core 1
 * @param parameter Pointer to nfcReadTaskSRIXData structure
 * 
 * Executes blocking NFC read operation without blocking web server.
 * Removes itself from watchdog timer to prevent system resets.
 */
void nfcReadTaskSRIX(void* parameter) {
    nfcReadTaskSRIXData* data = (nfcReadTaskSRIXData*)parameter;
    data->running = true;
    
    LOG_INFO("NFC-TASK", "Read task started on Core 1");

    // Remove task from watchdog timer if registered
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_delete(NULL);
        LOG_DEBUG("NFC-TASK", "Removed from watchdog timer");
    }

    // Execute blocking read (can take up to timeout_sec seconds)
    data->result = data->nfc->readSRIX(data->tagInfo, data->timeout_sec);
    
    data->running = false;
    data->completed = true;
    
    LOG_INFO("NFC-TASK", "Read task completed - success: %d", data->result.success);
    vTaskDelete(NULL); // Self-delete task
}

/**
 * @brief SRIX write task - runs on Core 1
 * @param parameter Pointer to nfcWriteTaskSRIXData structure
 * 
 * Executes blocking NFC write operation.
 * Full SRIX write: 128 blocks × (write + delay + wait) ≈ 90+ seconds.
 * Auto-cleanup: deletes data structure on completion.
 */
void nfcWriteTaskSRIX(void* parameter) {
    nfcWriteTaskSRIXData* data = (nfcWriteTaskSRIXData*)parameter;
    data->running = true;
    
    LOG_INFO("NFC-TASK", "Write task started on Core 1");

    // Remove from watchdog timer
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_delete(NULL);
    }

    // Execute blocking write (can take 90+ seconds)
    data->result = data->nfc->writeSRIX(data->tagInfo, data->timeoutsec);
    
    data->running = false;
    data->completed = true;
    
    LOG_INFO("NFC-TASK", "Write task completed - success: %d, code: %d", 
             data->result.success, data->result.code);

    // Wait briefly to allow web server to read results
    delay(100);
    
    // Auto-cleanup: delete data structure (caller won't do it)
    delete data;
    vTaskDelete(NULL);
}

/**
 * @brief SRIX selective write task - runs on Core 1
 * @param parameter Pointer to nfcWriteSelectiveTaskSRIXData structure
 * 
 * Writes only specified blocks instead of entire tag.
 * Estimated time: ~2.6s per block.
 */
void nfcWriteSelectiveTaskSRIX(void* parameter) {
    nfcWriteSelectiveTaskSRIXData* data = (nfcWriteSelectiveTaskSRIXData*)parameter;
    data->running = true;
    
    LOG_INFO("NFC-TASK", "Write-Selective task started on Core 1 (%d blocks)", 
             data->block_numbers.size());

    // Remove from watchdog timer
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_delete(NULL);
    }

    // Execute selective write (can take several minutes for many blocks)
    data->result = data->nfc->writeSRIXBlocksSelective(data->block_numbers);
    
    data->running = false;
    data->completed = true;
    
    LOG_INFO("NFC-TASK", "Write-Selective task completed - success: %d", 
             data->result.success);

    // Wait briefly to allow web server to read results
    delay(100);
    
    // NOTE: Data cleanup done by web server for selective write
    vTaskDelete(NULL);
}

/**
 * @brief Mifare read task - runs on Core 1
 * @param parameter Pointer to nfcReadTaskMifareData structure
 * 
 * Supports both UID-only read and full dump with authentication.
 */
void nfcReadTaskMifare(void* parameter) {
    nfcReadTaskMifareData* data = (nfcReadTaskMifareData*)parameter;
    data->running = true;
    
    LOG_INFO("NFC-TASK", "Mifare Read task started on Core 1 (uid_only: %d)", 
             data->uid_only);

    // Remove from watchdog timer
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_delete(NULL);
    }

    // Execute read based on mode
    if (data->uid_only) {
        data->result = data->nfc->readMifareUID(data->tagInfo, data->timeout_sec);
    } else {
        data->result = data->nfc->readMifare(data->tagInfo, data->timeout_sec);
    }
    
    data->running = false;
    data->completed = true;
    
    LOG_INFO("NFC-TASK", "Mifare Read task completed - success: %d", 
             data->result.success);
    
    vTaskDelete(NULL);
}

/**
 * @brief Mifare write task - runs on Core 1
 * @param parameter Pointer to nfcWriteTaskMifareData structure
 * 
 * Writes complete tag with sector authentication.
 * Mifare 1K: 16 sectors ≈ 30+ seconds.
 * Auto-cleanup: deletes data structure on completion.
 */
void nfcWriteTaskMifare(void* parameter) {
    nfcWriteTaskMifareData* data = (nfcWriteTaskMifareData*)parameter;
    data->running = true;
    
    LOG_INFO("NFC-TASK", "Mifare Write task started on Core 1");

    // Remove from watchdog timer
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_delete(NULL);
    }

    // Execute write with authentication
    data->result = data->nfc->writeMifare(data->tagInfo, data->timeout_sec);
    
    data->running = false;
    data->completed = true;
    
    LOG_INFO("NFC-TASK", "Mifare Write task completed - success: %d, code: %d", 
             data->result.success, data->result.code);

    // Wait briefly for web server to read results
    delay(100);
    
    // Auto-cleanup
    delete data;
    vTaskDelete(NULL);
}

/**
 * @brief Mifare UID clone task - runs on Core 1
 * @param parameter Pointer to nfcCloneTaskMifareData structure
 * 
 * Clones UID to magic card (Gen2/CUID compatible).
 * Auto-cleanup: deletes data structure on completion.
 */
void nfcCloneTaskMifare(void* parameter) {
    nfcCloneTaskMifareData* data = (nfcCloneTaskMifareData*)parameter;
    data->running = true;
    
    LOG_INFO("NFC-TASK", "Mifare Clone UID task started on Core 1");

    // Remove from watchdog timer
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_delete(NULL);
    }

    // Execute UID clone
    data->result = data->nfc->cloneMifareUID(data->tagInfo, data->timeout_sec);
    
    data->running = false;
    data->completed = true;
    
    LOG_INFO("NFC-TASK", "Mifare Clone task completed - success: %d", 
             data->result.success);

    // Wait briefly for web server
    delay(100);
    
    // Auto-cleanup
    delete data;
    vTaskDelete(NULL);
}

/**
 * @brief Mifare selective write task - runs on Core 1
 * @param parameter Pointer to nfcWriteSelectiveTaskMifareData structure
 * 
 * Writes only specified blocks with authentication.
 */
void nfcWriteSelectiveTaskMifare(void* parameter) {
    nfcWriteSelectiveTaskMifareData* data = (nfcWriteSelectiveTaskMifareData*)parameter;
    data->running = true;
    
    LOG_INFO("NFC-TASK", "Mifare Write-Selective task started on Core 1 (%d blocks)", 
             data->block_numbers.size());

    // Remove from watchdog timer
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_delete(NULL);
    }

    // Execute selective write with authentication
    data->result = data->nfc->writeMifareBlocksSelective(data->block_numbers);
    
    data->running = false;
    data->completed = true;
    
    LOG_INFO("NFC-TASK", "Mifare Write-Selective task completed - success: %d", 
             data->result.success);

    // Wait briefly for web server
    delay(100);
    
    // NOTE: Cleanup done by web server for selective write
    vTaskDelete(NULL);
}

// ============================================
// CONSTRUCTOR AND SETUP
// ============================================

WebServerHandlerNFC::WebServerHandlerNFC(AsyncWebServer& server, NFCManager& nfc, LoginHandler& login)
    : _server(server), _nfc(nfc), _loginHandler(login) 
{
    LOG_DEBUG("NFC-WEB", "WebServerHandlerNFC instance created");
}

void WebServerHandlerNFC::setupRoutes() {
    LOG_INFO("NFC-WEB", "Setting up NFC routes...");

    // ============================================
    // STATIC FILES (PROTECTED)
    // ============================================

    _server.on("/nfc-tab.html", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("NFC-WEB", "Unauthorized access to /nfc-tab.html");
            request->send(HTTP_UNAUTHORIZED, "text/plain", "Unauthorized");
            return;
        }
        handleNFCTabHTML(request);
    });

    _server.on("/nfc-app.js", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("NFC-WEB", "Unauthorized access to /nfc-app.js");
            request->send(HTTP_UNAUTHORIZED, "text/plain", "Unauthorized");
            return;
        }
        handleNFCAppJS(request);
    });

    // ============================================
    // SRIX API ROUTES (PROTECTED)
    // ============================================

    _server.on("/api/nfc/srix/read", HTTP_POST,
        [this](AsyncWebServerRequest* request) { /* Placeholder */ },
        NULL,
        [this](AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!_loginHandler.isAuthenticated(request)) {
                LOG_WARN("NFC-WEB", "Unauthorized access to /api/nfc/srix/read");
                request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
                return;
            }
            handleSRIXRead(request, data, len);
        }
    );

    _server.on("/api/nfc/srix/write", HTTP_POST,
        [this](AsyncWebServerRequest* request) { /* Placeholder */ },
        NULL,
        [this](AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!_loginHandler.isAuthenticated(request)) {
                LOG_WARN("NFC-WEB", "Unauthorized access to /api/nfc/srix/write");
                request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
                return;
            }
            handleSRIXWrite(request, data, len);
        }
    );

    _server.on("/api/nfc/srix/compare", HTTP_POST,
        [this](AsyncWebServerRequest* request) { /* Placeholder */ },
        NULL,
        [this](AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!_loginHandler.isAuthenticated(request)) {
                LOG_WARN("NFC-WEB", "Unauthorized access to /api/nfc/srix/compare");
                request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
                return;
            }
            handleSRIXCompare(request, data, len);
        }
    );

    _server.on("/api/nfc/srix/write-selective", HTTP_POST,
        [this](AsyncWebServerRequest* request) { /* Placeholder */ },
        NULL,
        [this](AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!_loginHandler.isAuthenticated(request)) {
                LOG_WARN("NFC-WEB", "Unauthorized access to /api/nfc/srix/write-selective");
                request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
                return;
            }
            handleSRIXWriteSelective(request, data, len);
        }
    );

    // ============================================
    // MIFARE API ROUTES (PROTECTED)
    // ============================================

    _server.on("/api/nfc/mifare/read", HTTP_POST,
        [this](AsyncWebServerRequest* request) { /* Placeholder */ },
        NULL,
        [this](AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!_loginHandler.isAuthenticated(request)) {
                LOG_WARN("NFC-WEB", "Unauthorized access to /api/nfc/mifare/read");
                request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
                return;
            }
            handleMifareRead(request, data, len);
        }
    );

    _server.on("/api/nfc/mifare/read-uid", HTTP_POST,
        [this](AsyncWebServerRequest* request) { /* Placeholder */ },
        NULL,
        [this](AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!_loginHandler.isAuthenticated(request)) {
                LOG_WARN("NFC-WEB", "Unauthorized access to /api/nfc/mifare/read-uid");
                request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
                return;
            }
            handleMifareReadUID(request, data, len);
        }
    );

    _server.on("/api/nfc/mifare/write", HTTP_POST,
        [this](AsyncWebServerRequest* request) { /* Placeholder */ },
        NULL,
        [this](AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!_loginHandler.isAuthenticated(request)) {
                LOG_WARN("NFC-WEB", "Unauthorized access to /api/nfc/mifare/write");
                request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
                return;
            }
            handleMifareWrite(request, data, len);
        }
    );

    _server.on("/api/nfc/mifare/clone", HTTP_POST,
        [this](AsyncWebServerRequest* request) { /* Placeholder */ },
        NULL,
        [this](AsyncWebServerRequest* request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (!_loginHandler.isAuthenticated(request)) {
                LOG_WARN("NFC-WEB", "Unauthorized access to /api/nfc/mifare/clone");
                request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
                return;
            }
            handleMifareClone(request, data, len);
        }
    );

    _server.on("/api/nfc/mifare/compare", HTTP_POST,
        [this](AsyncWebServerRequest* request) { /* Placeholder */ },
        NULL,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!_loginHandler.isAuthenticated(request)) {
                LOG_WARN("NFC-WEB", "Unauthorized access to /api/nfc/mifare/compare");
                request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
                return;
            }
            handleMifareCompare(request, data, len);
        }
    );

    _server.on("/api/nfc/mifare/write-selective", HTTP_POST,
        [this](AsyncWebServerRequest* request) { /* Placeholder */ },
        NULL,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!_loginHandler.isAuthenticated(request)) {
                LOG_WARN("NFC-WEB", "Unauthorized access to /api/nfc/mifare/write-selective");
                request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
                return;
            }
            handleMifareWriteSelective(request, data, len);
        }
    );

    // ============================================
    // UNIFIED API ROUTES (PROTOCOL-AGNOSTIC)
    // ============================================

    _server.on("/api/nfc/save", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("NFC-WEB", "Unauthorized access to /api/nfc/save");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleSave(request);
    });

    _server.on("/api/nfc/load", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("NFC-WEB", "Unauthorized access to /api/nfc/load");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleLoad(request);
    });

    _server.on("/api/nfc/list", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("NFC-WEB", "Unauthorized access to /api/nfc/list");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleList(request);
    });

    _server.on("/api/nfc/delete", HTTP_DELETE, [this](AsyncWebServerRequest* request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("NFC-WEB", "Unauthorized access to /api/nfc/delete");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleDelete(request);
    });

    _server.on("/api/nfc/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!_loginHandler.isAuthenticated(request)) {
            LOG_WARN("NFC-WEB", "Unauthorized access to /api/nfc/status");
            request->send(HTTP_UNAUTHORIZED, "application/json", "{\"error\":\"Unauthorized\"}");
            return;
        }
        handleStatus(request);
    });

    LOG_INFO("NFC-WEB", "All NFC routes registered successfully");
}
// ============================================
// SRIX HANDLERS (ISO 15693)
// ============================================

void WebServerHandlerNFC::handleSRIXRead(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    // Parse JSON body for timeout parameter
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    int timeout = 10; // Default timeout in seconds
    if (!error && doc["timeout"].is<int>()) {
        timeout = doc["timeout"].as<int>();
    }

    LOG_INFO("NFC-API", "SRIX Read request (timeout: %ds)", timeout);

    // Allocate task data structure on heap (not stack!)
    nfcReadTaskSRIXData* taskData = new nfcReadTaskSRIXData();
    taskData->nfc = &_nfc;
    taskData->timeout_sec = timeout;
    taskData->completed = false;
    taskData->running = false;

    // Create task on Core 1 (opposite to web server on Core 0)
    TaskHandle_t readTaskHandle;
    BaseType_t result = xTaskCreatePinnedToCore(
        nfcReadTaskSRIX,           // Task function
        "NFCRead",                  // Task name
        TASK_STACK_SIZE_SMALL,      // Stack size (8KB)
        taskData,                   // Parameter
        TASK_PRIORITY,              // Priority
        &readTaskHandle,            // Task handle
        TASK_CORE_ID                // Core 1
    );

    if (result != pdPASS) {
        LOG_ERROR("NFC-API", "Failed to create read task");
        delete taskData;
        request->send(HTTP_INTERNAL_ERROR, "application/json",
                     "{\"success\":false,\"message\":\"Failed to create task\"}");
        return;
    }

    LOG_DEBUG("NFC-API", "Read task created, waiting for completion...");

    // Non-blocking polling with watchdog resets
    unsigned long start = millis();
    unsigned long max_wait = (timeout * 1000) + TIMEOUT_MARGIN_READ_MS;
    
    while (!taskData->completed && (millis() - start < max_wait)) {
        delay(POLL_INTERVAL_MS);
        esp_task_wdt_reset();
    }

    // Wait for task to fully stop
    int retries = 0;
    while (taskData->running && retries < MAX_RETRIES_STANDARD) {
        delay(200);
        esp_task_wdt_reset();
        retries++;
    }

    // Build JSON response
    JsonDocument responseDoc;
    
    if (taskData->completed) {
        responseDoc["success"] = taskData->result.success;
        responseDoc["message"] = taskData->result.message;
        
        if (taskData->result.success) {
            responseDoc["protocol"] = taskData->tagInfo.protocol_name;
            responseDoc["uid"] = _nfc.uidToString(taskData->tagInfo.uid, taskData->tagInfo.uid_length);
            responseDoc["size"] = _nfc.getTagDataSize(taskData->tagInfo);

            // Generate hex dump preview (first 64 bytes)
            const uint8_t* dumpData = _nfc.getTagDataPointer(taskData->tagInfo);
            String dumpHex = "";
            size_t dumpSize = _nfc.getTagDataSize(taskData->tagInfo);
            
            for (size_t i = 0; i < DUMP_PREVIEW_BYTES && i < dumpSize; i++) {
                char hex[3];
                sprintf(hex, "%02X", dumpData[i]);
                dumpHex += hex;
            }
            responseDoc["dump"] = dumpHex;
            
            LOG_INFO("NFC-API", "Read successful - UID: %s, Size: %d bytes", 
                    responseDoc["uid"].as<const char*>(), dumpSize);
        } else {
            LOG_WARN("NFC-API", "Read failed: %s", taskData->result.message.c_str());
        }
    } else {
        responseDoc["success"] = false;
        responseDoc["message"] = "Request timeout (task still running)";
        LOG_ERROR("NFC-API", "Read timeout after %lums", millis() - start);
    }

    // Cleanup
    delete taskData;

    String output;
    serializeJson(responseDoc, output);
    request->send(HTTP_OK, "application/json", output);
}

void WebServerHandlerNFC::handleSRIXWrite(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    LOG_INFO("NFC-API", "SRIX Write request");

    // Check if we have data to write
    if (!_nfc.hasValidData()) {
        LOG_WARN("NFC-API", "Write request but no data loaded");
        request->send(HTTP_BAD_REQUEST, "application/json",
                     "{\"success\":false,\"message\":\"No data to write\"}");
        return;
    }

    // Parse JSON for timeout
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    int timeout = 10; // Default timeout
    if (!error && doc["timeout"].is<int>()) {
        timeout = doc["timeout"].as<int>();
    }

    LOG_INFO("NFC-API", "SRIX Write request - timeout: %ds", timeout);

    // Allocate task data structure on heap
    nfcWriteTaskSRIXData* taskData = new nfcWriteTaskSRIXData;
    taskData->nfc = &_nfc;
    taskData->tagInfo = _nfc.getCurrentTag();
    taskData->timeoutsec = timeout;
    taskData->completed = false;
    taskData->running = false;

    // Create task on Core 1
    TaskHandle_t writeTaskHandle;
    BaseType_t result = xTaskCreatePinnedToCore(
        nfcWriteTaskSRIX,
        "NFCWrite",
        TASK_STACK_SIZE_SMALL,
        taskData,
        TASK_PRIORITY,
        &writeTaskHandle,
        TASK_CORE_ID
    );

    if (result != pdPASS) {
        LOG_ERROR("NFC-API", "Failed to create write task");
        delete taskData;
        request->send(HTTP_INTERNAL_ERROR, "application/json",
                     "{\"success\":false,\"message\":\"Failed to create task\"}");
        return;
    }

    LOG_DEBUG("NFC-API", "Write task created, waiting for completion...");

    // EXTRA TIME FOR FULL WRITE
    // Full write SRIX: 128 blocks × (write + delay + waitForTag 600ms) ≈ 90+ seconds
    unsigned long start = millis();
    unsigned long max_wait = (timeout * 1000) + SRIX_FULL_WRITE_EXTRA_MS;
    
    while (!taskData->completed && (millis() - start < max_wait)) {
        delay(POLL_INTERVAL_SLOW_MS);
        esp_task_wdt_reset();
    }

    // Wait for task to fully stop with longer delays
    int retries = 0;
    while (taskData->running && retries < MAX_RETRIES_LONG_OP) {
        delay(POLL_INTERVAL_VERY_SLOW_MS);
        esp_task_wdt_reset();
        retries++;
    }

    // Build response
    JsonDocument responseDoc;
    
    if (taskData->completed) {
        responseDoc["success"] = taskData->result.success;
        responseDoc["message"] = taskData->result.message;
        responseDoc["code"] = taskData->result.code;
        
        LOG_INFO("NFC-API", "Write completed - success: %d, code: %d", 
                taskData->result.success, taskData->result.code);
    } else {
        responseDoc["success"] = false;
        responseDoc["message"] = "Write timeout - operation did not complete in time";
        responseDoc["code"] = -99;
        
        LOG_ERROR("NFC-API", "Write timeout after %lums", millis() - start);
    }

    // NOTE: taskData is deleted by task itself (see nfcWriteTaskSRIX)
    // Do NOT delete here!

    String output;
    serializeJson(responseDoc, output);
    request->send(HTTP_OK, "application/json", output);
}

void WebServerHandlerNFC::handleSRIXWait(AsyncWebServerRequest* request) {
    LOG_INFO("NFC-API", "SRIX Wait request");

    uint32_t timeout_ms = 5000; // Default 5 seconds
    bool detected = _nfc.waitForSRIXTag(timeout_ms);

    JsonDocument doc;
    doc["detected"] = detected;
    doc["message"] = detected ? "Tag detected" : "Timeout - no tag found";

    LOG_INFO("NFC-API", "Wait result: %s", detected ? "detected" : "timeout");

    String output;
    serializeJson(doc, output);
    request->send(HTTP_OK, "application/json", output);
}

void WebServerHandlerNFC::handleSRIXCompare(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    LOG_INFO("NFC-API", "SRIX Compare request");

    // Check if we have a loaded dump to compare
    if (!_nfc.hasValidData()) {
        LOG_WARN("NFC-API", "Compare request but no data loaded");
        request->send(HTTP_BAD_REQUEST, "application/json",
                     "{\"success\":false,\"message\":\"No loaded dump to compare\"}");
        return;
    }

    // IMPORTANT: Save loaded dump data BEFORE reading physical tag
    // (reading will overwrite the internal buffer)
    NFCManager::TagInfo loadedTag = _nfc.getCurrentTag();
    
    // Copy data to temporary buffer
    size_t loadedDataSize = _nfc.getTagDataSize(loadedTag);
    uint8_t* loadedDataCopy = new uint8_t[loadedDataSize];
    memcpy(loadedDataCopy, _nfc.getTagDataPointer(loadedTag), loadedDataSize);

    LOG_DEBUG("NFC-API", "Saved loaded dump: size=%d, UID=%02X%02X%02X%02X",
             loadedDataSize, loadedTag.uid[0], loadedTag.uid[1],
             loadedTag.uid[2], loadedTag.uid[3]);

    // Parse JSON for timeout
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    int timeout = 10;
    if (!error && doc["timeout"].is<int>()) {
        timeout = doc["timeout"].as<int>();
    }

    LOG_INFO("NFC-API", "Compare - reading physical tag with timeout=%ds", timeout);

    // Allocate struct for read task
    nfcReadTaskSRIXData* taskData = new nfcReadTaskSRIXData();
    taskData->nfc = &_nfc;
    taskData->timeout_sec = timeout;
    taskData->completed = false;
    taskData->running = false;

    // Create read task on Core 1
    TaskHandle_t readTaskHandle;
    BaseType_t result = xTaskCreatePinnedToCore(
        nfcReadTaskSRIX,
        "NFCCompareRead",
        TASK_STACK_SIZE_SMALL,
        taskData,
        TASK_PRIORITY,
        &readTaskHandle,
        TASK_CORE_ID
    );

    if (result != pdPASS) {
        LOG_ERROR("NFC-API", "Failed to create compare read task");
        delete[] loadedDataCopy;
        delete taskData;
        request->send(HTTP_INTERNAL_ERROR, "application/json",
                     "{\"success\":false,\"message\":\"Failed to create read task\"}");
        return;
    }

    // Non-blocking polling
    unsigned long start = millis();
    unsigned long max_wait = (timeout * 1000) + TIMEOUT_MARGIN_READ_MS;
    
    while (!taskData->completed && (millis() - start < max_wait)) {
        delay(POLL_INTERVAL_MS);
        esp_task_wdt_reset();
    }

    int retries = 0;
    while (taskData->running && retries < MAX_RETRIES_STANDARD) {
        delay(200);
        esp_task_wdt_reset();
        retries++;
    }

    // Build response with comparison
    JsonDocument responseDoc;
    
    if (taskData->completed && taskData->result.success) {
        // Physical tag read successfully
        NFCManager::TagInfo physicalTag = taskData->tagInfo;
        
        LOG_DEBUG("NFC-API", "Read physical tag: UID=%02X%02X%02X%02X",
                 physicalTag.uid[0], physicalTag.uid[1],
                 physicalTag.uid[2], physicalTag.uid[3]);

        responseDoc["success"] = true;
        responseDoc["message"] = "Tag read successfully";

        // Physical tag info
        responseDoc["physical_uid"] = _nfc.uidToString(physicalTag.uid, physicalTag.uid_length);
        responseDoc["physical_protocol"] = physicalTag.protocol_name;

        // Loaded dump info
        responseDoc["loaded_uid"] = _nfc.uidToString(loadedTag.uid, loadedTag.uid_length);

        // Delegate comparison to helper function
        const uint8_t* physicalData = _nfc.getTagDataPointer(physicalTag);
        size_t physicalSize = _nfc.getTagDataSize(physicalTag);
        
        compareTagData(loadedDataCopy, loadedDataSize, physicalData, physicalSize, responseDoc);
        
        LOG_INFO("NFC-API", "Compare completed - identical: %d", 
                responseDoc["identical"].as<bool>());
        _nfc.restoreCurrentTag(loadedTag);
        LOG_INFO("NFC-API", "COMPARE: Restored loaded dump to currentTag for Write Selective");
        
    } else if (taskData->completed) {
        responseDoc["success"] = false;
        responseDoc["message"] = taskData->result.message;
        LOG_WARN("NFC-API", "Compare failed: %s", taskData->result.message.c_str());
    } else {
        responseDoc["success"] = false;
        responseDoc["message"] = "Read timeout - could not read tag";
        LOG_ERROR("NFC-API", "Compare timeout");
    }

    // Cleanup
    delete[] loadedDataCopy;
    delete taskData;

    String output;
    serializeJson(responseDoc, output);
    request->send(HTTP_OK, "application/json", output);
}

void WebServerHandlerNFC::handleSRIXWriteSelective(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    LOG_INFO("NFC-API", "SRIX Write-Selective request");

    // Check if we have data to write
    if (!_nfc.hasValidData()) {
        LOG_WARN("NFC-API", "Write-Selective request but no data loaded");
        request->send(HTTP_BAD_REQUEST, "application/json",
                     "{\"success\":false,\"message\":\"No data loaded\"}");
        return;
    }

    // Parse JSON
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) {
        LOG_ERROR("NFC-API", "Invalid JSON in Write-Selective request");
        request->send(HTTP_BAD_REQUEST, "application/json",
                     "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
    }

    if (!doc["blocks"].is<JsonArray>()) {
        LOG_ERROR("NFC-API", "Missing blocks array in Write-Selective request");
        request->send(HTTP_BAD_REQUEST, "application/json",
                     "{\"success\":false,\"message\":\"Missing blocks array\"}");
        return;
    }

    // Extract block numbers
    std::vector<uint8_t> blocks_to_write;
    for (JsonVariant v : doc["blocks"].as<JsonArray>()) {
        if (v.is<int>()) {
            uint8_t block_num = v.as<uint8_t>();
            if (block_num <= SRIX_MAX_BLOCK) {
                blocks_to_write.push_back(block_num);
            } else {
                LOG_WARN("NFC-API", "Invalid block number: %d (max: %d)", 
                        block_num, SRIX_MAX_BLOCK);
            }
        }
    }

    if (blocks_to_write.empty()) {
        LOG_ERROR("NFC-API", "No valid blocks to write");
        request->send(HTTP_BAD_REQUEST, "application/json",
                     "{\"success\":false,\"message\":\"No valid blocks\"}");
        return;
    }

    LOG_INFO("NFC-API", "Write-Selective: %d blocks", blocks_to_write.size());

    // Allocate task data structure on heap
    nfcWriteSelectiveTaskSRIXData* taskData = new nfcWriteSelectiveTaskSRIXData();
    taskData->nfc = &_nfc;
    taskData->block_numbers = blocks_to_write;
    taskData->completed = false;
    taskData->running = false;

    // Create task on Core 1
    TaskHandle_t writeTaskHandle;
    BaseType_t result = xTaskCreatePinnedToCore(
        nfcWriteSelectiveTaskSRIX,
        "NFCWriteSelective",
        TASK_STACK_SIZE_LARGE,      // Larger stack for loop
        taskData,
        TASK_PRIORITY,
        &writeTaskHandle,
        TASK_CORE_ID
    );

    if (result != pdPASS) {
        LOG_ERROR("NFC-API", "Failed to create write-selective task");
        delete taskData;
        request->send(HTTP_INTERNAL_ERROR, "application/json",
                     "{\"success\":false,\"message\":\"Failed to create task\"}");
        return;
    }

    // DYNAMIC TIMEOUT
    // Estimate: ~2.6s per block (wait 2.5s + write 50ms + verify 10ms)
    unsigned long estimated_time = blocks_to_write.size() * MS_PER_BLOCK_SRIX;
    unsigned long max_wait = estimated_time + SELECTIVE_WRITE_EXTRA_MS;

    LOG_INFO("NFC-API", "Estimated time: %lums, max wait: %lums", 
            estimated_time, max_wait);

    // Non-blocking polling
    unsigned long start = millis();
    while (!taskData->completed && (millis() - start < max_wait)) {
        delay(POLL_INTERVAL_SLOW_MS);
        esp_task_wdt_reset();
    }

    // If still running, wait with longer delays
    int retries = 0;
    while (taskData->running && retries < MAX_RETRIES_VERY_LONG_OP) {
        delay(POLL_INTERVAL_VERY_SLOW_MS);
        esp_task_wdt_reset();
        retries++;
    }

    // Build response
    JsonDocument responseDoc;
    
    if (taskData->completed) {
        responseDoc["success"] = taskData->result.success;
        responseDoc["message"] = taskData->result.message;
        responseDoc["code"] = taskData->result.code;
        responseDoc["blocks_count"] = blocks_to_write.size();
        
        LOG_INFO("NFC-API", "Write-Selective completed - success: %d", 
                taskData->result.success);
    } else {
        responseDoc["success"] = false;
        responseDoc["message"] = "Write timeout - operation did not complete";
        responseDoc["code"] = -99;
        
        LOG_ERROR("NFC-API", "Write-Selective timeout after %lums", millis() - start);
    }

    // Cleanup
    delete taskData;

    String output;
    serializeJson(responseDoc, output);
    request->send(HTTP_OK, "application/json", output);
}
// ============================================
// MIFARE HANDLERS
// ============================================

void WebServerHandlerNFC::handleMifareRead(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    // Parse JSON body for timeout parameter
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    int timeout = 10; // Default timeout
    if (!error && doc["timeout"].is<int>()) {
        timeout = doc["timeout"].as<int>();
    }

    LOG_INFO("NFC-API", "Mifare Read request (timeout: %ds)", timeout);

    // Allocate task data structure
    nfcReadTaskMifareData* taskData = new nfcReadTaskMifareData();
    taskData->nfc = &_nfc;
    taskData->timeout_sec = timeout;
    taskData->uid_only = false; // Full dump
    taskData->completed = false;
    taskData->running = false;

    // Create task on Core 1
    TaskHandle_t readTaskHandle;
    BaseType_t result = xTaskCreatePinnedToCore(
        nfcReadTaskMifare,
        "MifareRead",
        TASK_STACK_SIZE_MEDIUM,     // Larger stack for Mifare (multiple authentication)
        taskData,
        TASK_PRIORITY,
        &readTaskHandle,
        TASK_CORE_ID
    );

    if (result != pdPASS) {
        LOG_ERROR("NFC-API", "Failed to create Mifare read task");
        delete taskData;
        request->send(HTTP_INTERNAL_ERROR, "application/json",
                     "{\"success\":false,\"message\":\"Failed to create task\"}");
        return;
    }

    LOG_DEBUG("NFC-API", "Mifare read task created, waiting for completion...");

    // Non-blocking polling with extra time for authentication
    unsigned long start = millis();
    unsigned long max_wait = (timeout * 1000) + TIMEOUT_MARGIN_WRITE_MS;
    
    while (!taskData->completed && (millis() - start < max_wait)) {
        delay(POLL_INTERVAL_MS);
        esp_task_wdt_reset();
    }

    int retries = 0;
    while (taskData->running && retries < MAX_RETRIES_STANDARD) {
        delay(200);
        esp_task_wdt_reset();
        retries++;
    }

    // Build response
    JsonDocument responseDoc;
    
    if (taskData->completed) {
        responseDoc["success"] = taskData->result.success;
        responseDoc["message"] = taskData->result.message;
        
        if (taskData->result.success) {
            responseDoc["protocol"] = taskData->tagInfo.protocol_name;
            responseDoc["uid"] = _nfc.uidToString(taskData->tagInfo.uid, taskData->tagInfo.uid_length);
            responseDoc["size"] = _nfc.getTagDataSize(taskData->tagInfo);
            responseDoc["sectors"] = taskData->tagInfo.data.mifare_classic.sectors;

            // Dump preview (first 64 bytes)
            const uint8_t* dumpData = _nfc.getTagDataPointer(taskData->tagInfo);
            String dumpHex = "";
            size_t dumpSize = _nfc.getTagDataSize(taskData->tagInfo);
            
            for (size_t i = 0; i < DUMP_PREVIEW_BYTES && i < dumpSize; i++) {
                char hex[3];
                sprintf(hex, "%02X", dumpData[i]);
                dumpHex += hex;
            }
            responseDoc["dump"] = dumpHex;
            
            LOG_INFO("NFC-API", "Mifare read successful - UID: %s, %d sectors", 
                    responseDoc["uid"].as<const char*>(), 
                    taskData->tagInfo.data.mifare_classic.sectors);
        } else {
            LOG_WARN("NFC-API", "Mifare read failed: %s", taskData->result.message.c_str());
        }
    } else {
        responseDoc["success"] = false;
        responseDoc["message"] = "Request timeout (task still running)";
        LOG_ERROR("NFC-API", "Mifare read timeout");
    }

    // Cleanup
    delete taskData;

    String output;
    serializeJson(responseDoc, output);
    request->send(HTTP_OK, "application/json", output);
}

void WebServerHandlerNFC::handleMifareReadUID(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    // Parse JSON body
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    int timeout = 5; // Default shorter timeout for UID-only
    if (!error && doc["timeout"].is<int>()) {
        timeout = doc["timeout"].as<int>();
    }

    LOG_INFO("NFC-API", "Mifare Read UID (timeout: %ds)", timeout);

    // Allocate task data structure
    nfcReadTaskMifareData* taskData = new nfcReadTaskMifareData();
    taskData->nfc = &_nfc;
    taskData->timeout_sec = timeout;
    taskData->uid_only = true; // UID only
    taskData->completed = false;
    taskData->running = false;

    // Create task on Core 1
    TaskHandle_t readTaskHandle;
    BaseType_t result = xTaskCreatePinnedToCore(
        nfcReadTaskMifare,
        "MifareUID",
        TASK_STACK_SIZE_SMALL,
        taskData,
        TASK_PRIORITY,
        &readTaskHandle,
        TASK_CORE_ID
    );

    if (result != pdPASS) {
        LOG_ERROR("NFC-API", "Failed to create Mifare UID read task");
        delete taskData;
        request->send(HTTP_INTERNAL_ERROR, "application/json",
                     "{\"success\":false,\"message\":\"Failed to create task\"}");
        return;
    }

    // Non-blocking polling
    unsigned long start = millis();
    unsigned long max_wait = (timeout * 1000) + TIMEOUT_MARGIN_READ_MS;
    
    while (!taskData->completed && (millis() - start < max_wait)) {
        delay(POLL_INTERVAL_MS);
        esp_task_wdt_reset();
    }

    int retries = 0;
    while (taskData->running && retries < MAX_RETRIES_STANDARD) {
        delay(200);
        esp_task_wdt_reset();
        retries++;
    }

    // Build response
    JsonDocument responseDoc;
    
    if (taskData->completed) {
        responseDoc["success"] = taskData->result.success;
        responseDoc["message"] = taskData->result.message;
        
        if (taskData->result.success) {
            responseDoc["protocol"] = taskData->tagInfo.protocol_name;
            responseDoc["uid"] = _nfc.uidToString(taskData->tagInfo.uid, taskData->tagInfo.uid_length);
            
            LOG_INFO("NFC-API", "Mifare UID read successful: %s", 
                    responseDoc["uid"].as<const char*>());
        }
    } else {
        responseDoc["success"] = false;
        responseDoc["message"] = "Request timeout";
        LOG_ERROR("NFC-API", "Mifare UID read timeout");
    }

    // Cleanup
    delete taskData;

    String output;
    serializeJson(responseDoc, output);
    request->send(HTTP_OK, "application/json", output);
}

void WebServerHandlerNFC::handleMifareWrite(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    LOG_INFO("NFC-API", "Mifare Write request");

    // Check if we have data to write
    if (!_nfc.hasValidData()) {
        LOG_WARN("NFC-API", "Write request but no data loaded");
        request->send(HTTP_BAD_REQUEST, "application/json",
                     "{\"success\":false,\"message\":\"No data to write (load/read first)\"}");
        return;
    }

    // Parse JSON for timeout
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    int timeout = 20; // Default longer timeout for Mifare write
    if (!error && doc["timeout"].is<int>()) {
        timeout = doc["timeout"].as<int>();
    }

    LOG_INFO("NFC-API", "Mifare Write - timeout: %ds", timeout);

    // Allocate task data structure
    nfcWriteTaskMifareData* taskData = new nfcWriteTaskMifareData;
    taskData->nfc = &_nfc;
    taskData->tagInfo = _nfc.getCurrentTag();
    taskData->timeout_sec = timeout;
    taskData->completed = false;
    taskData->running = false;

    // Create task on Core 1
    TaskHandle_t writeTaskHandle;
    BaseType_t result = xTaskCreatePinnedToCore(
        nfcWriteTaskMifare,
        "MifareWrite",
        TASK_STACK_SIZE_MEDIUM,
        taskData,
        TASK_PRIORITY,
        &writeTaskHandle,
        TASK_CORE_ID
    );

    if (result != pdPASS) {
        LOG_ERROR("NFC-API", "Failed to create Mifare write task");
        delete taskData;
        request->send(HTTP_INTERNAL_ERROR, "application/json",
                     "{\"success\":false,\"message\":\"Failed to create task\"}");
        return;
    }

    LOG_DEBUG("NFC-API", "Mifare write task created, waiting for completion...");

    // Polling with extra timeout for full write
    // Mifare 1K: 16 sectors × authentication + write ≈ 30+ seconds
    unsigned long start = millis();
    unsigned long max_wait = (timeout * 1000) + MIFARE_FULL_WRITE_EXTRA_MS;
    
    while (!taskData->completed && (millis() - start < max_wait)) {
        delay(POLL_INTERVAL_SLOW_MS);
        esp_task_wdt_reset();
    }

    int retries = 0;
    while (taskData->running && retries < MAX_RETRIES_LONG_OP) {
        delay(1000);
        esp_task_wdt_reset();
        retries++;
    }

    // Build response
    JsonDocument responseDoc;
    
    if (taskData->completed) {
        responseDoc["success"] = taskData->result.success;
        responseDoc["message"] = taskData->result.message;
        responseDoc["code"] = taskData->result.code;
        
        LOG_INFO("NFC-API", "Mifare write completed - success: %d, code: %d", 
                taskData->result.success, taskData->result.code);
    } else {
        responseDoc["success"] = false;
        responseDoc["message"] = "Write timeout - operation did not complete";
        responseDoc["code"] = -99;
        
        LOG_ERROR("NFC-API", "Mifare write timeout");
    }

    // NOTE: taskData deleted by task itself
    
    String output;
    serializeJson(responseDoc, output);
    request->send(HTTP_OK, "application/json", output);
}

void WebServerHandlerNFC::handleMifareClone(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    LOG_INFO("NFC-API", "Mifare Clone UID request");

    // Check if we have UID to clone
    if (!_nfc.hasValidData()) {
        LOG_WARN("NFC-API", "Clone request but no UID loaded");
        request->send(HTTP_BAD_REQUEST, "application/json",
                     "{\"success\":false,\"message\":\"No UID to clone (load/read first)\"}");
        return;
    }

    // Parse JSON for timeout
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    int timeout = 10; // Default timeout
    if (!error && doc["timeout"].is<int>()) {
        timeout = doc["timeout"].as<int>();
    }

    LOG_INFO("NFC-API", "Mifare Clone UID - timeout: %ds", timeout);

    // Allocate task data structure
    nfcCloneTaskMifareData* taskData = new nfcCloneTaskMifareData;
    taskData->nfc = &_nfc;
    taskData->tagInfo = _nfc.getCurrentTag();
    taskData->timeout_sec = timeout;
    taskData->completed = false;
    taskData->running = false;

    // Create task on Core 1
    TaskHandle_t cloneTaskHandle;
    BaseType_t result = xTaskCreatePinnedToCore(
        nfcCloneTaskMifare,
        "MifareClone",
        TASK_STACK_SIZE_SMALL,
        taskData,
        TASK_PRIORITY,
        &cloneTaskHandle,
        TASK_CORE_ID
    );

    if (result != pdPASS) {
        LOG_ERROR("NFC-API", "Failed to create Mifare clone task");
        delete taskData;
        request->send(HTTP_INTERNAL_ERROR, "application/json",
                     "{\"success\":false,\"message\":\"Failed to create task\"}");
        return;
    }

    // Polling
    unsigned long start = millis();
    unsigned long max_wait = (timeout * 1000) + TIMEOUT_MARGIN_WRITE_MS;
    
    while (!taskData->completed && (millis() - start < max_wait)) {
        delay(POLL_INTERVAL_SLOW_MS);
        esp_task_wdt_reset();
    }

    int retries = 0;
    while (taskData->running && retries < MAX_RETRIES_STANDARD) {
        delay(200);
        esp_task_wdt_reset();
        retries++;
    }

    // Build response
    JsonDocument responseDoc;
    
    if (taskData->completed) {
        responseDoc["success"] = taskData->result.success;
        responseDoc["message"] = taskData->result.message;
        responseDoc["code"] = taskData->result.code;
        
        LOG_INFO("NFC-API", "Mifare clone completed - success: %d", 
                taskData->result.success);
    } else {
        responseDoc["success"] = false;
        responseDoc["message"] = "Clone timeout";
        responseDoc["code"] = -99;
        
        LOG_ERROR("NFC-API", "Mifare clone timeout");
    }

    // NOTE: taskData deleted by task itself

    String output;
    serializeJson(responseDoc, output);
    request->send(HTTP_OK, "application/json", output);
}

void WebServerHandlerNFC::handleMifareCompare(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    LOG_INFO("NFC-API", "Mifare Compare request");

    // Check if we have a loaded dump
    if (!_nfc.hasValidData()) {
        LOG_WARN("NFC-API", "Compare request but no data loaded");
        request->send(HTTP_BAD_REQUEST, "application/json",
                     "{\"success\":false,\"message\":\"No loaded dump to compare\"}");
        return;
    }

    // Save loaded dump data before reading physical tag
    NFCManager::TagInfo loadedTag = _nfc.getCurrentTag();
    
    size_t loadedDataSize = _nfc.getTagDataSize(loadedTag);
    uint8_t* loadedDataCopy = new uint8_t[loadedDataSize];
    memcpy(loadedDataCopy, _nfc.getTagDataPointer(loadedTag), loadedDataSize);

    LOG_DEBUG("NFC-API", "Saved loaded Mifare dump: size=%d, UID=%02X%02X%02X%02X",
             loadedDataSize, loadedTag.uid[0], loadedTag.uid[1],
             loadedTag.uid[2], loadedTag.uid[3]);

    // Parse JSON for timeout
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    
    int timeout = 10;
    if (!error && doc["timeout"].is<int>()) {
        timeout = doc["timeout"].as<int>();
    }

    LOG_INFO("NFC-API", "Mifare Compare - reading physical tag with timeout=%ds", timeout);

    // Allocate task data for read
    nfcReadTaskMifareData* taskData = new nfcReadTaskMifareData();
    taskData->nfc = &_nfc;
    taskData->timeout_sec = timeout;
    taskData->uid_only = false; // Full dump for comparison
    taskData->completed = false;
    taskData->running = false;

    // Create read task
    TaskHandle_t readTaskHandle;
    BaseType_t result = xTaskCreatePinnedToCore(
        nfcReadTaskMifare,
        "MifareCompareRead",
        TASK_STACK_SIZE_MEDIUM,
        taskData,
        TASK_PRIORITY,
        &readTaskHandle,
        TASK_CORE_ID
    );

    if (result != pdPASS) {
        LOG_ERROR("NFC-API", "Failed to create compare read task");
        delete[] loadedDataCopy;
        delete taskData;
        request->send(HTTP_INTERNAL_ERROR, "application/json",
                     "{\"success\":false,\"message\":\"Failed to create read task\"}");
        return;
    }

    // Non-blocking polling
    unsigned long start = millis();
    unsigned long max_wait = (timeout * 1000) + TIMEOUT_MARGIN_WRITE_MS;
    
    while (!taskData->completed && (millis() - start < max_wait)) {
        delay(POLL_INTERVAL_MS);
        esp_task_wdt_reset();
    }

    int retries = 0;
    while (taskData->running && retries < MAX_RETRIES_STANDARD) {
        delay(200);
        esp_task_wdt_reset();
        retries++;
    }

    // Build response with comparison
    JsonDocument responseDoc;
    
    if (taskData->completed && taskData->result.success) {
        NFCManager::TagInfo physicalTag = taskData->tagInfo;
        
        LOG_DEBUG("NFC-API", "Read physical Mifare tag: UID=%02X%02X%02X%02X",
                 physicalTag.uid[0], physicalTag.uid[1],
                 physicalTag.uid[2], physicalTag.uid[3]);

        responseDoc["success"] = true;
        responseDoc["message"] = "Tag read successfully";

        // Physical tag info
        responseDoc["physical_uid"] = _nfc.uidToString(physicalTag.uid, physicalTag.uid_length);
        responseDoc["physical_protocol"] = physicalTag.protocol_name;

        // Loaded dump info
        responseDoc["loaded_uid"] = _nfc.uidToString(loadedTag.uid, loadedTag.uid_length);

        // Perform comparison
        const uint8_t* physicalData = _nfc.getTagDataPointer(physicalTag);
        size_t physicalSize = _nfc.getTagDataSize(physicalTag);
        
        compareTagData(loadedDataCopy, loadedDataSize, physicalData, physicalSize, responseDoc);
        
        LOG_INFO("NFC-API", "Mifare compare completed - identical: %d", 
                responseDoc["identical"].as<bool>());
                _nfc.restoreCurrentTag(loadedTag);
        LOG_INFO("NFC-API", "COMPARE: Restored loaded dump to currentTag for Write Selective");

    } else if (taskData->completed) {
        responseDoc["success"] = false;
        responseDoc["message"] = taskData->result.message;
        LOG_WARN("NFC-API", "Mifare compare failed: %s", taskData->result.message.c_str());
    } else {
        responseDoc["success"] = false;
        responseDoc["message"] = "Read timeout - could not read tag";
        LOG_ERROR("NFC-API", "Mifare compare timeout");
    }

    // Cleanup
    delete[] loadedDataCopy;
    delete taskData;

    String output;
    serializeJson(responseDoc, output);
    request->send(HTTP_OK, "application/json", output);
}

void WebServerHandlerNFC::handleMifareWriteSelective(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    LOG_INFO("NFC-API", "Mifare Write-Selective request");

    // Check if we have data to write
    if (!_nfc.hasValidData()) {
        LOG_WARN("NFC-API", "Write-Selective request but no data loaded");
        request->send(HTTP_BAD_REQUEST, "application/json",
                     "{\"success\":false,\"message\":\"No data loaded\"}");
        return;
    }

    // Parse JSON
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) {
        LOG_ERROR("NFC-API", "Invalid JSON in Mifare Write-Selective request");
        request->send(HTTP_BAD_REQUEST, "application/json",
                     "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
    }

    if (!doc["blocks"].is<JsonArray>()) {
        LOG_ERROR("NFC-API", "Missing blocks array in Mifare Write-Selective request");
        request->send(HTTP_BAD_REQUEST, "application/json",
                     "{\"success\":false,\"message\":\"Missing blocks array\"}");
        return;
    }

    // Extract block numbers
    std::vector<uint8_t> blocks_to_write;
    for (JsonVariant v : doc["blocks"].as<JsonArray>()) {
        if (v.is<int>()) {
            uint8_t block_num = v.as<uint8_t>();
            blocks_to_write.push_back(block_num);
        }
    }

    if (blocks_to_write.empty()) {
        LOG_ERROR("NFC-API", "No valid blocks to write");
        request->send(HTTP_BAD_REQUEST, "application/json",
                     "{\"success\":false,\"message\":\"No valid blocks\"}");
        return;
    }

    LOG_INFO("NFC-API", "Mifare Write-Selective: %d blocks", blocks_to_write.size());

    // Allocate task data structure
    nfcWriteSelectiveTaskMifareData* taskData = new nfcWriteSelectiveTaskMifareData();
    taskData->nfc = &_nfc;
    taskData->block_numbers = blocks_to_write;
    taskData->completed = false;
    taskData->running = false;

    // Create task on Core 1
    TaskHandle_t writeTaskHandle;
    BaseType_t result = xTaskCreatePinnedToCore(
        nfcWriteSelectiveTaskMifare,
        "MifareWriteSelective",
        TASK_STACK_SIZE_LARGE,
        taskData,
        TASK_PRIORITY,
        &writeTaskHandle,
        TASK_CORE_ID
    );

    if (result != pdPASS) {
        LOG_ERROR("NFC-API", "Failed to create Mifare write-selective task");
        delete taskData;
        request->send(HTTP_INTERNAL_ERROR, "application/json",
                     "{\"success\":false,\"message\":\"Failed to create task\"}");
        return;
    }

    // Dynamic timeout based on block count
    unsigned long estimated_time = blocks_to_write.size() * 2000; // ~2s per block
    unsigned long max_wait = estimated_time + SELECTIVE_WRITE_EXTRA_MS;

    LOG_INFO("NFC-API", "Estimated time: %lums, max wait: %lums", 
            estimated_time, max_wait);

    // Non-blocking polling
    unsigned long start = millis();
    while (!taskData->completed && (millis() - start < max_wait)) {
        delay(POLL_INTERVAL_SLOW_MS);
        esp_task_wdt_reset();
    }

    int retries = 0;
    while (taskData->running && retries < MAX_RETRIES_VERY_LONG_OP) {
        delay(POLL_INTERVAL_VERY_SLOW_MS);
        esp_task_wdt_reset();
        retries++;
    }

    // Build response
    JsonDocument responseDoc;
    
    if (taskData->completed) {
        responseDoc["success"] = taskData->result.success;
        responseDoc["message"] = taskData->result.message;
        responseDoc["code"] = taskData->result.code;
        responseDoc["blocks_count"] = blocks_to_write.size();
        
        LOG_INFO("NFC-API", "Mifare Write-Selective completed - success: %d", 
                taskData->result.success);
    } else {
        responseDoc["success"] = false;
        responseDoc["message"] = "Write timeout - operation did not complete";
        responseDoc["code"] = -99;
        
        LOG_ERROR("NFC-API", "Mifare Write-Selective timeout");
    }

    // Cleanup
    delete taskData;

    String output;
    serializeJson(responseDoc, output);
    request->send(HTTP_OK, "application/json", output);
}

// ============================================
// UNIFIED API HANDLERS (PROTOCOL-AGNOSTIC)
// ============================================

void WebServerHandlerNFC::handleSave(AsyncWebServerRequest* request) {
    // Get filename from POST body
    if (!request->hasParam("filename", true)) {  // true = POST body
        LOG_WARN("NFC-API", "Save request missing filename");
        request->send(HTTP_BAD_REQUEST, "application/json",
                     "{\"success\":false,\"message\":\"Missing filename parameter\"}");
        return;
    }

    String filename = request->getParam("filename", true)->value();  // true = POST body
    LOG_INFO("NFC-API", "Save request - filename: %s", filename.c_str());

    // Delegate to NFCManager
    NFCManager::Result result = _nfc.save(filename);

    JsonDocument doc;
    doc["success"] = result.success;
    doc["message"] = result.message;

    if (result.success) {
        LOG_INFO("NFC-API", "Save successful: %s", filename.c_str());
    } else {
        LOG_ERROR("NFC-API", "Save failed: %s", result.message.c_str());
    }

    String output;
    serializeJson(doc, output);
    request->send(HTTP_OK, "application/json", output);
}

void WebServerHandlerNFC::handleLoad(AsyncWebServerRequest* request) {
    // Get filename from query parameter
    if (!request->hasParam("filename", true)) {
        LOG_WARN("NFC-API", "Load request missing filename");
        request->send(HTTP_BAD_REQUEST, "application/json", 
                    "{\"success\":false,\"message\":\"Missing filename parameter\"}");
        return;
    }

    String filename = request->getParam("filename", true)->value();
    LOG_INFO("NFC-API", "Load request - filename: %s", filename.c_str());

    // Deduce protocol from file extension
    NFCManager::Protocol protocol = NFCManager::PROTOCOL_UNKNOWN;
    if (filename.endsWith(".srix")) {
        protocol = NFCManager::PROTOCOL_SRIX;
    } else if (filename.endsWith(".mfc")) {
        protocol = NFCManager::PROTOCOL_MIFARE_CLASSIC;
    }

    // Delegate to NFCManager (will auto-detect if protocol is UNKNOWN)
    NFCManager::Result result = _nfc.load(filename, protocol);

    JsonDocument doc;
    doc["success"] = result.success;
    doc["message"] = result.message;

    if (result.success) {
        // Get loaded tag info and add to response
        NFCManager::TagInfo tagInfo = _nfc.getCurrentTag();
        doc["protocol"] = tagInfo.protocol_name;
        doc["uid"] = _nfc.uidToString(tagInfo.uid, tagInfo.uid_length);
        doc["size"] = _nfc.getTagDataSize(tagInfo);
        
        LOG_INFO("NFC-API", "Load successful: %s (Protocol: %s, UID: %s)", 
                 filename.c_str(), 
                 tagInfo.protocol_name, 
                 _nfc.uidToString(tagInfo.uid, tagInfo.uid_length).c_str());
    } else {
        LOG_ERROR("NFC-API", "Load failed: %s", result.message.c_str());
    }

    String output;
    serializeJson(doc, output);
    request->send(HTTP_OK, "application/json", output);
}

void WebServerHandlerNFC::handleList(AsyncWebServerRequest *request) {
    LOG_DEBUG("NFC-API", "List files request");
    
    // Get protocol parameter
    String protocolParam = "srix"; // default
    if (request->hasParam("protocol")) {
        protocolParam = request->getParam("protocol")->value();
    }
    
    // Map protocol string to enum
    NFCManager::Protocol protocol = NFCManager::PROTOCOL_SRIX;
    if (protocolParam == "mifare") {
        protocol = NFCManager::PROTOCOL_MIFARE_CLASSIC;
    } else if (protocolParam == "auto") {
        // Auto: list both SRIX and Mifare files
        protocol = NFCManager::PROTOCOL_UNKNOWN; // Special case
    }
    
    LOG_DEBUG("NFC-API", "List request for protocol: %s", protocolParam.c_str());
    
    JsonDocument doc;
    JsonArray filesArray = doc["files"].to<JsonArray>();
    
    // Helper lambda to scan directory
    auto scanDirectory = [&](const String& path, const String& ext) {
        if (!LittleFS.exists(path)) {
            LOG_WARN("NFC-API", "Folder not found: %s", path.c_str());
            return;
        }
        
        File dir = LittleFS.open(path);
        if (!dir || !dir.isDirectory()) {
            LOG_WARN("NFC-API", "Invalid directory: %s", path.c_str());
            return;
        }
        
        LOG_DEBUG("NFC-API", "Scanning directory: %s", path.c_str());
        
        File file = dir.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                String filename = String(file.name());
                
                // Filter by extension
                if (filename.endsWith(ext)) {
                    JsonObject fileObj = filesArray.add<JsonObject>();
                    
                    // Extract name without extension
                    String nameOnly = filename;
                    int lastSlash = nameOnly.lastIndexOf('/');
                    if (lastSlash >= 0) {
                        nameOnly = nameOnly.substring(lastSlash + 1);
                    }
                    nameOnly.replace(ext, "");
                    
                    fileObj["name"] = nameOnly;
                    fileObj["ext"] = ext;
                    fileObj["fullname"] = filename;
                    
                    LOG_DEBUG("NFC-API", "Found file: %s", filename.c_str());
                }
            }
            file = dir.openNextFile();
        }
        dir.close();
    };
    
    // Scan directories based on protocol
    if (protocol == NFCManager::PROTOCOL_SRIX) {
        scanDirectory(NFC_SRIX_DUMP_FOLDER, ".srix");
    } else if (protocol == NFCManager::PROTOCOL_MIFARE_CLASSIC) {
        scanDirectory(NFC_MIFARE_DUMP_FOLDER, ".mfc");
    } else {
        // Auto: scan both
        scanDirectory(NFC_SRIX_DUMP_FOLDER, ".srix");
        scanDirectory(NFC_MIFARE_DUMP_FOLDER, ".mfc");
    }
    
    doc["success"] = true;
    doc["message"] = filesArray.size() > 0 ? 
        String("Found ") + String(filesArray.size()) + " files" : 
        "No files found";
    
    LOG_INFO("NFC-API", "Listed %d files", filesArray.size());
    
    String output;
    serializeJson(doc, output);
    request->send(HTTP_OK, "application/json", output);
}

void WebServerHandlerNFC::handleDelete(AsyncWebServerRequest* request) {
    // Get filename from query parameter (DELETE use query string, not body)
    if (!request->hasParam("filename", false)) {  // false = query string (per DELETE)
        LOG_WARN("NFC-API", "Delete request missing filename");
        request->send(HTTP_BAD_REQUEST, "application/json",
                     "{\"success\":false,\"message\":\"Missing filename parameter\"}");
        return;
    }

    String filename = request->getParam("filename", false)->value();  // false = query string
    LOG_INFO("NFC-API", "Delete request - filename: %s", filename.c_str());

    // Deduce protocol from extension
    NFCManager::Protocol protocol = NFCManager::PROTOCOL_UNKNOWN;
    if (filename.endsWith(".srix")) {
        protocol = NFCManager::PROTOCOL_SRIX;
    } else if (filename.endsWith(".mfc")) {
        protocol = NFCManager::PROTOCOL_MIFARE_CLASSIC;
    } else {
        LOG_ERROR("NFC-API", "Invalid file extension: %s", filename.c_str());
        request->send(HTTP_BAD_REQUEST, "application/json",
                     "{\"success\":false,\"message\":\"Invalid file extension\"}");
        return;
    }

    // Pass complete filename (with extension) to NFCManager
    NFCManager::Result result = _nfc.deleteFile(filename, protocol);

    JsonDocument doc;
    doc["success"] = result.success;
    doc["message"] = result.message;

    if (result.success) {
        LOG_INFO("NFC-API", "Delete successful: %s", filename.c_str());
    } else {
        LOG_ERROR("NFC-API", "Delete failed: %s", result.message.c_str());
    }

    String output;
    serializeJson(doc, output);
    request->send(HTTP_OK, "application/json", output);
}

void WebServerHandlerNFC::handleStatus(AsyncWebServerRequest* request) {
    LOG_DEBUG("NFC-API", "Status request");

    JsonDocument doc;
    doc["ready"] = _nfc.isReady();
    doc["srix_hw"] = _nfc.isSRIXReady();
    doc["has_data"] = _nfc.hasValidData();

    if (_nfc.hasValidData()) {
        NFCManager::TagInfo tag = _nfc.getCurrentTag();
        doc["protocol"] = _nfc.protocolToString(_nfc.getCurrentProtocol());
        doc["uid"] = _nfc.uidToString(tag.uid, tag.uid_length);
    }

    String output;
    serializeJson(doc, output);
    request->send(HTTP_OK, "application/json", output);
}

// ============================================
// STATIC FILE HANDLERS
// ============================================

void WebServerHandlerNFC::handleNFCTabHTML(AsyncWebServerRequest* request) {
    LOG_DEBUG("NFC-WEB", "Serving nfc-tab.html (gzipped)");
    
    // Serve from PROGMEM (compiled into flash)
    auto res = request->beginResponse(HTTP_OK, "text/html", nfc_tab_web, nfc_tab_web_size);
    res->addHeader("Content-Encoding", "gzip");
    request->send(res);
}

void WebServerHandlerNFC::handleNFCAppJS(AsyncWebServerRequest* request) {
    LOG_DEBUG("NFC-WEB", "Serving nfc-app.js (gzipped)");
    
    // Serve from PROGMEM
    auto res = request->beginResponse(HTTP_OK, "application/javascript", nfc_app_web, nfc_app_web_size);
    res->addHeader("Content-Encoding", "gzip");
    request->send(res);
}

// ============================================
// HELPER FUNCTIONS
// ============================================

void WebServerHandlerNFC::compareTagData(
    const uint8_t* loadedData,
    size_t loadedSize,
    const uint8_t* physicalData,
    size_t physicalSize,
    JsonDocument& responseDoc)
{
    LOG_DEBUG("NFC-API", "Comparing tag data: loaded=%d bytes, physical=%d bytes", 
             loadedSize, physicalSize);

    // Check size mismatch
    if (loadedSize != physicalSize) {
        responseDoc["success"] = false;
        responseDoc["message"] = "Size mismatch between loaded dump and physical tag";
        responseDoc["identical"] = false;
        responseDoc["size_mismatch"] = true;
        responseDoc["loaded_size"] = loadedSize;
        responseDoc["physical_size"] = physicalSize;
        
        LOG_WARN("NFC-API", "Size mismatch: loaded=%d, physical=%d", 
                loadedSize, physicalSize);
        return;
    }

    // Auto-detect protocol by size
    int BLOCK_SIZE;
    bool is_mifare = false;
    
    if (loadedSize == 512) {
        // SRIX4K: 128 blocks × 4 bytes = 512 bytes
        BLOCK_SIZE = 4;
        LOG_DEBUG("NFC-API", "Detected protocol: SRIX4K");
    } else if (loadedSize == 1024) {
        // Mifare 1K: 64 blocks × 16 bytes = 1024 bytes
        BLOCK_SIZE = 16;
        is_mifare = true;
        LOG_DEBUG("NFC-API", "Detected protocol: Mifare Classic 1K");
    } else {
        responseDoc["success"] = false;
        responseDoc["message"] = "Unsupported dump size";
        responseDoc["identical"] = false;
        LOG_ERROR("NFC-API", "Unsupported dump size: %d bytes", loadedSize);
        return;
    }

    // Array of differences
    JsonArray differences = responseDoc["differences"].to<JsonArray>();
    int totalDifferences = 0;
    int numBlocks = loadedSize / BLOCK_SIZE;

    LOG_DEBUG("NFC-API", "Comparing %d blocks (block_size=%d bytes)", 
             numBlocks, BLOCK_SIZE);

    for (int block = 0; block < numBlocks; block++) {
        bool isDifferent = false;
        
        // Mifare: Check if sector trailer (will be skipped during write)
        bool is_sector_trailer = false;
        if (is_mifare) {
            // Mifare: sectors 0-31 (4 blocks), sectors 32+ (16 blocks)
            int sector = (block < 128) ? (block / 4) : (32 + (block - 128) / 16);
            int first_block = (sector < 32) ? (sector * 4) : (128 + (sector - 32) * 16);
            int block_count = (sector < 32) ? 4 : 16;
            is_sector_trailer = (block == first_block + block_count - 1);
        }
        
        // Check if block is different
        for (int b = 0; b < BLOCK_SIZE; b++) {
            int offset = block * BLOCK_SIZE + b;
            if (loadedData[offset] != physicalData[offset]) {
                isDifferent = true;
                break;
            }
        }

        if (isDifferent) {
            JsonObject diff = differences.add<JsonObject>();
            diff["block"] = block;
            
            // Loaded data (hex with spaces)
            String loadedHex = "";
            for (int b = 0; b < BLOCK_SIZE; b++) {
                char hex[3];
                sprintf(hex, "%02X", loadedData[block * BLOCK_SIZE + b]);
                loadedHex += hex;
                if (b < BLOCK_SIZE - 1) loadedHex += " "; // Space between bytes
            }
            diff["loaded"] = loadedHex;
            
            // Physical data (hex with spaces)
            String physicalHex = "";
            for (int b = 0; b < BLOCK_SIZE; b++) {
                char hex[3];
                sprintf(hex, "%02X", physicalData[block * BLOCK_SIZE + b]);
                physicalHex += hex;
                if (b < BLOCK_SIZE - 1) physicalHex += " "; // Space between bytes
            }
            diff["physical"] = physicalHex;
            
            // Add warning for Mifare sector trailer
            if (is_mifare) {
                if (block == 0) {
                    diff["warning"] = "Block 0 (UID) - will be skipped";
                } else if (is_sector_trailer) {
                    diff["warning"] = "Sector trailer (keys) - will be skipped";
                }
            }
            
            totalDifferences++;
            
            LOG_DEBUG("NFC-API", "Block %d different: %s -> %s%s", 
                     block, 
                     physicalHex.c_str(), 
                     loadedHex.c_str(),
                     is_sector_trailer ? " [SECTOR TRAILER]" : "");
        }
    }

    responseDoc["identical"] = (totalDifferences == 0);
    responseDoc["total_differences"] = totalDifferences;
    responseDoc["total_blocks"] = numBlocks;
    responseDoc["block_size"] = BLOCK_SIZE;
    
    if (totalDifferences == 0) {
        LOG_INFO("NFC-API", "Tags are identical (%d blocks)", numBlocks);
    } else {
        LOG_INFO("NFC-API", "Tags differ: %d/%d blocks different", 
                totalDifferences, numBlocks);
    }
}
