#pragma once

// ============================================================
//  digital_alarms.h — Oil/temp alarm input debounce
// ============================================================

struct EngineState;

namespace digital_alarms {

void init(EngineState* state);

}  // namespace digital_alarms
