#pragma once

// ============================================================
//  n2k_publisher.h — NMEA 2000 periodic PGN transmission
// ============================================================

class tNMEA2000;
struct EngineState;
struct TempDestination;

namespace sensesp {
template <typename T> class PersistingObservableValue;
}

namespace sensesp::onewire {
class OneWireTemperature;
}

namespace n2k_publisher {

struct InitParams {
    EngineState*                                state;
    tNMEA2000*                                  nmea2000;
    sensesp::PersistingObservableValue<float>*   tankCapacityL;
    sensesp::PersistingObservableValue<int>**    owDest;       // array[NUM_ONEWIRE_SLOTS]
    sensesp::onewire::OneWireTemperature**       owSensors;    // array[NUM_ONEWIRE_SLOTS]
};

void init(const InitParams& p);

}  // namespace n2k_publisher
