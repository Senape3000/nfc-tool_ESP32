#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>
#include "config.h"
#include "logger.h"

/**
 * @brief WiFi connection manager with persistent credential storage
 * 
 * Manages WiFi connections using a priority-based approach:
 * 1. Try connecting to saved networks (from LittleFS JSON database)
 * 2. If none available, prompt user via Serial for network selection
 * 3. If no input received, start fallback Access Point mode
 * 
 * Features:
 * - Persistent credential storage in /wifi_db.json
 * - Network scanning and signal strength reporting
 * - Interactive serial-based network selection
 * - Automatic fallback to AP mode
 * - mDNS responder for .local domain access
 */
class WiFiManager {
public:
    WiFiManager();

    /**
     * @brief Initialize WiFi in Station mode
     * @return Always returns true
     * @note Must be called after LittleFS.begin()
     */
    bool begin();

    /**
     * @brief Attempt connection to any saved network
     * @return true if connected successfully
     * 
     * Scans visible networks and attempts connection to any
     * previously saved credential that matches a visible SSID.
     * Credentials are tried in the order they appear in database.
     */
    bool connectFromSaved();

    /**
     * @brief Interactive network selection via Serial
     * 
     * Scans available networks, displays them to user via Serial,
     * and prompts for network selection + password entry.
     * Implements timeout-based fallback and retry logic.
     */
    void scanAndAskCredentials();

    /**
     * @brief Start fallback Access Point mode
     * 
     * Creates a WiFi AP using credentials from config.h.
     * Used when no STA connection can be established.
     */
    void startAP();

    /**
     * @brief Connect to specific network (blocking)
     * @param ssid Network SSID
     * @param pass Network password
     * @return true if connection successful within timeout
     * 
     * Blocks until connected or WIFI_TIMEOUT_MS expires.
     * On success, saves credential to database and starts mDNS.
     */
    bool connect(const String& ssid, const String& pass);

    /**
     * @brief Check if connected to WiFi network
     * @return true if WL_CONNECTED status
     */
    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }

    /**
     * @brief Get current local IP address
     * @return IP address (0.0.0.0 if not connected)
     */
    IPAddress localIP() const { return WiFi.localIP(); }

    /**
     * @brief Delete all saved credentials from filesystem
     */
    void clearCredentials();

    /**
     * @brief Automatic connection sequence
     * @return true if connected (STA or AP mode)
     * 
     * Execution order:
     * 1. connectFromSaved()
     * 2. scanAndAskCredentials()
     * 3. startAP() (fallback)
     */
    bool autoConnect();

    /**
     * @brief WiFi credential structure
     */
    struct Cred {
        String ssid;
        String pass;
    };

    /**
     * @brief Add or update credential in database
     * @param c Credential to save
     * @return true if saved successfully
     * 
     * If SSID already exists, updates password.
     * If new SSID, appends to database.
     */
    bool addOrUpdateCred(const Cred &c);

private:
    // ============================================
    // CREDENTIAL DATABASE (JSON via LittleFS)
    // ============================================
    
    /**
     * @brief Load all credentials from filesystem
     * @param out Vector to populate with credentials
     * @return true if file exists and parsed successfully
     */
    bool loadAllCreds(std::vector<Cred> &out);

    /**
     * @brief Save credentials to filesystem
     * @param in Vector of credentials to persist
     * @return true if write successful
     */
    bool saveAllCreds(const std::vector<Cred> &in);

    /**
     * @brief Check if credential exists for given SSID
     * @param ssid SSID to search for
     * @return true if found in database
     */
    bool existsCred(const String &ssid);

    /**
     * @brief Start mDNS responder (if connected)
     * 
     * Enables access via http://[MDNS_HOSTNAME].local
     * Only starts if WiFi is in connected state.
     */
    void startMDNS();

    // ============================================
    // NETWORK UTILITIES
    // ============================================
    
    /**
     * @brief Find SSID in scan results
     * @param ssid SSID to search for
     * @param nScan Number of scan results
     * @return Index of SSID in scan results, or -1 if not found
     */
    int indexOfScannedSSID(const String &ssid, int nScan);

    /**
     * @brief Read line from Serial with timeout
     * @param timeoutMs Timeout in milliseconds
     * @return Input string (trimmed), or empty string on timeout
     * 
     * Implementation details:
     * - Flushes output buffer before reading
     * - Clears input buffer to remove stale data
     * - Resets timeout on each character received
     * - Handles both \n and \r\n line endings
     */
    String readSerialWithTimeout(unsigned long timeoutMs = 30000);
};
