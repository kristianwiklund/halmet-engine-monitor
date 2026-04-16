#pragma once

// ============================================================
//  analog_inputs.h — Coolant temp, tank level, ADS1115 recovery,
//                    battery voltage, INA226 coolant sender
// ============================================================

struct EngineState;
class Adafruit_ADS1115;
class INA226_WE;

namespace sensesp {
class SKOutputRawJson;
template <typename T> class PersistingObservableValue;
}

namespace analog_inputs {

struct InitParams {
    EngineState*                                    state;
    Adafruit_ADS1115*                               ads;
    INA226_WE*                                      ina226;
    sensesp::SKOutputRawJson*                       skCoolantNotification;
    sensesp::PersistingObservableValue<float>*       coolantWarnC;
    sensesp::PersistingObservableValue<float>*       coolantAlarmC;
    sensesp::PersistingObservableValue<float>*       voltageMultiplier;
};

void init(const InitParams& p);

}  // namespace analog_inputs
