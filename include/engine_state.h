#pragma once

// ============================================================
//  engine_state.h — Shared engine/sensor state
//
//  A single flat struct replacing scattered file-scope globals.
//  One static instance lives in main.cpp and is passed by
//  pointer to each module's init() function.
// ============================================================

#include <cstdint>
#include "halmet_config.h"

enum class CoolantAlertState : uint8_t {
    NORMAL = 0,
    WARN   = 1,
    ALARM  = 2,
};

struct EngineState {
    // Written by analog_inputs
    double            coolantK            = -1e9;   // N2kDoubleNA sentinel
    uint32_t          coolantLastUpdateMs = 0;
    float             tankLevelPct        = TANK_LEVEL_HIGH_PCT;
    CoolantAlertState coolantAlertState   = CoolantAlertState::NORMAL;

    // Written by digital_alarms
    bool     oilAlarm        = false;
    bool     tempAlarm       = false;
    uint8_t  oilAlarmHistory  = 0;
    uint8_t  tempAlarmHistory = 0;

    // Written by engine_state_machine
    bool     engineRunning    = false;
    bool     engineRunningRaw = false;
    uint32_t engineStateMs    = 0;

    // Written by analog_inputs (ADS recovery)
    bool     adsOk        = false;
    uint32_t adsFailCount = 0;
};
