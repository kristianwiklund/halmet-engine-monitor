// ============================================================
//  n2k_publisher.cpp — NMEA 2000 periodic PGN transmission
// ============================================================

#include "n2k_publisher.h"

#include <Arduino.h>
#include <cmath>
#include <NMEA2000.h>
#include <N2kMessages.h>
#include <sensesp.h>
#include <sensesp/system/observablevalue.h>
#include <sensesp_onewire/onewire_temperature.h>

#include "halmet_config.h"
#include "engine_state.h"
#include "onewire_setup.h"
#include "N2kSenders.h"

using namespace sensesp;
using namespace sensesp::onewire;

namespace n2k_publisher {

void init(const InitParams& p) {
    EngineState*                       st         = p.state;
    tNMEA2000*                         nmea       = p.nmea2000;
    PersistingObservableValue<float>*  povTankCap  = p.tankCapacityL;
    PersistingObservableValue<int>**   owDest      = p.owDest;
    OneWireTemperature**               owSensors   = p.owSensors;

    // N2K slow PGNs: PGN 127489 + PGN 127505 (1 s)
    event_loop()->onRepeat(1000, [st, nmea, povTankCap]() {
        double coolantToSend = st->coolantK;
        if (st->coolantLastUpdateMs == 0 ||
            (millis() - st->coolantLastUpdateMs) > STALE_DATA_TIMEOUT_MS) {
            coolantToSend = N2kDoubleNA;
        }
        N2kSenders::sendEngineDynamic(*nmea, N2K_ENGINE_INSTANCE,
                                      coolantToSend, N2kDoubleNA,
                                      st->oilAlarm, st->tempAlarm);
        N2kSenders::sendFluidLevel(*nmea, 0, N2kft_Fuel,
                                   st->tankLevelPct, povTankCap->get());
    });

    // 1-Wire → N2K PGN 130316 (10 s, matching 1-Wire read interval)
    event_loop()->onRepeat(INTERVAL_ONEWIRE_N2K_MS, [nmea, owDest, owSensors]() {
        for (int i = 0; i < NUM_ONEWIRE_SLOTS; i++) {
            if (!owDest[i] || !owSensors[i]) continue;
            int dest = owDest[i]->get();
            if (dest <= 0 || dest >= kNumTempDests) continue;
            int n2kSrc = kTempDests[dest].n2kSource;
            if (n2kSrc < 0) continue;
            float tempK = owSensors[i]->get();
            if (std::isnan(tempK) || tempK <= 0) continue;
            N2kSenders::sendTemperatureExtended(
                *nmea, i,
                static_cast<tN2kTempSource>(n2kSrc),
                tempK);
        }
    });

    // NMEA 2000 message pump (every 1 ms — must be fast)
    event_loop()->onRepeat(1, [nmea]() {
        nmea->ParseMessages();
    });
}

}  // namespace n2k_publisher
