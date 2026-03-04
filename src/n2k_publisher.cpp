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
#include "BilgeFan.h"

using namespace sensesp;
using namespace sensesp::onewire;

namespace n2k_publisher {

// ---- PGN 127502 handler ----
// File-scope pointer set during init(); used by the static handler below.
static BilgeFan* sBilgeFan = nullptr;

static void handleSwitchBankControl(const tN2kMsg& N2kMsg) {
    if (N2kMsg.PGN != 127502UL) return;
    unsigned char targetBank;
    tN2kBinaryStatus bankStatus;
    if (!ParseN2kSwitchbankControl(N2kMsg, targetBank, bankStatus)) return;
    if (targetBank != 0) return;   // only handle our bank
    tN2kOnOff sw0 = N2kGetStatusOnBinaryStatus(bankStatus, 1);
    if      (sw0 == N2kOnOff_On)  sBilgeFan->manualOn();
    else if (sw0 == N2kOnOff_Off) sBilgeFan->forceOff();
    ESP_LOGI("N2K", "PGN 127502: bank=%u sw0=%d", (unsigned)targetBank, (int)sw0);
}

void init(const InitParams& p) {
    EngineState*                       st         = p.state;
    tNMEA2000*                         nmea       = p.nmea2000;
    PersistingObservableValue<float>*  povTankCap  = p.tankCapacityL;
    int*                               owDest      = p.owDest;
    OneWireTemperature**               owSensors   = p.owSensors;
    BilgeFan*                          bilgeFan    = p.bilgeFan;

    // Register PGN 127501 (tx) and 127502 (rx) with the N2K stack
    sBilgeFan = bilgeFan;
    static const unsigned long kExtraTxPGNs[] PROGMEM = { 127501UL, 0 };
    static const unsigned long kExtraRxPGNs[] PROGMEM = { 127502UL, 0 };
    nmea->ExtendTransmitMessages(kExtraTxPGNs);
    nmea->ExtendReceiveMessages(kExtraRxPGNs);
    nmea->SetMsgHandler(handleSwitchBankControl);

    // N2K slow PGNs: PGN 127489 + PGN 127505 + PGN 127501 (1 s)
    event_loop()->onRepeat(1000, [st, nmea, povTankCap, bilgeFan]() {
        double coolantToSend = st->coolantK;
        if (st->coolantLastUpdateMs == 0 ||
            (millis() - st->coolantLastUpdateMs) > STALE_DATA_TIMEOUT_MS) {
            coolantToSend = N2kDoubleNA;
        }
        N2kSenders::sendEngineDynamic(*nmea, N2K_ENGINE_INSTANCE,
                                      coolantToSend,
                                      st->oilAlarm, st->tempAlarm);
        N2kSenders::sendFluidLevel(*nmea, 0, N2kft_Fuel,
                                   st->tankLevelPct, povTankCap->get());
        N2kSenders::sendBinaryStatus(*nmea, 0, bilgeFan->relayOn());
    });

    // 1-Wire → N2K PGN 130316 (10 s, matching 1-Wire read interval)
    event_loop()->onRepeat(INTERVAL_ONEWIRE_N2K_MS, [nmea, owDest, owSensors]() {
        for (int i = 0; i < NUM_ONEWIRE_SLOTS; i++) {
            int dest = owDest[i];
            if (dest <= 0 || dest >= kNumTempDests || !owSensors[i]) continue;
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
