// ============================================================
//  onewire_setup.cpp — 1-Wire temperature destination table + setup
// ============================================================

#include "onewire_setup.h"

#include <Arduino.h>
#include <sensesp.h>
#include <sensesp/system/observablevalue.h>
#include <sensesp/ui/config_item.h>
#include <sensesp/signalk/signalk_output.h>
#include <sensesp_onewire/onewire_temperature.h>

#include "halmet_config.h"

using namespace sensesp;
using namespace sensesp::onewire;

// ---- APPEND ONLY — do not reorder or insert ----
const TempDestination kTempDests[] = {
//  config  label                     n2k   SK path
    /*0*/  {"Disabled (raw SK)",       -1,  nullptr},
    /*1*/  {"Engine room",              3,  "environment.inside.engineRoom.temperature"},
    /*2*/  {"Exhaust gas",             14,  "propulsion.0.exhaustTemperature"},
    /*3*/  {"Sea water",                0,  "environment.water.temperature"},
    /*4*/  {"Outside air",              1,  "environment.outside.temperature"},
    /*5*/  {"Inside / cabin",           2,  "environment.inside.temperature"},
    /*6*/  {"Refrigeration",            7,  "environment.inside.refrigerator.temperature"},
    /*7*/  {"Freezer",                 13,  "environment.inside.freezer.temperature"},
    /*8*/  {"Alternator (SK only)",    -1,  "electrical.alternators.0.temperature"},
    /*9*/  {"Oil sump (SK only)",      -1,  "propulsion.0.oilTemperature"},
    /*10*/ {"Intake manifold (SK)",    -1,  "propulsion.0.intakeManifoldTemperature"},
    /*11*/ {"Engine block (SK)",       -1,  "propulsion.0.engineBlockTemperature"},
};
const int kNumTempDests = sizeof(kTempDests) / sizeof(TempDestination);

namespace onewire_setup {

void init(Outputs& out) {
    auto* dts = new DallasTemperatureSensors(HALMET_PIN_1WIRE);
    for (int i = 0; i < NUM_ONEWIRE_SLOTS; i++) {
        // Configurable destination per sensor slot
        String destCfg = "/onewire/sensor" + String(i) + "/dest";
        out.owDest[i] = new PersistingObservableValue<int>(
            DEFAULT_ONEWIRE_DEST, destCfg);
        ConfigItem(out.owDest[i])
            ->set_title(("Sensor " + String(i) + " dest (0=off,1=engRoom,2=exhaust,3=sea,4=outside,5=cabin,6=fridge,7=freezer,8=alt,9=oil,10=intake,11=block)").c_str());

        // OneWireTemperature reads in Kelvin; ROM address configurable via web UI
        String owCfg = "/onewire/sensor" + String(i) + "/address";
        out.owSensors[i] = new OneWireTemperature(dts, INTERVAL_1WIRE_MS, owCfg.c_str());

        // SK output — path depends on configured destination
        int dest = out.owDest[i]->get();
        String skPath;
        if (dest > 0 && dest < kNumTempDests && kTempDests[dest].skPath) {
            skPath = String(kTempDests[dest].skPath) + "." + String(i);
        } else {
            skPath = "environment.inside.temperature." + String(i);
        }
        auto* skOutput = new SKOutputFloat(skPath);
        out.owSensors[i]->connect_to(skOutput);
    }
}

}  // namespace onewire_setup
