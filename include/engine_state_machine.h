#pragma once

// ============================================================
//  engine_state_machine.h — RPM reading + engine running debounce
// ============================================================

class tNMEA2000;
class RpmSensor;
struct EngineState;

namespace sensesp {
template <typename T> class PersistingObservableValue;
}

namespace engine_state_machine {

struct InitParams {
    EngineState*                                state;
    tNMEA2000*                                  nmea2000;
    RpmSensor*                                  rpm;
    sensesp::PersistingObservableValue<float>*   pulsesPerRev;
    sensesp::PersistingObservableValue<float>*   runningThreshold;
};

void init(const InitParams& p);

}  // namespace engine_state_machine
