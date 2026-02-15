#pragma once

#include <Arduino.h>
#include <functional>

// ============================================================
//  BilgeFan.h  —  Engine-stop bilge fan purge controller
//
//  State machine:
//
//    IDLE  ──(engine starts)──▶  RUNNING  ──(engine stops)──▶  PURGE
//      ▲                                                           │
//      └──────────────── (purge timer expires) ────────────────────┘
//
//  Constraints enforced:
//   • Relay is NEVER energised in IDLE or RUNNING states.
//   • If engine restarts during PURGE, relay is de-energised
//     immediately and state returns to RUNNING.
//   • purgeDurationSec is configurable at runtime.
// ============================================================

enum class FanState : uint8_t {
    IDLE    = 0,   ///< Engine has not yet run this session
    RUNNING = 1,   ///< Engine is running; fan is OFF
    PURGE   = 2,   ///< Engine just stopped; fan is ON for purge period
};

class BilgeFan {
public:
    /// @param relayPin    GPIO connected to relay module IN
    /// @param activeHigh  true if relay activates on HIGH (most modules)
    explicit BilgeFan(uint8_t relayPin, bool activeHigh = true);

    /// Call once in setup()
    void begin();

    /// Call periodically (every INTERVAL_FAN_MS ms) from loop() or a
    /// RepeatSensor tick.
    /// @param engineRunning  true when RPM > threshold (debounced)
    /// @param purgeDurationSec  configurable purge time in seconds
    void update(bool engineRunning, float purgeDurationSec);

    FanState state()    const { return _state; }
    bool     relayOn()  const { return _relayOn; }

    /// Force relay OFF immediately and reset to IDLE.
    /// Used before OTA to prevent relay freezing during firmware write.
    void forceOff();

    /// Register a callback invoked whenever relay state changes.
    void onRelayChange(std::function<void(bool)> cb) { _onChange = cb; }

private:
    void setRelay(bool on);

    uint8_t  _pin;
    bool     _activeHigh;
    FanState _state     = FanState::IDLE;
    bool     _relayOn   = false;
    float    _timerSec  = 0.0f;

    std::function<void(bool)> _onChange;
};
