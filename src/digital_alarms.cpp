// ============================================================
//  digital_alarms.cpp — Oil/temp alarm input debounce
// ============================================================

#include "digital_alarms.h"

#include <Arduino.h>
#include <sensesp.h>

#include "halmet_config.h"
#include "engine_state.h"

using namespace sensesp;

namespace digital_alarms {

void init(EngineState* st) {
    // Alarm digital inputs with debounce (500 ms)
    // Shift-register majority vote: alarm asserts only when
    // ALARM_DEBOUNCE_THRESHOLD of the last ALARM_DEBOUNCE_SAMPLES agree.
    event_loop()->onRepeat(INTERVAL_DIGITAL_ALARM_MS, [st]() {
        constexpr uint8_t mask = (1 << ALARM_DEBOUNCE_SAMPLES) - 1;

        st->oilAlarmHistory  = ((st->oilAlarmHistory  << 1) | (digitalRead(HALMET_PIN_D2) == LOW)) & mask;
        st->tempAlarmHistory = ((st->tempAlarmHistory << 1) | (digitalRead(HALMET_PIN_D3) == LOW)) & mask;

        st->oilAlarm  = (__builtin_popcount(st->oilAlarmHistory)  >= ALARM_DEBOUNCE_THRESHOLD);
        st->tempAlarm = (__builtin_popcount(st->tempAlarmHistory) >= ALARM_DEBOUNCE_THRESHOLD);
    });
}

}  // namespace digital_alarms
