#include "login_handler.h"
#include "config.h"

// ============================================
// CONSTRUCTOR
// ============================================

LoginHandler::LoginHandler()
    : _sessionDurationMs(DEFAULT_SESSION_DURATION_MS) 
{
    // Seed random number generator for token generation
    // Using micros() provides better entropy than millis()
    randomSeed(micros());
    
    LOG_DEBUG("AUTH", "LoginHandler initialized (session duration: %lu sec)", 
              _sessionDurationMs / 1000);
}

// ============================================
// AUTHENTICATION
// ============================================

bool LoginHandler::authenticate(const String& username, const String& password) {
    // Validate credentials against config.h values
    // Currently single-user system, can be extended for multi-user
    bool valid = (username == WEB_USERNAME && password == WEB_PASSWORD);
    
    if (valid) {
        LOG_INFO("AUTH", "Authentication successful for user: %s", username.c_str());
    } else {
        LOG_WARN("AUTH", "Authentication failed for user: %s", username.c_str());
    }
    
    return valid;
}

bool LoginHandler::isAuthenticated(AsyncWebServerRequest *request) {
    // Debug mode bypass - authentication disabled for development
    #if DEBUG_SKIP_AUTH
        static bool warningShown = false;
        if (!warningShown) {
            LOG_WARN("AUTH", "⚠️  DEBUG MODE: Authentication is DISABLED!");
            warningShown = true;
        }
        return true;
    #endif

    // Clean expired sessions before validation
    cleanExpiredSessions();

    // Extract session token from Cookie header
    String token = getTokenFromRequest(request);
    if (token.isEmpty()) {
        LOG_DEBUG("AUTH", "No session token found in request");
        return false;
    }

    // Validate token and check expiration
    if (isTokenValid(token)) {
        // Token valid - renew expiry time (sliding window)
        renewSession(token);
        LOG_DEBUG("AUTH", "Session validated and renewed: %s", token.c_str());
        return true;
    }

    LOG_DEBUG("AUTH", "Invalid or expired session token: %s", token.c_str());
    return false;
}

// ============================================
// SESSION MANAGEMENT
// ============================================

String LoginHandler::createSession() {
    // Generate unique random token
    String token = generateToken();
    
    // Calculate expiry timestamp (current time + duration)
    unsigned long expiry = millis() + _sessionDurationMs;
    _sessions[token] = expiry;
    
    LOG_INFO("AUTH", "Session created: %s (expires in %lu sec)", 
             token.c_str(), _sessionDurationMs / 1000);
    LOG_DEBUG("AUTH", "Active sessions: %d", _sessions.size());
    
    return token;
}

void LoginHandler::terminateSession(AsyncWebServerRequest *request) {
    String token = getTokenFromRequest(request);
    
    if (!token.isEmpty() && _sessions.find(token) != _sessions.end()) {
        _sessions.erase(token);
        LOG_INFO("AUTH", "Session terminated: %s", token.c_str());
        LOG_DEBUG("AUTH", "Active sessions: %d", _sessions.size());
    } else {
        LOG_DEBUG("AUTH", "Attempted to terminate non-existent session");
    }
}

void LoginHandler::cleanExpiredSessions() {
    unsigned long now = millis();
    auto it = _sessions.begin();
    int cleaned = 0;

    // Iterate through sessions and remove expired ones
    while (it != _sessions.end()) {
        // Check if session has expired
        if (now > it->second) {
            LOG_DEBUG("AUTH", "Session expired: %s", it->first.c_str());
            it = _sessions.erase(it);
            cleaned++;
        } else {
            ++it;
        }
    }

    if (cleaned > 0) {
        LOG_INFO("AUTH", "Cleaned %d expired session(s)", cleaned);
        LOG_DEBUG("AUTH", "Active sessions remaining: %d", _sessions.size());
    }
}

// ============================================
// COOKIE HELPERS
// ============================================

String LoginHandler::getTokenFromRequest(AsyncWebServerRequest *request) {
    // Check if Cookie header exists
    if (!request->hasHeader("Cookie")) {
        return "";
    }

    // Get full Cookie header value (may contain multiple cookies)
    String cookie = request->header("Cookie");
    
    // Find "session_token=" in cookie string
    int tokenStart = cookie.indexOf(COOKIE_NAME);
    if (tokenStart == -1) {
        return "";
    }

    // Skip past "session_token=" prefix (14 characters)
    tokenStart += COOKIE_NAME_LENGTH;
    
    // Find end of token value (next semicolon or end of string)
    int tokenEnd = cookie.indexOf(';', tokenStart);
    String token = (tokenEnd == -1) 
        ? cookie.substring(tokenStart)           // Last cookie in string
        : cookie.substring(tokenStart, tokenEnd); // More cookies follow
    
    token.trim();
    
    LOG_DEBUG("AUTH", "Extracted token from Cookie header: %s", token.c_str());
    return token;
}

void LoginHandler::setSessionCookie(AsyncWebServerResponse *response, const String& token) {
    // Build cookie value with security attributes:
    // - HttpOnly: Prevents JavaScript access (XSS protection)
    // - Path=/: Valid for entire website
    // - Max-Age: Seconds until expiration
    String cookieValue = String(COOKIE_NAME) + "=" + token + COOKIE_ATTRIBUTES + 
                        String(_sessionDurationMs / 1000);
    
    response->addHeader("Set-Cookie", cookieValue);
    
    LOG_DEBUG("AUTH", "Session cookie set: %s (Max-Age: %lu sec)", 
              token.c_str(), _sessionDurationMs / 1000);
}

void LoginHandler::clearSessionCookie(AsyncWebServerResponse *response) {
    // Set Max-Age=0 to force browser to delete cookie immediately
    response->addHeader("Set-Cookie", COOKIE_CLEAR_VALUE);
    
    LOG_DEBUG("AUTH", "Session cookie cleared");
}

// ============================================
// PRIVATE HELPERS
// ============================================

String LoginHandler::generateToken() {
    // Character set for token generation
    // 62 chars total: 0-9 (10) + A-Z (26) + a-z (26)
    const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const size_t charSetSize = sizeof(chars) - 1; // Exclude null terminator
    
    String token = "";
    token.reserve(TOKEN_LENGTH); // Pre-allocate to avoid reallocation
    
    // Generate TOKEN_LENGTH random characters
    for (size_t i = 0; i < TOKEN_LENGTH; i++) {
        token += chars[random(0, charSetSize)];
    }
    
    LOG_DEBUG("AUTH", "Generated token: %s", token.c_str());
    return token;
}

bool LoginHandler::isTokenValid(const String& token) {
    // Check if token exists in sessions map
    if (_sessions.find(token) == _sessions.end()) {
        LOG_DEBUG("AUTH", "Token not found in active sessions: %s", token.c_str());
        return false;
    }

    // Get expiry timestamp for this token
    unsigned long expiry = _sessions[token];
    unsigned long now = millis();
    
    // Check if token has expired
    if (now < expiry) {
        // Token still valid
        unsigned long remainingMs = expiry - now;
        LOG_DEBUG("AUTH", "Token valid (expires in %lu sec): %s", 
                  remainingMs / 1000, token.c_str());
        return true;
    }

    // Token expired - remove from map
    _sessions.erase(token);
    LOG_DEBUG("AUTH", "Token expired and removed: %s", token.c_str());
    return false;
}

void LoginHandler::renewSession(const String& token) {
    // Check if token exists in sessions map
    if (_sessions.find(token) != _sessions.end()) {
        // Update expiry to current time + session duration (sliding window)
        unsigned long oldExpiry = _sessions[token];
        unsigned long newExpiry = millis() + _sessionDurationMs;
        _sessions[token] = newExpiry;
        
        LOG_DEBUG("AUTH", "Session renewed: %s (extended by %lu sec)", 
                  token.c_str(), _sessionDurationMs / 1000);
    } else {
        LOG_WARN("AUTH", "Attempted to renew non-existent session: %s", token.c_str());
    }
}
