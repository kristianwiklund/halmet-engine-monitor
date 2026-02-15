#pragma once

// ============================================================
//  analog_inputs.h — Coolant temp, tank level, ADS1115 recovery
// ============================================================

struct EngineState;
class Adafruit_ADS1115;

namespace sensesp {
class SKOutputRawJson;
template <typename T> class PersistingObservableValue;
}

namespace analog_inputs {

struct InitParams {
    EngineState*                                    state;
    Adafruit_ADS1115*                               ads;
    sensesp::SKOutputRawJson*                       skCoolantNotification;
    sensesp::PersistingObservableValue<float>*       coolantWarnC;
    sensesp::PersistingObservableValue<float>*       coolantAlarmC;
};

void init(const InitParams& p);

}  // namespace analog_inputs
