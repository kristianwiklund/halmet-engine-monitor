// ============================================================
//  diagnostics.cpp — SK diagnostic outputs and heartbeat
// ============================================================

#include "diagnostics.h"

#include <Arduino.h>
#include <esp_system.h>
#include <sensesp.h>
#include <sensesp/signalk/signalk_output.h>

#include "halmet_config.h"
#include "engine_state.h"

using namespace sensesp;

namespace diagnostics {

void init(const EngineState* st) {
    static auto* skDiagUptime    = new SKOutputFloat("design.halmet.diagnostics.uptimeSeconds", "");
    static auto* skDiagVersion   = new SKOutputString("design.halmet.diagnostics.firmwareVersion", "");
    static auto* skDiagAdsFails  = new SKOutputInt("design.halmet.diagnostics.adsFailCount", "");
    static auto* skDiagResetCode = new SKOutputInt("design.halmet.diagnostics.lastResetReason", "");

    skDiagVersion->set(FW_VERSION_STR);

    event_loop()->onRepeat(INTERVAL_DIAG_MS, [st]() {
        skDiagUptime->set(millis() / 1000.0f);
        skDiagAdsFails->set(static_cast<int>(st->adsFailCount));
        skDiagResetCode->set(static_cast<int>(esp_reset_reason()));
    });
}

}  // namespace diagnostics
