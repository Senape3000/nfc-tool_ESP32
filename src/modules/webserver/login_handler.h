#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <map>
#include "config.h"
#include "logger.h"

/**
 * @brief Session-based authentication handler for web interface
 * 
 * Provides secure authentication using HTTP-only cookies and session tokens.
 * Sessions are stored in memory and automatically cleaned when expired.
 * 
 * Security Features:
 * - HttpOnly cookies (prevent JavaScript access)
 * - Automatic session expiration and renewal
 * - Periodic cleanup of expired sessions
 * - Configurable session duration
 * - Debug mode bypass (for development only)
 * 
 * Usage Flow:
 * 1. User submits credentials via login form
 * 2. authenticate() validates credentials
 * 3. createSession() generates secure token and stores in map
 * 4. setSessionCookie() sends token to browser
 * 5. Subsequent requests: isAuthenticated() verifies token
 * 6. Logout: terminateSession() removes token and clears cookie
 * 
 * Implementation Notes:
 * - Sessions stored as map<token, expiry_timestamp>
 * - Tokens are 32-character alphanumeric strings
 * - Sessions auto-renew on each authenticated request
 * - Uses millis() for timestamp (wraps after ~49 days)
 */
class LoginHandler {
public:
    LoginHandler();

    // ============================================
    // AUTHENTICATION
    // ============================================
    
    /**
     * @brief Validate user credentials
     * @param username Username to check
     * @param password Password to check
     * @return true if credentials match WEB_USERNAME and WEB_PASSWORD
     * 
     * @note Currently supports single user from config.h
     * @note Can be extended to support multiple users from database
     */
    bool authenticate(const String& username, const String& password);

    /**
     * @brief Check if request has valid authenticated session
     * @param request HTTP request to validate
     * @return true if session token is valid and not expired
     * 
     * Process:
     * 1. Clean expired sessions from memory
     * 2. Extract session token from Cookie header
     * 3. Validate token exists and not expired
     * 4. Renew session expiry timestamp
     * 
     * @note Returns true immediately if DEBUG_SKIP_AUTH is enabled
     */
    bool isAuthenticated(AsyncWebServerRequest *request);

    // ============================================
    // SESSION MANAGEMENT
    // ============================================
    
    /**
     * @brief Create new authenticated session
     * @return Session token string (32 characters)
     * 
     * Generates cryptographically random token and stores
     * with expiry timestamp in sessions map.
     */
    String createSession();

    /**
     * @brief Terminate session for given request
     * @param request HTTP request containing session token
     * 
     * Extracts token from Cookie header and removes from
     * active sessions map. Does nothing if token not found.
     */
    void terminateSession(AsyncWebServerRequest *request);

    /**
     * @brief Remove expired sessions from memory
     * 
     * Iterates through sessions map and deletes entries where
     * current millis() > expiry timestamp. Automatically called
     * by isAuthenticated() before validation.
     */
    void cleanExpiredSessions();

    // ============================================
    // COOKIE HELPERS
    // ============================================
    
    /**
     * @brief Extract session token from request cookies
     * @param request HTTP request with Cookie header
     * @return Session token or empty string if not found
     * 
     * Parses Cookie header looking for "session_token=value".
     * Handles multiple cookies separated by semicolons.
     */
    String getTokenFromRequest(AsyncWebServerRequest *request);

    /**
     * @brief Set session cookie in response
     * @param response HTTP response to modify
     * @param token Session token to set
     * 
     * Adds Set-Cookie header with:
     * - HttpOnly flag (prevent XSS attacks)
     * - Path=/ (valid for entire site)
     * - Max-Age (seconds until expiration)
     */
    void setSessionCookie(AsyncWebServerResponse *response, const String& token);

    /**
     * @brief Clear session cookie in response
     * @param response HTTP response to modify
     * 
     * Sets Max-Age=0 to force browser to delete cookie.
     */
    void clearSessionCookie(AsyncWebServerResponse *response);

    // ============================================
    // CONFIGURATION
    // ============================================
    
    /**
     * @brief Set session duration
     * @param durationMs Duration in milliseconds
     */
    void setSessionDuration(unsigned long durationMs) { 
        _sessionDurationMs = durationMs; 
        LOG_DEBUG("AUTH", "Session duration set to %lu seconds", durationMs / 1000);
    }

    /**
     * @brief Get current session duration
     * @return Duration in milliseconds
     */
    unsigned long getSessionDuration() const { 
        return _sessionDurationMs; 
    }

    // ============================================
    // STATUS & MONITORING
    // ============================================
    
    /**
     * @brief Get number of active sessions
     * @return Count of non-expired sessions in memory
     */
    int getActiveSessionCount() const { 
        return _sessions.size(); 
    }

private:
    // ============================================
    // CONSTANTS
    // ============================================
    
    // Session token configuration
    static constexpr size_t TOKEN_LENGTH = 32;              // 32 alphanumeric chars
    static constexpr size_t COOKIE_NAME_LENGTH = 14;        // "session_token=" length
    
    // Default session duration: 30 minutes
    static constexpr unsigned long DEFAULT_SESSION_DURATION_MS = 30UL * 60UL * 1000UL;
    
    // Cookie configuration strings
    static constexpr const char* COOKIE_NAME = "session_token";
    static constexpr const char* COOKIE_ATTRIBUTES = "; Path=/; HttpOnly; Max-Age=";
    static constexpr const char* COOKIE_CLEAR_VALUE = "session_token=; Path=/; HttpOnly; Max-Age=0";

    // ============================================
    // MEMBER VARIABLES
    // ============================================
    
    /**
     * @brief Active sessions map
     * Key: session token (32 chars)
     * Value: expiry timestamp (millis)
     */
    std::map<String, unsigned long> _sessions;
    
    /**
     * @brief Session duration in milliseconds
     */
    unsigned long _sessionDurationMs;

    // ============================================
    // PRIVATE HELPERS
    // ============================================
    
    /**
     * @brief Generate cryptographically random session token
     * @return 32-character alphanumeric token
     * 
     * Uses Arduino random() seeded with micros() at construction.
     * Character set: [0-9A-Za-z] (62 possible characters per position)
     */
    String generateToken();

    /**
     * @brief Check if token exists and not expired
     * @param token Session token to validate
     * @return true if token valid and not expired
     * 
     * Automatically removes expired token from map if found.
     */
    bool isTokenValid(const String& token);

    /**
     * @brief Extend session expiry time
     * @param token Session token to renew
     * 
     * Updates expiry timestamp to current millis() + duration.
     * Called automatically on each successful authentication check.
     */
    void renewSession(const String& token);
};
