#pragma once

// ============================================================
//  RpmSensor.h  —  Alternator W-terminal RPM measurement
//
//  Uses the ESP32 pulse-counter peripheral via the Arduino
//  attachInterrupt mechanism.  An ISR counts falling edges on
//  HALMET_PIN_D1 (the conditioned W-terminal signal).
//
//  call RpmSensor::begin() once in setup().
//  call RpmSensor::getRpm()  each INTERVAL_RPM_MS to get the
//  latest smoothed engine RPM.
// ============================================================

#include <Arduino.h>
#include "halmet_config.h"

class RpmSensor {
public:
    /// @param pin              GPIO of the conditioned W-terminal signal
    /// @param pulsesPerRev     W-terminal pulses per engine crankshaft rev
    /// @param smoothingSamples Moving-average window length
    explicit RpmSensor(uint8_t pin,
                       float   pulsesPerRev    = DEFAULT_PULSES_PER_REVOLUTION,
                       int     smoothingSamples = RPM_SMOOTHING_SAMPLES);

    void  begin();

    /// Update internal state; call every INTERVAL_RPM_MS ms.
    /// Returns the current smoothed RPM.
    float update();

    float getRpm()          const { return _smoothedRpm; }
    float getPulsesPerRev() const { return _pulsesPerRev; }

    /// Allow runtime reconfiguration (from web UI parameter)
    void  setPulsesPerRev(float p) { _pulsesPerRev = p; }

    // ISR — must be public so attachInterrupt() can reach it
    static void IRAM_ATTR isrHandler();

private:
    uint8_t _pin;
    float   _pulsesPerRev;
    int     _smoothingSamples;
    float   _smoothedRpm  = 0.0f;

    // Circular buffer for moving average
    static constexpr int kMaxSamples = 20;
    float   _samples[kMaxSamples] = {};
    int     _sampleIdx = 0;
    int     _sampleCount = 0;

    // Shared with ISR — must be volatile
    static volatile uint32_t _pulseCount;
    static volatile uint32_t _lastPulseTime;  // micros() at last edge

    unsigned long _lastUpdateMs = 0;
};
