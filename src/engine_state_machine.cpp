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

    // RPM measurement (10 Hz — feeds smoothing window and engine state)
    event_loop()->onRepeat(INTERVAL_RPM_MS, [st, rpm, povPulses, povThresh]() {
        rpm->setPulsesPerRev(povPulses->get());
        float rpmVal = rpm->update();
        updateEngineState(st, rpmVal > povThresh->get());
    });

    // PGN 127488 — Engine Rapid Update (4 Hz, decoupled from measurement)
    event_loop()->onRepeat(INTERVAL_RPM_N2K_MS, [nmea, rpm]() {
        N2kSenders::sendEngineRapidUpdate(*nmea, N2K_ENGINE_INSTANCE, rpm->getRpm());
    });
}

}  // namespace engine_state_machine
