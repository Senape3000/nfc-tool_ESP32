#include "wifi_manager.h"

WiFiManager::WiFiManager() {}

bool WiFiManager::begin() {
    WiFi.mode(WIFI_STA);
    LOG_DEBUG("WIFI", "WiFi mode set to STATION");
    return true;
}

// ============================================
// JSON CREDENTIAL DATABASE
// ============================================

bool WiFiManager::loadAllCreds(std::vector<Cred> &out) {
    out.clear();
    
    if (!LittleFS.exists(WIFI_DB_PATH)) {
        LOG_DEBUG("WIFI", "No credential database found");
        return false;
    }

    File file = LittleFS.open(WIFI_DB_PATH, FILE_READ);
    if (!file) {
        LOG_ERROR("WIFI", "Failed to open credential file");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        LOG_ERROR("WIFI", "JSON parse error: %s", error.c_str());
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
        Cred c;
        c.ssid = obj["ssid"].as<String>();
        c.pass = obj["pass"].as<String>();
        out.push_back(c);
    }

    LOG_INFO("WIFI", "Loaded %d credential(s) from database", out.size());
    return !out.empty();
}

bool WiFiManager::saveAllCreds(const std::vector<Cred> &in) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    
    for (const auto &c : in) {
        JsonObject obj = arr.add<JsonObject>();
        obj["ssid"] = c.ssid;
        obj["pass"] = c.pass;
    }

    File file = LittleFS.open(WIFI_DB_PATH, FILE_WRITE);
    if (!file) {
        LOG_ERROR("WIFI", "Failed to open file for writing");
        return false;
    }

    size_t bytesWritten = serializeJson(doc, file);
    file.close();
    
    LOG_INFO("WIFI", "Saved %d credential(s) (%d bytes)", in.size(), bytesWritten);
    return true;
}

bool WiFiManager::existsCred(const String &ssid) {
    std::vector<Cred> creds;
    if (!loadAllCreds(creds)) return false;
    
    for (const auto &c : creds) {
        if (c.ssid == ssid) return true;
    }
    return false;
}

bool WiFiManager::addOrUpdateCred(const Cred &c) {
    std::vector<Cred> creds;
    loadAllCreds(creds); // OK if false -> creds will be empty
    
    bool found = false;
    for (auto &x : creds) {
        if (x.ssid == c.ssid) {
            x.pass = c.pass;
            found = true;
            LOG_DEBUG("WIFI", "Updated existing credential for '%s'", c.ssid.c_str());
            break;
        }
    }
    
    if (!found) {
        creds.push_back(c);
        LOG_DEBUG("WIFI", "Added new credential for '%s'", c.ssid.c_str());
    }
    
    return saveAllCreds(creds);
}

// ============================================
// NETWORK CONNECTION FUNCTIONS
// ============================================

int WiFiManager::indexOfScannedSSID(const String &ssid, int nScan) {
    for (int i = 0; i < nScan; ++i) {
        if (WiFi.SSID(i) == ssid) return i;
    }
    return -1;
}

bool WiFiManager::connect(const String& ssid, const String& pass) {
    LOG_INFO("WIFI", "Connecting to '%s'...", ssid.c_str());
    
    WiFi.begin(ssid.c_str(), pass.c_str());
    
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
        delay(200);
        Serial.print("."); // Keep dots for visual feedback during connection
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(); // Newline after dots
        LOG_INFO("WIFI", "Connected successfully");
        LOG_INFO("WIFI", "IP: %s", WiFi.localIP().toString().c_str());
        
        // Start mDNS service
        startMDNS();
        
        // Save credential to database
        Cred c{ssid, pass};
        addOrUpdateCred(c);
        
        return true;
    } else {
        Serial.println(); // Newline after dots
        LOG_ERROR("WIFI", "Connection FAILED (timeout or wrong password)");
        return false;
    }
}

bool WiFiManager::connectFromSaved() {
    std::vector<Cred> creds;
    if (!loadAllCreds(creds)) {
        LOG_WARN("WIFI", "No saved credentials found");
        return false;
    }

    // Scan for visible networks
    LOG_INFO("WIFI", "Scanning networks...");
    int n = WiFi.scanNetworks();
    
    if (n < 0) {
        LOG_ERROR("WIFI", "Network scan error");
        return false;
    }
    
    if (n == 0) {
        LOG_WARN("WIFI", "No networks found");
        return false;
    }

    LOG_INFO("WIFI", "Found %d network(s)", n);

    // Try only saved credentials that are visible in scan results
    for (const auto &c : creds) {
        int idx = indexOfScannedSSID(c.ssid, n);
        if (idx >= 0) {
            LOG_INFO("WIFI", "Saved network '%s' detected (RSSI: %d dBm)", 
                     c.ssid.c_str(), WiFi.RSSI(idx));
            LOG_INFO("WIFI", "Attempting connection...");
            
            if (connect(c.ssid, c.pass)) {
                return true;
            }
            // If connection fails, try next saved credential
            LOG_WARN("WIFI", "Connection failed, trying next saved network");
        } else {
            LOG_DEBUG("WIFI", "Saved network '%s' not in range", c.ssid.c_str());
        }
    }
    
    LOG_WARN("WIFI", "No known network found in range");
    return false;
}

void WiFiManager::scanAndAskCredentials() {
    LOG_INFO("WIFI", "Starting interactive network selection via Serial");
    LOG_INFO("WIFI", "Scanning networks...");
    
    int n = WiFi.scanNetworks();
    if (n <= 0) {
        LOG_WARN("WIFI", "No networks found");
        return;
    }

    // Display available networks to user
    LOG_INFO("WIFI", "Found %d network(s):", n);
    for (int i = 0; i < n; ++i) {
        String enc = WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "🔒 SECURE";
        Serial.printf("[%d] %s (%d dBm) %s\n", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), enc.c_str());
    }
    
    Serial.println("\nEnter network number:");
    String input = readSerialWithTimeout(WIFI_TIMEOUT_MS);
    
    // Check if input timeout occurred
    if (input.isEmpty()) {
        LOG_WARN("WIFI", "Input timeout - no network selected");
        return; // Exit and fallback to AP mode
    }
    
    int idx = input.toInt();
    
    // Validate input range
    if (idx < 0 || idx >= n) {
        LOG_ERROR("WIFI", "Invalid network number: %d", idx);
        return; // Exit instead of infinite loop
    }
    
    String ssid = WiFi.SSID(idx);
    String pass = "";
    
    if (WiFi.encryptionType(idx) != WIFI_AUTH_OPEN) {
        Serial.printf("Enter password for '%s':\n", ssid.c_str());
        pass = readSerialWithTimeout(60000); // 60 seconds for password entry
        
        // Check password timeout
        if (pass.isEmpty()) {
            LOG_WARN("WIFI", "Password input timeout");
            return;
        }
    } else {
        LOG_INFO("WIFI", "Open network selected, no password required");
    }
    
    // Limited retry attempts instead of infinite loop
    int maxAttempts = 3;
    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
        LOG_INFO("WIFI", "Connection attempt %d/%d", attempt, maxAttempts);
        
        if (connect(ssid, pass)) {
            LOG_INFO("WIFI", "Connection established and credential saved");
            return;
        }
        
        if (attempt < maxAttempts) {
            LOG_WARN("WIFI", "Wrong password or network unavailable");
            Serial.println("Enter new password (or press ENTER to abort):");
            
            String next = readSerialWithTimeout(30000);
            if (next.isEmpty()) {
                LOG_INFO("WIFI", "Aborting connection attempts");
                return;
            }
            pass = next;
        }
    }
    
    LOG_ERROR("WIFI", "Maximum connection attempts reached");
}

void WiFiManager::startAP() {
    LOG_INFO("WIFI", "Starting fallback Access Point...");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    
    LOG_INFO("WIFI", "AP started successfully");
    LOG_INFO("WIFI", "SSID: %s", AP_SSID);
    LOG_INFO("WIFI", "IP: %s", WiFi.softAPIP().toString().c_str());
}

void WiFiManager::clearCredentials() {
    if (LittleFS.exists(WIFI_DB_PATH)) {
        LittleFS.remove(WIFI_DB_PATH);
        LOG_INFO("WIFI", "All credentials cleared");
    } else {
        LOG_WARN("WIFI", "No credential database found");
    }
}

void WiFiManager::startMDNS() {
    if (WiFi.status() != WL_CONNECTED) {
        LOG_DEBUG("WIFI", "Skipping mDNS (not connected)");
        return;
    }
    
    if (MDNS.begin(MDNS_HOSTNAME)) {
        LOG_INFO("MDNS", "Responder started: http://%s.local", MDNS_HOSTNAME);
    } else {
        LOG_ERROR("MDNS", "Failed to start responder");
    }
}

bool WiFiManager::autoConnect() {
    LOG_INFO("WIFI", "Starting auto-connect sequence");
    
    // Step 1: Try saved credentials
    if (connectFromSaved()) {
        return true;
    }
    
    // Step 2: Interactive serial setup
    scanAndAskCredentials();
    if (isConnected()) {
        return true;
    }
    
    // Step 3: Fallback to AP mode
    startAP();
    return false;
}

// ============================================
// SERIAL INPUT HELPER
// ============================================

String WiFiManager::readSerialWithTimeout(unsigned long timeoutMs) {
    LOG_DEBUG("WIFI", "Waiting for serial input (timeout: %lu ms)", timeoutMs);
    
    // STEP 1: Flush output buffer completely
    Serial.flush(); // Wait for outgoing data to be sent
    delay(50);      // Brief pause for stability
    
    // STEP 2: Clear input buffer (remove stale data)
    while (Serial.available() > 0) {
        Serial.read(); // Discard all pending bytes
    }
    
    // STEP 3: Read actual user input
    unsigned long start = millis();
    String result;
    
    while (millis() - start < timeoutMs) {
        if (Serial.available()) {
            char c = Serial.read();
            
            if (c == '\n' || c == '\r') {
                // Handle line ending (Windows uses \r\n, Unix uses \n)
                while (Serial.available() && (Serial.peek() == '\n' || Serial.peek() == '\r')) {
                    Serial.read(); // Consume any additional newline characters
                }
                
                result.trim();
                LOG_DEBUG("WIFI", "Serial input received: '%s'", result.c_str());
                return result;
            }
            
            result += c;
            start = millis(); // Reset timeout on each character received
        }
        
        delay(10);
    }
    
    // Timeout occurred
    result.trim();
    if (result.isEmpty()) {
        LOG_DEBUG("WIFI", "Serial input timeout (no data)");
    } else {
        LOG_DEBUG("WIFI", "Partial input received: '%s'", result.c_str());
    }
    
    return result;
}
