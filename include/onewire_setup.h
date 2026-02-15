#pragma once

// ============================================================
//  onewire_setup.h — 1-Wire temperature destination table + setup
// ============================================================

#include "halmet_config.h"

namespace sensesp::onewire {
class OneWireTemperature;
}

struct TempDestination {
    const char* label;     // web UI display name
    int         n2kSource; // tN2kTempSource enum, or -1 for SK-only / disabled
    const char* skPath;    // SK path, or nullptr for raw sensor index
};

extern const TempDestination kTempDests[];
extern const int kNumTempDests;

namespace onewire_setup {

struct Outputs {
    int                                      owDest[NUM_ONEWIRE_SLOTS];
    sensesp::onewire::OneWireTemperature*    owSensors[NUM_ONEWIRE_SLOTS];
};

void init(Outputs& out);

}  // namespace onewire_setup
