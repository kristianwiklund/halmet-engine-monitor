// ============================================================
//  onewire_setup.cpp — Sensor-centric 1-Wire temperature config
//
//  Sprint 6 UX redesign: each detected sensor gets a config card
//  with ROM address title, live temp description, and a dropdown
//  to pick its destination (engine room, exhaust, sea water, etc.)
// ============================================================

#include "onewire_setup.h"

#include <Arduino.h>
#include <vector>
#include <sensesp.h>
#include <sensesp/system/observablevalue.h>
#include <sensesp/system/saveable.h>
#include <sensesp/ui/config_item.h>
#include <sensesp/signalk/signalk_output.h>
#include <sensesp_onewire/onewire_temperature.h>
#include <OneWireNg_CurrentPlatform.h>
#include <drivers/DSTherm.h>

#include "halmet_config.h"

using namespace sensesp;
using namespace sensesp::onewire;

// ---- APPEND ONLY — do not reorder or insert ----
const TempDestination kTempDests[] = {
//  config  label                     n2k   SK path
    /*0*/  {"Not used",                -1,  nullptr},
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

// ============================================================
//  File-scope state
// ============================================================
static std::vector<OWDevAddr> sDetectedAddrs;

// Per-detected-sensor binding
struct SensorBinding {
    OWDevAddr                       addr;
    PersistingObservableValue<String>* pov;       // persisted dest label
    ConfigItemT<PersistingObservableValue<String>>* configItem;
    int                             slot;         // assigned slot, or -1
};
static std::vector<SensorBinding> sBindings;

// ============================================================
//  Helper: format OWDevAddr as "28:aa:bb:cc:dd:ee:ff:00"
// ============================================================
static void formatAddr(char* buf, const OWDevAddr& addr) {
    sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
            addr[0], addr[1], addr[2], addr[3],
            addr[4], addr[5], addr[6], addr[7]);
}

// Helper: ROM address as compact hex (no colons) for config path
static void formatAddrCompact(char* buf, const OWDevAddr& addr) {
    sprintf(buf, "%02x%02x%02x%02x%02x%02x%02x%02x",
            addr[0], addr[1], addr[2], addr[3],
            addr[4], addr[5], addr[6], addr[7]);
}

// ============================================================
//  Independent bus scan (before DallasTemperatureSensors)
// ============================================================
static void scanBus() {
    sDetectedAddrs.clear();
    {
        OneWireNg_CurrentPlatform ow(HALMET_PIN_1WIRE, false);
        DSTherm drv{ow};
        drv.filterSupportedSlaves();

        for (const auto& id : ow) {
            OWDevAddr owda;
            for (int j = 0; j < 8; j++) owda[j] = id[j];
            sDetectedAddrs.push_back(owda);
        }
    }  // ow destroyed here — pin released

    ESP_LOGI("1Wire", "Bus scan found %d sensor(s):", sDetectedAddrs.size());
    for (size_t i = 0; i < sDetectedAddrs.size(); i++) {
        char buf[24];
        formatAddr(buf, sDetectedAddrs[i]);
        ESP_LOGI("1Wire", "  [%d] %s", i, buf);
    }
}

// ============================================================
//  Build dropdown JSON schema from kTempDests[].label
// ============================================================
static String buildDropdownSchema() {
    String schema = R"({"type":"object","properties":{"value":{"title":"Destination","type":"array","format":"select","uniqueItems":true,"items":{"type":"string","enum":[)";
    for (int i = 0; i < kNumTempDests; i++) {
        if (i > 0) schema += ",";
        schema += "\"";
        schema += kTempDests[i].label;
        schema += "\"";
    }
    schema += "]}}}}";  // closes: enum[], items{}, value{}, properties{}, root{}
    return schema;
}

// ============================================================
//  Find kTempDests index by label string, returns 0 if not found
// ============================================================
static int destIndexByLabel(const String& label) {
    for (int i = 0; i < kNumTempDests; i++) {
        if (label == kTempDests[i].label) return i;
    }
    return 0;  // "Not used"
}

// ============================================================
//  OwAddressPrewriter — writes ROM address to OWT config path
//  so that OneWireTemperature::load() reads the pre-written
//  address instead of auto-assigning.
// ============================================================
class OwAddressPrewriter : public FileSystemSaveable,
                           public virtual Serializable {
public:
    OwAddressPrewriter(const String& config_path, const String& addr)
        : FileSystemSaveable(config_path), addr_(addr) {}

    bool to_json(JsonObject& doc) override {
        doc["address"] = addr_;
        doc["found"] = true;
        return true;
    }
    bool from_json(const JsonObject&) override { return true; }

private:
    String addr_;
};

namespace onewire_setup {

void init(Outputs& out) {
    // Zero-init outputs
    for (int i = 0; i < NUM_ONEWIRE_SLOTS; i++) {
        out.owDest[i] = 0;
        out.owSensors[i] = nullptr;
    }

    // ---- Step 1: scan bus ----
    scanBus();

    // ---- Step 2: build dropdown schema ----
    String dropdownSchema = buildDropdownSchema();

    // ---- Step 3: create config card per detected sensor ----
    sBindings.clear();
    sBindings.reserve(sDetectedAddrs.size());

    for (size_t i = 0; i < sDetectedAddrs.size(); i++) {
        char romColon[24];
        formatAddr(romColon, sDetectedAddrs[i]);
        char romCompact[20];
        formatAddrCompact(romCompact, sDetectedAddrs[i]);

        // Config path: /onewire/<rom_hex>/dest — stable across discovery order
        String cfgPath = String("/onewire/") + romCompact + "/dest";

        auto* pov = new PersistingObservableValue<String>(
            String(kTempDests[0].label), cfgPath);

        auto ci = ConfigItem(pov);
        ci->set_title(romColon)
          ->set_description("Not yet read")
          ->set_config_schema(dropdownSchema)
          ->set_requires_restart(true)
          ->set_sort_order(2000 + (int)i);

        SensorBinding binding;
        binding.addr = sDetectedAddrs[i];
        binding.pov = pov;
        binding.configItem = ci.get();
        binding.slot = -1;
        sBindings.push_back(binding);

        String destLabel = pov->get();
        ESP_LOGI("1Wire", "Sensor %s → dest \"%s\"", romColon, destLabel.c_str());
    }

    // ---- Step 4: slot assignment ----
    int nextSlot = 0;
    for (auto& b : sBindings) {
        String destLabel = b.pov->get();
        int destIdx = destIndexByLabel(destLabel);

        if (destIdx == 0) continue;  // "Not used"

        if (destIdx > 0 && destLabel != String(kTempDests[destIdx].label)) {
            // Label mismatch (shouldn't happen after destIndexByLabel, but guard)
            char romBuf[24];
            formatAddr(romBuf, b.addr);
            ESP_LOGW("1Wire", "Sensor %s: dest \"%s\" not found, treating as Not used",
                     romBuf, destLabel.c_str());
            continue;
        }

        if (nextSlot >= NUM_ONEWIRE_SLOTS) {
            char romBuf[24];
            formatAddr(romBuf, b.addr);
            ESP_LOGW("1Wire", "Sensor %s: all %d slots in use, skipping",
                     romBuf, NUM_ONEWIRE_SLOTS);
            continue;
        }

        // Pre-write ROM address to the OWT config path for this slot
        char romColon[24];
        formatAddr(romColon, b.addr);
        String owCfg = "/onewire/sensor" + String(nextSlot) + "/address";
        OwAddressPrewriter(owCfg, String(romColon)).save();

        b.slot = nextSlot;
        out.owDest[nextSlot] = destIdx;

        ESP_LOGI("1Wire", "Slot %d ← %s → %s (idx %d)",
                 nextSlot, romColon, kTempDests[destIdx].label, destIdx);
        nextSlot++;
    }

    ESP_LOGI("1Wire", "Assigned %d of %d detected sensors to slots",
             nextSlot, sDetectedAddrs.size());

    // ---- Step 5: create DallasTemperatureSensors ----
    auto* dts = new DallasTemperatureSensors(HALMET_PIN_1WIRE);

    // ---- Step 6: create OWT + SK outputs per assigned slot ----
    for (int i = 0; i < NUM_ONEWIRE_SLOTS; i++) {
        if (out.owDest[i] <= 0) {
            out.owSensors[i] = nullptr;
            continue;
        }

        String owCfg = "/onewire/sensor" + String(i) + "/address";
        out.owSensors[i] = new OneWireTemperature(dts, INTERVAL_1WIRE_MS, owCfg.c_str());

        int dest = out.owDest[i];
        String skPath;
        if (dest > 0 && dest < kNumTempDests && kTempDests[dest].skPath) {
            skPath = kTempDests[dest].skPath;
        } else {
            skPath = "environment.inside.temperature." + String(i);
        }
        auto* skOutput = new SKOutputFloat(skPath);
        out.owSensors[i]->connect_to(skOutput);
    }

    // ---- Step 7: periodic description updater + SK diagnostics ----
    auto* skDiag = new SKOutputRawJson(
        "design.halmet.diagnostics.onewireSensors", "");

    auto* sensorArr = out.owSensors;
    auto* destArr = out.owDest;

    event_loop()->onRepeat(INTERVAL_ONEWIRE_DIAG_MS, [skDiag, sensorArr, destArr]() {
        JsonDocument doc;
        JsonArray sensors = doc["sensors"].to<JsonArray>();

        for (auto& b : sBindings) {
            JsonObject obj = sensors.add<JsonObject>();
            char romBuf[24];
            formatAddr(romBuf, b.addr);
            obj["address"] = String(romBuf);

            String destLabel = b.pov->get();
            obj["dest"] = destLabel;
            obj["slot"] = b.slot;

            // Update config card description with live temp
            String desc;
            if (b.slot >= 0 && b.slot < NUM_ONEWIRE_SLOTS && sensorArr[b.slot]) {
                float tempK = sensorArr[b.slot]->get();
                if (!isnan(tempK) && tempK > 0) {
                    float tempC = tempK - 273.15f;
                    obj["tempK"] = serialized(String(tempK, 1));
                    desc = "Currently: " + String(tempC, 1) + " °C";
                    if (destLabel != kTempDests[0].label) {
                        desc += String(" — ") + destLabel;
                    }
                } else {
                    desc = "Waiting for reading";
                    if (destLabel != kTempDests[0].label) {
                        desc += String(" — ") + destLabel;
                    }
                }
            } else if (b.slot < 0) {
                int destIdx = destIndexByLabel(destLabel);
                if (destIdx == 0) {
                    desc = "Not assigned";
                } else {
                    desc = "Not assigned (all slots in use)";
                }
            }
            if (b.configItem) {
                b.configItem->set_description(desc);
            }
        }

        String output;
        serializeJson(doc, output);
        skDiag->set(output);
    });
}

}  // namespace onewire_setup
