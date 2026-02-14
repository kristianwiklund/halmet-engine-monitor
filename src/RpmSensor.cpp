#include "RpmSensor.h"

// ============================================================
//  RpmSensor.cpp
// ============================================================

// Static members shared with the ISR
volatile uint32_t RpmSensor::_pulseCount    = 0;
volatile uint32_t RpmSensor::_lastPulseTime = 0;

// ----------------------------------------------------------
//  ISR  — runs in IRAM, counts every falling edge
// ----------------------------------------------------------
void IRAM_ATTR RpmSensor::isrHandler() {
    _pulseCount = _pulseCount + 1;  // avoid deprecated volatile++ in C++20
    _lastPulseTime = micros();
}

// ----------------------------------------------------------
RpmSensor::RpmSensor(uint8_t pin, float pulsesPerRev, int smoothingSamples)
    : _pin(pin),
      _pulsesPerRev(pulsesPerRev),
      _smoothingSamples(smoothingSamples < 1 ? 1
                       : smoothingSamples > kMaxSamples ? kMaxSamples
                       : smoothingSamples)
{}

void RpmSensor::begin() {
    pinMode(_pin, INPUT);                            // HALMET D-inputs have external pull/clamp
    attachInterrupt(digitalPinToInterrupt(_pin),
                    isrHandler,
                    FALLING);
    _lastUpdateMs = millis();
}

float RpmSensor::update() {
    unsigned long now   = millis();
    unsigned long dtMs  = now - _lastUpdateMs;
    if (dtMs == 0) return _smoothedRpm;
    _lastUpdateMs = now;

    // Atomically snapshot and clear the counter
    noInterrupts();
    uint32_t pulses     = _pulseCount;
    _pulseCount         = 0;
    interrupts();

    // Compute instantaneous RPM from pulse count over the elapsed interval
    float instantHz  = (float)pulses / (dtMs / 1000.0f);
    float instantRpm = (instantHz / _pulsesPerRev) * 60.0f;

    // Moving-average smoothing
    _samples[_sampleIdx] = instantRpm;
    _sampleIdx = (_sampleIdx + 1) % _smoothingSamples;
    if (_sampleCount < _smoothingSamples) _sampleCount++;

    float sum = 0.0f;
    for (int i = 0; i < _sampleCount; i++) sum += _samples[i];
    _smoothedRpm = sum / _sampleCount;

    // If no pulses in the last 2 seconds, engine is definitely stopped
    noInterrupts();
    uint32_t lastPulse = _lastPulseTime;
    interrupts();
    if ((micros() - lastPulse) > 2'000'000UL && _pulseCount == 0) {
        _smoothedRpm = 0.0f;
        // Reset moving-average buffer so stale values don't linger
        for (int i = 0; i < kMaxSamples; i++) _samples[i] = 0.0f;
        _sampleCount = 0;
    }

    return _smoothedRpm;
}
