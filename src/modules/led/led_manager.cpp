#include "led_manager.h"
#include "logger.h"

// Static instance pointer for ticker callback
LedManager* LedManager::_instance = nullptr;

/**
 * @brief Initialize LED manager
 */
void LedManager::begin(uint8_t pin, bool invertLogic) {
    _pin = pin;
    _invertLogic = invertLogic;
    _currentPattern = LedPattern::OFF;
    _blinkStep = 0;
    _pulseValue = 0;
    _pulseDirection = true;

    // Set static instance for ticker callback
    _instance = this;

    // Configure pin
    pinMode(_pin, OUTPUT);
    setLedState(false);  // Start with LED off

    LOG_DEBUG("LED", "Initialized on pin %d (inverted: %s)", 
              _pin, _invertLogic ? "yes" : "no");
}

/**
 * @brief Set LED pattern
 */
void LedManager::setPattern(LedPattern pattern) {
    // Stop current pattern
    _ticker.detach();
    _blinkStep = 0;
    _pulseValue = 0;
    _pulseDirection = true;

    _currentPattern = pattern;

    LOG_DEBUG("LED", "Pattern changed to: %d", (int)pattern);

    switch (pattern) {
        case LedPattern::OFF:
            setLedState(false);
            break;

        case LedPattern::ON:
            setLedState(true);
            break;

        case LedPattern::SLOW_BLINK:
            setLedState(true);
            _ticker.attach(1.0, tickerCallback);  // 1 Hz (1000ms period)
            break;

        case LedPattern::NORMAL_BLINK:
            setLedState(true);
            _ticker.attach(0.5, tickerCallback);  // 2 Hz (500ms period)
            break;

        case LedPattern::FAST_BLINK:
            setLedState(true);
            _ticker.attach(0.2, tickerCallback);  // 5 Hz (200ms period)
            break;

        case LedPattern::VERY_FAST_BLINK:
            setLedState(true);
            _ticker.attach(0.1, tickerCallback);  // 10 Hz (100ms period)
            break;

        case LedPattern::DOUBLE_BLINK:
            setLedState(true);
            _ticker.attach_ms(150, tickerCallback);  // 150ms steps
            break;

        case LedPattern::TRIPLE_BLINK:
            setLedState(true);
            _ticker.attach_ms(150, tickerCallback);  // 150ms steps
            break;

        case LedPattern::HEARTBEAT:
            setLedState(true);
            _ticker.attach_ms(100, tickerCallback);  // 100ms steps
            break;

        case LedPattern::PULSE:
            setLedPWM(0);
            _ticker.attach_ms(20, tickerCallback);  // 20ms steps for smooth PWM
            break;

        case LedPattern::SOS:
            setLedState(true);
            _ticker.attach_ms(100, tickerCallback);  // 100ms steps (dot = 1 unit)
            break;
    }
}

/**
 * @brief Ticker callback (static)
 */
void LedManager::tickerCallback() {
    if (_instance) {
        _instance->updatePattern();
    }
}

/**
 * @brief Update pattern state
 */
void LedManager::updatePattern() {
    switch (_currentPattern) {
        case LedPattern::SLOW_BLINK:
        case LedPattern::NORMAL_BLINK:
        case LedPattern::FAST_BLINK:
        case LedPattern::VERY_FAST_BLINK:
            // Simple toggle for basic blink patterns
            setLedState(!isOn());
            break;

        case LedPattern::DOUBLE_BLINK:
            updateDoubleBlink();
            break;

        case LedPattern::TRIPLE_BLINK:
            updateTripleBlink();
            break;

        case LedPattern::HEARTBEAT:
            updateHeartbeat();
            break;

        case LedPattern::PULSE:
            updatePulse();
            break;

        case LedPattern::SOS:
            updateSOS();
            break;

        default:
            break;
    }
}

/**
 * @brief Double blink pattern: ON-OFF-ON-OFF--pause--
 */
void LedManager::updateDoubleBlink() {
    // Pattern: ON(150ms) - OFF(150ms) - ON(150ms) - OFF(600ms)
    // Steps:   0          1            2           3-6

    switch (_blinkStep) {
        case 0: setLedState(true);  break;  // First ON
        case 1: setLedState(false); break;  // First OFF
        case 2: setLedState(true);  break;  // Second ON
        default: setLedState(false); break; // OFF pause
    }

    _blinkStep++;
    if (_blinkStep >= 7) _blinkStep = 0;  // Reset after pause
}

/**
 * @brief Triple blink pattern: ON-OFF-ON-OFF-ON-OFF--pause--
 */
void LedManager::updateTripleBlink() {
    // Pattern: ON(150ms) - OFF(150ms) - ON(150ms) - OFF(150ms) - ON(150ms) - OFF(600ms)
    // Steps:   0          1            2           3            4           5-8

    switch (_blinkStep) {
        case 0: setLedState(true);  break;  // First ON
        case 1: setLedState(false); break;  // First OFF
        case 2: setLedState(true);  break;  // Second ON
        case 3: setLedState(false); break;  // Second OFF
        case 4: setLedState(true);  break;  // Third ON
        default: setLedState(false); break; // OFF pause
    }

    _blinkStep++;
    if (_blinkStep >= 9) _blinkStep = 0;  // Reset after pause
}

/**
 * @brief Heartbeat pattern: quick double pulse
 */
void LedManager::updateHeartbeat() {
    // Pattern: ON(100ms) - OFF(100ms) - ON(100ms) - OFF(700ms)
    // Steps:   0          1            2           3-9

    switch (_blinkStep) {
        case 0: setLedState(true);  break;  // First pulse
        case 1: setLedState(false); break;
        case 2: setLedState(true);  break;  // Second pulse
        default: setLedState(false); break; // Long pause
    }

    _blinkStep++;
    if (_blinkStep >= 10) _blinkStep = 0;
}

/**
 * @brief Pulse (breathing) pattern using PWM
 */
void LedManager::updatePulse() {
    // Smooth fade in/out
    const uint8_t step = 5;  // PWM increment/decrement step

    if (_pulseDirection) {
        // Fading in
        _pulseValue += step;
        if (_pulseValue >= 250) {  // Near max
            _pulseValue = 255;
            _pulseDirection = false;  // Start fading out
        }
    } else {
        // Fading out
        if (_pulseValue <= step) {
            _pulseValue = 0;
            _pulseDirection = true;  // Start fading in
        } else {
            _pulseValue -= step;
        }
    }

    setLedPWM(_pulseValue);
}

/**
 * @brief SOS morse code pattern: ... --- ...
 */
void LedManager::updateSOS() {
    // SOS in morse: 
    // S = ... (3 short)
    // O = --- (3 long)
    // S = ... (3 short)
    // Short = 1 unit (100ms)
    // Long = 3 units (300ms)
    // Gap between symbols = 1 unit
    // Gap between letters = 3 units
    // Gap between words = 7 units

    // Total: 3*(1+1) + 3 + 3*(3+1) + 3 + 3*(1+1) + 7 = 6 + 3 + 12 + 3 + 6 + 7 = 37 steps

    const bool pattern[37] = {
        // S: ... (dot-space-dot-space-dot)
        1,0,1,0,1,0,0,0,  // 3 dots + letter gap (8 steps)
        // O: --- (dash-space-dash-space-dash)
        1,1,1,0,1,1,1,0,1,1,1,0,0,0,  // 3 dashes + letter gap (14 steps)
        // S: ... (dot-space-dot-space-dot)
        1,0,1,0,1,0,0,0,  // 3 dots + letter gap (8 steps)
        // Word gap
        0,0,0,0,0,0,0  // 7 units pause (7 steps)
    };

    setLedState(pattern[_blinkStep]);

    _blinkStep++;
    if (_blinkStep >= 37) _blinkStep = 0;
}

/**
 * @brief Set LED physical state
 */
void LedManager::setLedState(bool state) {
    // Apply logic inversion if needed
    bool outputState = _invertLogic ? !state : state;
    digitalWrite(_pin, outputState ? HIGH : LOW);
}

/**
 * @brief Set LED PWM value (for pulse pattern)
 */
void LedManager::setLedPWM(uint8_t value) {
    // Apply logic inversion if needed
    uint8_t outputValue = _invertLogic ? (255 - value) : value;
    analogWrite(_pin, outputValue);
}

/**
 * @brief Check if LED is currently on
 */
bool LedManager::isOn() const {
    bool pinState = digitalRead(_pin) == HIGH;
    return _invertLogic ? !pinState : pinState;
}

/**
 * @brief Get current pattern
 */
LedPattern LedManager::getCurrentPattern() const {
    return _currentPattern;
}

// ========================================
// CONVENIENCE METHODS
// ========================================

void LedManager::off() {
    setPattern(LedPattern::OFF);
}

void LedManager::on() {
    setPattern(LedPattern::ON);
}

void LedManager::slowBlink() {
    setPattern(LedPattern::SLOW_BLINK);
}

void LedManager::normalBlink() {
    setPattern(LedPattern::NORMAL_BLINK);
}

void LedManager::blinking() {
    // Compatibility with old code
    setPattern(LedPattern::NORMAL_BLINK);
}

void LedManager::fastBlink() {
    setPattern(LedPattern::FAST_BLINK);
}

void LedManager::veryFastBlink() {
    setPattern(LedPattern::VERY_FAST_BLINK);
}

void LedManager::doubleBlink() {
    setPattern(LedPattern::DOUBLE_BLINK);
}

void LedManager::tripleBlink() {
    setPattern(LedPattern::TRIPLE_BLINK);
}

void LedManager::heartbeat() {
    setPattern(LedPattern::HEARTBEAT);
}

void LedManager::pulse() {
    setPattern(LedPattern::PULSE);
}

void LedManager::sos() {
    setPattern(LedPattern::SOS);
}

void LedManager::connected() {
    // Compatibility with old code
    setPattern(LedPattern::ON);
}