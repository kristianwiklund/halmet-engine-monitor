#pragma once

// ============================================================
//  diagnostics.h — SK diagnostic outputs and heartbeat
// ============================================================

struct EngineState;

namespace diagnostics {

void init(const EngineState* state);

}  // namespace diagnostics
