#include "BilgeFan.h"
#include <Arduino.h>
#include "halmet_config.h"

// ============================================================
//  BilgeFan.cpp  —  Implementation
// ============================================================

BilgeFan::BilgeFan(uint8_t relayPin, bool activeHigh)
    : _pin(relayPin), _activeHigh(activeHigh) {}

void BilgeFan::begin() {
    pinMode(_pin, OUTPUT);
    setRelay(false);      // Always start with relay OFF
    _state    = FanState::IDLE;
    _timerSec = 0.0f;
}

void BilgeFan::update(bool engineRunning, float purgeDurationSec) {
    // dt is the nominal tick period in seconds
    constexpr float dt = INTERVAL_FAN_MS / 1000.0f;

    switch (_state) {
        // -------------------------------------------------------
        case FanState::IDLE:
            if (!_manualOverride) setRelay(false);   // Guarantee relay is OFF
            if (engineRunning) {
                _state = FanState::RUNNING;
            }
            break;

        // -------------------------------------------------------
        case FanState::RUNNING:
            if (!_manualOverride) setRelay(false);   // Guarantee relay is OFF
            if (!engineRunning) {
                _timerSec = purgeDurationSec;
                _state    = FanState::PURGE;
            }
            break;

        // -------------------------------------------------------
        case FanState::PURGE:
            if (engineRunning) {
                // Engine restarted during purge — abort immediately
                if (!_manualOverride) setRelay(false);
                _state = FanState::RUNNING;
                break;
            }
            setRelay(true);
            _timerSec -= dt;
            if (_timerSec <= 0.0f) {
                // Timer expired: release manual latch and turn relay off
                _manualOverride = false;
                setRelay(false);
                _state = FanState::IDLE;
            }
            break;
    }
}

void BilgeFan::forceOff() {
    _manualOverride = false;
    setRelay(false);
    _state    = FanState::IDLE;
    _timerSec = 0.0f;
}

void BilgeFan::manualOn() {
    _manualOverride = true;
    setRelay(true);
}

void BilgeFan::setRelay(bool on) {
    if (on == _relayOn) return;   // No change — skip digitalWrite + callback
    _relayOn = on;
    digitalWrite(_pin, _activeHigh ? (on ? HIGH : LOW)
                                   : (on ? LOW  : HIGH));
    if (_onChange) {
        _onChange(_relayOn);
    }
}
