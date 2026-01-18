#pragma once

/**
 * ╔════════════════════════════════════════════════════════════╗
 * ║                  Lib by Senape3000    - 2026               ║
 * ╚════════════════════════════════════════════════════════════╝
 */

/**
 * @file led_manager.h
 * @brief Non-blocking LED manager with multiple blink patterns
 * 
 * Features:
 * - Multiple blink patterns (slow, fast, double, pulse, heartbeat, etc.)
 * - Non-blocking operation using Ticker
 * - State machine based pattern control
 * - Easy pattern switching
 * - Logger integration
 * 
 * Usage:
 *   LedManager ledMgr;
 *   ledMgr.begin(LED_PIN);
 *   ledMgr.setPattern(LedPattern::FAST_BLINK);
 *   ledMgr.setPattern(LedPattern::CONNECTED);  // Solid ON
 *   ledMgr.off();
 */

#include <Arduino.h>
#include <Ticker.h>

/**
 * @brief LED blink patterns
 */
enum class LedPattern {
    OFF,              ///< LED off
    ON,               ///< LED solid on (connected/ready)
    SLOW_BLINK,       ///< Slow blink: 1000ms on/off (idle, waiting)
    NORMAL_BLINK,     ///< Normal blink: 500ms on/off (default activity)
    FAST_BLINK,       ///< Fast blink: 200ms on/off (busy, processing)
    VERY_FAST_BLINK,  ///< Very fast blink: 100ms on/off (critical activity)
    DOUBLE_BLINK,     ///< Double blink pattern (confirmation)
    TRIPLE_BLINK,     ///< Triple blink pattern (special event)
    HEARTBEAT,        ///< Heartbeat pattern: quick double pulse (alive indicator)
    PULSE,            ///< Smooth fade in/out using PWM (breathing effect)
    SOS               ///< SOS morse code pattern (error/help)
};


class LedManager {
public:
    /**
     * @brief Initialize LED manager
     * @param pin GPIO pin connected to LED (active HIGH)
     * @param invertLogic Set to true if LED is active LOW
     */
    void begin(uint8_t pin, bool invertLogic = false);

    /**
     * @brief Set LED pattern
     * @param pattern Pattern to display
     */
    void setPattern(LedPattern pattern);

    /**
     * @brief Convenience methods for common patterns
     */
    void off();                     ///< Turn LED off
    void on();                      ///< Turn LED on (solid)
    void slowBlink();               ///< Start slow blinking (1 Hz)
    void normalBlink();             ///< Start normal blinking (2 Hz) - alias for blinking()
    void blinking();                ///< Start normal blinking - compatibility with old code
    void fastBlink();               ///< Start fast blinking (5 Hz)
    void veryFastBlink();           ///< Start very fast blinking (10 Hz)
    void doubleBlink();             ///< Start double blink pattern
    void tripleBlink();             ///< Start triple blink pattern
    void heartbeat();               ///< Start heartbeat pattern
    void pulse();                   ///< Start pulse (breathing) pattern
    void sos();                     ///< Start SOS morse pattern
    void connected();               ///< Set connected state (solid on) - compatibility

    /**
     * @brief Get current pattern
     * @return Current active pattern
     */
    LedPattern getCurrentPattern() const;

    /**
     * @brief Check if LED is currently on
     * @return true if LED is on, false otherwise
     */
    bool isOn() const;

private:
    uint8_t _pin;                   ///< LED GPIO pin
    bool _invertLogic;              ///< true if LED is active LOW
    LedPattern _currentPattern;     ///< Current active pattern
    Ticker _ticker;                 ///< Non-blocking timer

    // Pattern state variables
    uint8_t _blinkStep;             ///< Current step in multi-step patterns
    uint8_t _pulseValue;            ///< Current PWM value for pulse pattern
    bool _pulseDirection;           ///< true = increasing, false = decreasing

    /**
     * @brief Internal ticker callbacks for different patterns
     */
    static void tickerCallback();
    void updatePattern();

    /**
     * @brief Set LED physical state
     * @param state true = on, false = off (respects invertLogic)
     */
    void setLedState(bool state);

    /**
     * @brief Set LED PWM value (for pulse pattern)
     * @param value PWM value 0-255
     */
    void setLedPWM(uint8_t value);

    /**
     * @brief Pattern-specific update functions
     */
    void updateDoubleBlink();
    void updateTripleBlink();
    void updateHeartbeat();
    void updatePulse();
    void updateSOS();

    // Static instance pointer for ticker callback
    static LedManager* _instance;
};