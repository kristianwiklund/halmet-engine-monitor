// ============================================================
//  engine_state_machine.cpp — RPM reading + engine running debounce
// ============================================================

#include "engine_state_machine.h"

#include <Arduino.h>
#include <NMEA2000.h>
#include <sensesp.h>
#include <sensesp/system/observablevalue.h>

#include "halmet_config.h"
#include "engine_state.h"
#include "RpmSensor.h"
#include "N2kSenders.h"

using namespace sensesp;

namespace engine_state_machine {

static void updateEngineState(EngineState* st, bool rawRunning) {
    if (rawRunning != st->engineRunningRaw) {
        st->engineRunningRaw = rawRunning;
        st->engineStateMs    = millis();
    }
    if ((millis() - st->engineStateMs) >= ENGINE_STATE_DEBOUNCE_MS) {
        st->engineRunning = st->engineRunningRaw;
    }
}

void init(const InitParams& p) {
    EngineState*                       st        = p.state;
    tNMEA2000*                         nmea      = p.nmea2000;
    RpmSensor*                         rpm       = p.rpm;
    PersistingObservableValue<float>*  povPulses  = p.pulsesPerRev;
    PersistingObservableValue<float>*  povThresh  = p.runningThreshold;

    // RPM counter + N2K PGN 127488 (100 ms / 10 Hz)
    event_loop()->onRepeat(INTERVAL_RPM_MS, [st, nmea, rpm, povPulses, povThresh]() {
        rpm->setPulsesPerRev(povPulses->get());
        float rpmVal = rpm->update();
        updateEngineState(st, rpmVal > povThresh->get());
        N2kSenders::sendEngineRapidUpdate(*nmea, N2K_ENGINE_INSTANCE, rpmVal);
    });
}

}  // namespace engine_state_machine
