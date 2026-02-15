// ============================================================
//  onewire_setup.cpp — 1-Wire temperature destination table + setup
//
//  Sprint 6: ROM-based sensor selection (bus scan + picker)
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

// ============================================================
//  File-scope state: detected ROM addresses from bus scan
// ============================================================
static std::vector<OWDevAddr> sDetectedAddrs;

// ============================================================
//  Helper: format OWDevAddr as "28:ff:91:15:07:00:00:ca"
// ============================================================
static void formatAddr(char* buf, const OWDevAddr& addr) {
    sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
            addr[0], addr[1], addr[2], addr[3],
            addr[4], addr[5], addr[6], addr[7]);
}

// ============================================================
//  Independent bus scan (before DallasTemperatureSensors)
//
//  Creates a temporary OneWireNg scanner in a scoped block,
//  collects ROM addresses, then destroys it so DTS can claim
//  the same pin later.
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
    // ---- Step 1: scan bus independently ----
    scanBus();

    // ---- Step 2: per-slot picker + dest config (before DTS) ----
    // Build a label suffix showing detected addresses for the picker title
    String addrList;
    for (size_t i = 0; i < sDetectedAddrs.size(); i++) {
        char buf[24];
        formatAddr(buf, sDetectedAddrs[i]);
        if (i > 0) addrList += ", ";
        addrList += String((int)i) + "=" + buf;
    }

    for (int i = 0; i < NUM_ONEWIRE_SLOTS; i++) {
        // --- Address picker ---
        String pickerCfg = "/onewire/sensor" + String(i) + "/address_picker";
        auto* picker = new PersistingObservableValue<int>(-1, pickerCfg);
        String pickerTitle = "Sensor " + String(i) + " address (-1=auto";
        if (addrList.length() > 0) {
            pickerTitle += ", " + addrList;
        }
        pickerTitle += ")";
        ConfigItem(picker)->set_title(pickerTitle.c_str());

        // If picker has a valid selection, pre-write the ROM address
        int pick = picker->get();
        if (pick >= 0 && pick < (int)sDetectedAddrs.size()) {
            char romBuf[24];
            formatAddr(romBuf, sDetectedAddrs[pick]);
            String owCfg = "/onewire/sensor" + String(i) + "/address";
            OwAddressPrewriter(owCfg, String(romBuf)).save();
            ESP_LOGI("1Wire", "Slot %d: pre-wrote ROM %s from picker index %d", i, romBuf, pick);
            // Reset picker to -1 (one-shot)
            picker->set(-1);
        }

        // --- Destination selector (existing) ---
        String destCfg = "/onewire/sensor" + String(i) + "/dest";
        out.owDest[i] = new PersistingObservableValue<int>(
            DEFAULT_ONEWIRE_DEST, destCfg);
        ConfigItem(out.owDest[i])
            ->set_title(("Sensor " + String(i) + " dest (0=off,1=engRoom,2=exhaust,3=sea,4=outside,5=cabin,6=fridge,7=freezer,8=alt,9=oil,10=intake,11=block)").c_str());
    }

    // ---- Step 3: create DallasTemperatureSensors (scans bus again) ----
    auto* dts = new DallasTemperatureSensors(HALMET_PIN_1WIRE);

    // ---- Step 4: create OneWireTemperature + SK outputs per slot ----
    for (int i = 0; i < NUM_ONEWIRE_SLOTS; i++) {
        String owCfg = "/onewire/sensor" + String(i) + "/address";
        out.owSensors[i] = new OneWireTemperature(dts, INTERVAL_1WIRE_MS, owCfg.c_str());

        // SK output path depends on configured destination
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

    // ---- Step 5: periodic SK diagnostic output (17a) ----
    auto* skDiag = new SKOutputRawJson(
        "design.halmet.diagnostics.onewireSensors", "");

    // Capture pointers with static lifetime for the lambda
    auto* destArr = out.owDest;
    auto* sensorArr = out.owSensors;

    event_loop()->onRepeat(INTERVAL_ONEWIRE_DIAG_MS, [skDiag, destArr, sensorArr]() {
        JsonDocument doc;

        // Detected addresses
        JsonArray detected = doc["detected"].to<JsonArray>();
        for (size_t i = 0; i < sDetectedAddrs.size(); i++) {
            char buf[24];
            formatAddr(buf, sDetectedAddrs[i]);
            detected.add(String(buf));
        }

        // Slot bindings
        JsonArray slots = doc["slots"].to<JsonArray>();
        for (int i = 0; i < NUM_ONEWIRE_SLOTS; i++) {
            JsonObject slot = slots.add<JsonObject>();
            slot["index"] = i;

            // Get the OWT's persisted address by serializing its config
            JsonDocument addrDoc;
            JsonObject addrObj = addrDoc.to<JsonObject>();
            if (sensorArr[i]->to_json(addrObj) && addrObj["address"].is<const char*>()) {
                slot["address"] = addrObj["address"].as<String>();
            } else {
                slot["address"] = "unknown";
            }

            int destIdx = destArr[i]->get();
            if (destIdx >= 0 && destIdx < kNumTempDests) {
                slot["dest"] = kTempDests[destIdx].label;
            } else {
                slot["dest"] = "unknown";
            }

            float tempK = sensorArr[i]->get();
            if (!isnan(tempK) && tempK > 0) {
                slot["tempK"] = serialized(String(tempK, 1));
            }
        }

        String output;
        serializeJson(doc, output);
        skDiag->set(output);
    });
}

}  // namespace onewire_setup
