// ========================================
// ESP32 NFC Tool - Main Entry Point
// ========================================
// Description: Initialize all system modules and start main loop
// Author: Senape3000
// Version: 1.0
// Date: 2026-01-16
// ========================================

#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "webFiles.h"                                   // Web pages (gzipped)
#include "config.h"                                     // Project configuration
#include "logger.h"                                     // Structured logging system

// Modules
#include "modules/wifi/wifi_manager.h"
#include "modules/led/led_manager.h"
#include "modules/webserver/webserver_handler.h"
#include "modules/serial_commands/serial_commander.h"
#include "modules/bluetooth/bluetooth_manager.h"
#include "modules/rfid/nfc_manager.h"
#include "modules/rfid/mifare_keys_manager.h"

// ========================================
// GLOBAL OBJECTS
// ========================================

LedManager ledMgr;
WiFiManager wifiMgr;
NFCManager nfcMgr;
MifareKeysManager mfkMgr;
AsyncWebServer server(WEB_SERVER_PORT);
WebServerHandler webHandler(server, wifiMgr, nfcMgr);
SerialCommander commander(wifiMgr, nfcMgr);
BluetoothManager btMgr(wifiMgr, nfcMgr, ledMgr);

// ========================================
// TASK HANDLES
// ========================================

TaskHandle_t serialTaskHandle = NULL;
TaskHandle_t bluetoothTaskHandle = NULL;

// ========================================
// TASK: SERIAL COMMANDER (Core 1)
// ========================================
/**
 * @brief Serial command handler task
 * @param parameter Unused task parameter
 * 
 * Runs on Core 1 to avoid blocking Core 0 (WiFi/WebServer).
 * Handles serial commands from USB with 50ms polling interval.
 */
void serialCommandTask(void* parameter) {
    LOG_INFO("TASK", "Serial Commander started on Core %d", xPortGetCoreID());

    for(;;) {
        commander.handleCommands();
        vTaskDelay(pdMS_TO_TICKS(50));  // 50ms polling interval
    }

    // Should never reach here
    vTaskDelete(NULL);
}

// ========================================
// TASK: BLUETOOTH MANAGER (Core 1)
// ========================================
/**
 * @brief Bluetooth command handler task
 * @param parameter Unused task parameter
 * 
 * Runs on Core 1 to avoid blocking Core 0 (WiFi/WebServer).
 * Handles Bluetooth LE commands from Bruce device with 50ms polling interval.
 * Provides secure data transfers and NFC operation control.
 */
void bluetoothCommandTask(void* parameter) {
    LOG_INFO("TASK", "Bluetooth Commander started on Core %d", xPortGetCoreID());

    for(;;) {
        btMgr.handleCommands();
        vTaskDelay(pdMS_TO_TICKS(BT_TASK_INTERVAL_MS));  // 50ms polling interval
    }

    // Should never reach here
    vTaskDelete(NULL);
}


// ========================================
// SETUP - ONE-TIME INITIALIZATION
// ========================================

void setup() {
    // ====== SERIAL INITIALIZATION ======
    Serial.begin(SERIAL_BAUD);
    delay(BOOT_DELAY_MS
);  // Short delay for serial stability

    // Initialize logger
    Logger::begin();

    // Quick diagnostics: print runtime configuration
    Serial.printf("[DIAG] LOG_COLORS_ENABLED = %d, LOG_TIMESTAMP_ENABLED = %d\n",
                  (int)LOG_COLORS_ENABLED, (int)LOG_TIMESTAMP_ENABLED);

    // Emit self-test messages at all levels (bypasses macro filtering)
    Logger::selfTest();

    LOG_INFO("LOGGER", "Level: %s", Logger::getLevelName((LogLevel)MY_ESP_LOG_LEVEL));
    LOG_INFO("SETUP", "ESP32 NFC Tool v1.0 - Build %s %s", __DATE__, __TIME__);
    LOG_INFO("SETUP", "Chip: %s, CPU Freq: %d MHz", ESP.getChipModel(), ESP.getCpuFreqMHz());
    LOG_INFO("SETUP", "Free Heap: %d bytes", ESP.getFreeHeap());

    // ====== LED MANAGER ======
    ledMgr.begin(LED_PIN);
    ledMgr.blinking();
    LOG_DEBUG("SETUP", "LED Manager initialized (Pin: %d)", LED_PIN);

    // ====== FILESYSTEM (LittleFS) ======
    LOG_INFO("FLASH", "Mounting filesystem...");
    LOG_DEBUG("FLASH", "Auto-format on fail: %s", 
              LITTLEFS_FORMAT_ON_FAIL ? "enabled" : "disabled");
    
    if(!LittleFS.begin(LITTLEFS_FORMAT_ON_FAIL, "/littlefs", LITTLEFS_MAX_OPEN_FILES, "littlefs")) {
        LOG_ERROR("FLASH", "Mount failed with current settings");
        
        // If auto-format was disabled, offer manual recovery
        if (!LITTLEFS_FORMAT_ON_FAIL) {
            LOG_WARN("FLASH", "Attempting manual format as recovery...");
            
            if (LittleFS.format()) {
                LOG_INFO("FLASH", "Format successful, remounting...");
                
                if (LittleFS.begin(true)) {
                    LOG_INFO("FLASH", "Filesystem mounted after recovery");
                } else {
                    LOG_ERROR("FLASH", "Mount failed after format - hardware issue?");
                    LOG_ERROR("FLASH", "Continuing in degraded mode (RAM-only)");
                }
            } else {
                LOG_ERROR("FLASH", "Format failed - filesystem unavailable");
                LOG_ERROR("FLASH", "Continuing in degraded mode (RAM-only)");
            }
        } else {
            // Auto-format was enabled but failed
            LOG_ERROR("FLASH", "Critical: Auto-format failed");
            LOG_ERROR("FLASH", "Possible hardware issue or corrupted flash");
            LOG_ERROR("FLASH", "Continuing in degraded mode (RAM-only)");
        }
        
    } else {
        // Mount successful
        LOG_INFO("FLASH", "Filesystem mounted successfully");
        
        size_t totalBytes = LittleFS.totalBytes();
        size_t usedBytes = LittleFS.usedBytes();
        float usagePercent = totalBytes > 0 ? 
                            (float)usedBytes / totalBytes * 100.0f : 0.0f;
        
        LOG_INFO("FLASH", "Used: %d / %d bytes (%.1f%%)", 
                 usedBytes, 
                 totalBytes,
                 usagePercent);
        
        // Warning thresholds
        if (usagePercent > 90.0f) {
            LOG_WARN("FLASH", "Filesystem critical: %.1f%% full - cleanup recommended", 
                     usagePercent);
        } else if (usagePercent > 75.0f) {
            LOG_WARN("FLASH", "Filesystem high usage: %.1f%% full", usagePercent);
        }
    }
    
    delay(BOOT_DELAY_MS
);


    // ====== WIFI INITIALIZATION ======
    LOG_INFO("WIFI", "Initializing WiFi Manager...");
    LOG_DEBUG("WIFI", "Note: Serial Commander disabled during WiFi setup");

    wifiMgr.begin();

    if (wifiMgr.autoConnect()) {
        ledMgr.connected();

        LOG_INFO("WIFI", "Connected to: %s", WiFi.SSID().c_str());
        LOG_INFO("WIFI", "IP Address: %s", WiFi.localIP().toString().c_str());
        LOG_INFO("WIFI", "RSSI: %d dBm", WiFi.RSSI());
        LOG_INFO("WIFI", "Gateway: %s", WiFi.gatewayIP().toString().c_str());

        delay(BOOT_DELAY_MS
);
    } else {
        ledMgr.blinking();
        LOG_WARN("WIFI", "Connection failed - operating in offline mode");
    }

    // ====== BLUETOOTH INITIALIZATION ======
    LOG_INFO("BT", "Initializing Bluetooth Manager...");
    
    #if BT_ENABLED
    if (btMgr.begin()) {
        LOG_INFO("BT", "Bluetooth Manager initialized successfully");
        LOG_INFO("BT", "Device name: %s", BT_DEVICE_NAME);
        LOG_INFO("BT", "Security level: %d", BT_ENABLE_CHECKSUM ? 1 : 0);
    } else {
        LOG_ERROR("BT", "Bluetooth Manager initialization failed");
        LOG_WARN("BT", "Bluetooth operations will be unavailable");
    }
    #else
    LOG_INFO("BT", "Bluetooth disabled in configuration");
    #endif

    delay(BOOT_DELAY_MS);

    // ====== SERIAL COMMANDER TASK ======
    LOG_INFO("TASK", "Creating Serial Commander task...");

    BaseType_t taskResult = xTaskCreatePinnedToCore(
        serialCommandTask,           // Task function
        "SerialCmd",                 // Task name (for debugging)
        SERIAL_TASK_STACK_SIZE,      // Stack size: 4KB (reduced from 12KB)
        NULL,                        // Task parameters
        SERIAL_TASK_PRIORITY,        // Priority (1 = low)
        &serialTaskHandle,           // Task handle
        SERIAL_TASK_CORE             // Core ID (1 = secondary core)
    );

    if (taskResult == pdPASS) {
        LOG_INFO("TASK", "Serial Commander task created on Core 1");
        LOG_DEBUG("TASK", "Stack size: 4096 bytes, Priority: 1");
    } else {
        LOG_ERROR("TASK", "Failed to create Serial Commander task!");
    }

    #if BT_ENABLED && BT_TASK_ENABLED
    // ====== BLUETOOTH COMMANDER TASK ======
    LOG_INFO("TASK", "Creating Bluetooth Commander task...");

    taskResult = xTaskCreatePinnedToCore(
        bluetoothCommandTask,       // Task function
        "BluetoothCmd",              // Task name (for debugging)
        BT_TASK_STACK_SIZE,          // Stack size: 8KB
        NULL,                        // Task parameters
        BT_TASK_PRIORITY,            // Priority (2 = higher than serial)
        &bluetoothTaskHandle,       // Task handle
        BT_TASK_CORE                 // Core ID (1 = same as serial)
    );

    if (taskResult == pdPASS) {
        LOG_INFO("TASK", "Bluetooth Commander task created on Core 1");
        LOG_DEBUG("TASK", "Stack size: %d bytes, Priority: %d", BT_TASK_STACK_SIZE, BT_TASK_PRIORITY);
    } else {
        LOG_ERROR("TASK", "Failed to create Bluetooth Commander task!");
    }
    #endif

    // ====== WEB SERVER ======
    LOG_INFO("WEB", "Starting web server...");
    webHandler.begin();
    LOG_INFO("WEB", "Web server started on port %d", WEB_SERVER_PORT);

    #if DEBUG_SKIP_AUTH
        LOG_WARN("WEB", "⚠️   DEBUG MODE: Authentication disabled!   ⚠️");
    #endif

    delay(BOOT_DELAY_MS
);

    // ====== I2C BUS INITIALIZATION ======
    LOG_INFO("I2C", "Initializing I2C bus...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_FREQUENCY);  // 100kHz for PN532 stability
    LOG_INFO("I2C", "I2C initialized (SDA: %d, SCL: %d, Freq: %dkHz)", 
             I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQUENCY);
    delay(BOOT_DELAY_MS
);

    // ====== MIFARE KEYS MANAGER ======
    LOG_INFO("KEYS", "Loading Mifare Classic keys...");

    if (!MifareKeysManager::begin()) {
        LOG_WARN("KEYS", "Failed to load Mifare keys database");
        LOG_WARN("KEYS", "Using default keys only (FFFFFFFFFFFF, A0A1A2A3A4A5)");
    } else {
        LOG_INFO("KEYS", "Mifare keys loaded successfully");
    }

    delay(BOOT_DELAY_MS
);

    // ====== NFC MANAGER (PN532) ======
    LOG_INFO("NFC", "Initializing PN532 NFC module...");

    if (!nfcMgr.begin()) {
        LOG_ERROR("NFC", "PN532 initialization failed");
        LOG_ERROR("NFC", "Check wiring: SDA=%d, SCL=%d, IRQ=%d, RST=%d", 
                  I2C_SDA_PIN, I2C_SCL_PIN, PN532_IRQ, PN532_RF_REST);
        LOG_WARN("NFC", "NFC operations will be unavailable");
    } else {
        LOG_INFO("NFC", "PN532 initialized successfully");
        LOG_DEBUG("NFC", "Firmware version checked");
    }

    // ====== SETUP COMPLETE ======
    LOG_INFO("SETUP", "============================================");
    LOG_INFO("SETUP", "✅ System initialization complete");
    LOG_INFO("SETUP", "============================================");
    LOG_INFO("SETUP", "");
    LOG_INFO("SETUP", "Ready to accept commands:");
    if (WiFi.status() == WL_CONNECTED) {
        LOG_INFO("SETUP", "  - Web Interface: http://%s", WiFi.localIP().toString().c_str());
    }
    LOG_INFO("SETUP", "  - Serial Commands: Type 'help' for command list");
    #if BT_ENABLED
    LOG_INFO("SETUP", "  - Bluetooth: %s (ready for Bruce device)", BT_DEVICE_NAME);
    #endif
    LOG_INFO("SETUP", "");
    Serial.flush();
}


// ========================================
// MAIN LOOP - CONTINUOUS EXECUTION
// ========================================

void loop() {

    vTaskDelay(pdMS_TO_TICKS(100));  // Yield to scheduler

}