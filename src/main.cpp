// ============================================================
//  main.cpp  —  HALMET Marine Engine & Tank Monitor
//
//  Hardware:  Hat Labs HALMET (ESP32-WROOM-32E)
//  Engine:    Volvo Penta MD7A / Paris Rhone alternator
//  Framework: Arduino + SensESP v3 (PlatformIO)
//
//  Communication strategy:
//    Primary  → NMEA 2000 (engine/tank data via standard PGNs)
//    Fallback → WiFi / Signal K WebSocket (relay state, key sense)
//
//  SensESP v3 changes from v2:
//    - No global ReactESP app object; use event_loop() helper instead
//    - No sensesp_app->start() — removed in v3
//    - NumberConfig replaced by PersistingObservableValue + ConfigItem<T>
//    - set_wifi() is deprecated; use set_wifi_client()
//    - Builder methods return SensESPAppBuilder* (use -> not .)
//    - loop() body is just event_loop()->tick()
//    - SetupLogging() replaces SetupSerialDebug()
//
//  Input map:
//    D1 / GPIO 23  → Alternator W-terminal RPM pulses
//    D2 / GPIO 25  → Oil pressure warning (active-low)
//    D3 / GPIO 27  → Coolant temperature warning (active-low)
//    D4 / GPIO 26  → Ignition key sense (+12 V present = ON) [optional]
//    A1 / ADS ch0  → VP coolant temp sender voltage (parallel to gauge)
//    A2 / ADS ch1  → Gobius Pro sensor A OUT1 ("below 3/4" threshold)
//    A3 / ADS ch2  → Gobius Pro sensor B OUT1 ("below 1/4" threshold)
//    1-Wire        → DS18B20 engine-room temperature probes
//    GPIO 32       → Bilge fan relay output
// ============================================================

#include <Arduino.h>

// --- SensESP v3 ---
#include <sensesp.h>
#include <sensesp_app_builder.h>
#include <sensesp/ui/config_item.h>
//#include <sensesp/system/persisting_observablevalue.h>
#include <sensesp/signalk/signalk_output.h>

// --- NMEA 2000 ---
#include <NMEA2000_esp32.h>
#include <N2kMessages.h>

// --- Adafruit ADS1115 ---
#include <Adafruit_ADS1X15.h>

// --- SensESP/OneWire library (github.com/SensESP/OneWire) ---
#include <sensesp_onewire/onewire_temperature.h>

// --- Project modules ---
#include "secrets.h"
#include "halmet_config.h"
#include "BilgeFan.h"
#include "RpmSensor.h"
#include "N2kSenders.h"

using namespace sensesp;
using namespace sensesp::onewire;

// ============================================================
//  Global hardware objects
// ============================================================
static tNMEA2000_esp32  gNmea2000;
static Adafruit_ADS1115 gAds;
static RpmSensor        gRpm(HALMET_PIN_D1);
static BilgeFan         gBilgeFan(HALMET_PIN_RELAY, /*activeHigh=*/true);

// ============================================================
//  Configurable runtime parameters
//  PersistingObservableValue<T> stores the value to LittleFS so it
//  survives reboots. ConfigItem<T> registers a web-UI config card.
// ============================================================
static PersistingObservableValue<float>* gPurgeDurationSec = nullptr;
static PersistingObservableValue<float>* gPulsesPerRev     = nullptr;
static PersistingObservableValue<float>* gEngineRunningRpm = nullptr;
static PersistingObservableValue<float>* gTankCapacityL    = nullptr;

// ============================================================
//  Shared state (written by sensor callbacks, read by N2K senders)
// ============================================================
static float gCoolantK     = 273.15f;
static bool  gOilAlarm     = false;
static bool  gTempAlarm    = false;
static float gTankLevelPct = TANK_LEVEL_HIGH_PCT;

// ============================================================
//  Engine-state debounce
// ============================================================
static bool     gEngineRunning    = false;
static bool     gEngineRunningRaw = false;
static uint32_t gEngineStateMs    = 0;

static void updateEngineState(bool rawRunning) {
    if (rawRunning != gEngineRunningRaw) {
        gEngineRunningRaw = rawRunning;
        gEngineStateMs    = millis();
    }
    if ((millis() - gEngineStateMs) >= ENGINE_STATE_DEBOUNCE_MS) {
        gEngineRunning = gEngineRunningRaw;
    }
}

// ============================================================
//  Voltage → Temperature curve interpolation (VP / VDO NTC sender)
// ============================================================
struct CurvePoint { float v; float c; };
static const CurvePoint kTempCurve[] = { TEMP_CURVE_POINTS };
static constexpr int kTempCurveLen = sizeof(kTempCurve) / sizeof(CurvePoint);

static float voltageToCelsius(float volt) {
    if (volt <= kTempCurve[kTempCurveLen - 1].v) return kTempCurve[kTempCurveLen - 1].c;
    if (volt >= kTempCurve[0].v)                 return kTempCurve[0].c;
    for (int i = 0; i < kTempCurveLen - 1; i++) {
        if (volt <= kTempCurve[i].v && volt > kTempCurve[i + 1].v) {
            float ratio = (volt - kTempCurve[i + 1].v)
                        / (kTempCurve[i].v - kTempCurve[i + 1].v);
            return kTempCurve[i + 1].c + ratio * (kTempCurve[i].c - kTempCurve[i + 1].c);
        }
    }
    return -1.0f;
}

// ============================================================
//  Signal K outputs (WiFi, for data with no NMEA 2000 PGN)
// ============================================================
static SKOutputBool* gSkFanState = nullptr;
static SKOutputBool* gSkIgnState = nullptr;

// ============================================================
//  NMEA 2000 setup
// ============================================================
static void setupNmea2000() {
    gNmea2000.SetProductInformation(N2K_DEVICE_SERIAL, 100, N2K_MODEL_ID,
                                    "1.0.0", "1.0.0");
    gNmea2000.SetDeviceInformation(123456789UL, 160, 25, 2046);
    gNmea2000.SetMode(tNMEA2000::N2km_NodeOnly, 23);
    gNmea2000.EnableForward(false);
    gNmea2000.Open();
}

// ============================================================
//  Arduino setup()
// ============================================================
void setup() {
    // SensESP v3 logging init (replaces SetupSerialDebug)
    SetupLogging();

    // --- Digital inputs ---
    pinMode(HALMET_PIN_D2, INPUT_PULLUP);   // Oil pressure warning (active-low)
    pinMode(HALMET_PIN_D3, INPUT_PULLUP);   // Temperature warning  (active-low)
    pinMode(HALMET_PIN_D4, INPUT_PULLUP);   // Ignition key sense

    // --- RPM pulse counter ---
    gRpm.begin();

    // --- Bilge fan relay ---
    gBilgeFan.begin();

    // --- ADS1115 ADC ---
    Wire.begin();
    if (!gAds.begin(ADS1115_ADDRESS)) {
        ESP_LOGE("HALMET", "ADS1115 not found — check I2C wiring!");
    }
    gAds.setGain(GAIN_ONE);           // ±4.096 V range, 0.125 mV/bit
    gAds.setDataRate(RATE_ADS1115_8SPS);

    // --- NMEA 2000 ---
    setupNmea2000();

    // --- SensESP v3 app builder ---
    // set_wifi_client(ssid, password): if omitted, WifiManager creates a config AP.
    // set_sk_server(): if omitted, mDNS discovery is used.
    // No ->get_app() assignment needed unless you need the app pointer later.
    // No sensesp_app->start() — removed in v3.
    SensESPAppBuilder builder;
    builder.set_hostname("halmet-engine")
           ->set_wifi_client(WIFI_SSID, WIFI_PASSWORD)
           ->set_sk_server(SK_SERVER_IP, SK_SERVER_PORT)
           ->get_app();

    // --- Configurable parameters (web UI + persisted to flash) ---
    gPurgeDurationSec = new PersistingObservableValue<float>(
        DEFAULT_PURGE_DURATION_S, "/bilge/purge_duration_s");
    ConfigItem(gPurgeDurationSec)
        ->set_title("Bilge fan purge duration (s)");

    gPulsesPerRev = new PersistingObservableValue<float>(
        DEFAULT_PULSES_PER_REVOLUTION, "/rpm/pulses_per_rev");
    ConfigItem(gPulsesPerRev)
        ->set_title("Alternator pulses per engine revolution");

    gEngineRunningRpm = new PersistingObservableValue<float>(
        DEFAULT_ENGINE_RUNNING_RPM, "/rpm/running_threshold");
    ConfigItem(gEngineRunningRpm)
        ->set_title("RPM threshold: engine considered running");

    gTankCapacityL = new PersistingObservableValue<float>(
        DEFAULT_TANK_CAPACITY_L, "/tank/capacity_l");
    ConfigItem(gTankCapacityL)
        ->set_title("Tank capacity (litres)");

    // --- Signal K outputs for data with no NMEA 2000 PGN ---
    gSkFanState = new SKOutputBool("electrical.switches.bilgeFan.state", "",
                                   new SKMetadata("Bilge fan", "Bilge fan purge active"));
    gSkIgnState = new SKOutputBool("electrical.switches.ignition.state", "",
                                   new SKMetadata("Ignition key", "Ignition key present"));
    gSkFanState->set(false);
    gSkIgnState->set(false);

    // Relay state change callback → Signal K
    gBilgeFan.onRelayChange([](bool on) {
        if (gSkFanState) gSkFanState->set(on);
        ESP_LOGI("BilgeFan", "Relay -> %s", on ? "ON" : "OFF");
    });

    // --- DS18B20 1-Wire via SensESP/OneWire ---
    // DallasTemperatureSensors discovers all sensors on the bus at init.
    // Each OneWireTemperature is a SensESP producer; the web UI allows
    // mapping physical sensor ROM addresses to logical slots.
    auto* dts = new DallasTemperatureSensors(HALMET_PIN_1WIRE);
    for (int i = 0; i < 6; i++) {
        String cfg = "/onewire/sensor" + String(i);
        String sk  = "environment.inside.engineRoom.temperature." + String(i);
        // OneWireTemperature outputs temperature in Kelvin
        auto* ow = new OneWireTemperature(dts, INTERVAL_1WIRE_MS, cfg.c_str());
        ow->connect_to(new SKOutputFloat(sk));
        // N2K PGN 130316 is sent from the polling event below using
        // the raw K value; for simplicity we re-poll the bus there.
    }

    // -----------------------------------------------------------------------
    //  Periodic work registered as ReactESP events.
    //  All timing is handled by the SensESP event loop — no millis() needed.
    // -----------------------------------------------------------------------

    // RPM counter + N2K PGN 127488 (100 ms / 10 Hz)
    event_loop()->onRepeat(INTERVAL_RPM_MS, []() {
        gRpm.setPulsesPerRev(gPulsesPerRev->get());
        float rpm = gRpm.update();
        updateEngineState(rpm > gEngineRunningRpm->get());
        N2kSenders::sendEngineRapidUpdate(gNmea2000, N2K_ENGINE_INSTANCE, rpm);
    });

    // Analog reads: coolant temp + Gobius tank thresholds (200 ms)
    event_loop()->onRepeat(INTERVAL_ANALOG_MS, []() {
        // A1 — VP coolant temperature sender (passive voltage)
        float volts0 = gAds.computeVolts(gAds.readADC_SingleEnded(0));
        gCoolantK = voltageToCelsius(volts0) + 273.15f;

        // A2 — Gobius sensor A (below 3/4 threshold)
        bool below3q = gAds.computeVolts(gAds.readADC_SingleEnded(1))
                       < GOBIUS_THRESHOLD_VOLTAGE;
        // A3 — Gobius sensor B (below 1/4 threshold)
        bool below1q = gAds.computeVolts(gAds.readADC_SingleEnded(2))
                       < GOBIUS_THRESHOLD_VOLTAGE;

        if (below1q)      gTankLevelPct = TANK_LEVEL_LOW_PCT;   // 12.5 %
        else if (below3q) gTankLevelPct = TANK_LEVEL_MID_PCT;   // 50.0 %
        else              gTankLevelPct = TANK_LEVEL_HIGH_PCT;   // 87.5 %
    });

    // Alarm digital inputs (500 ms)
    event_loop()->onRepeat(INTERVAL_DIGITAL_ALARM_MS, []() {
        gOilAlarm  = (digitalRead(HALMET_PIN_D2) == LOW);
        gTempAlarm = (digitalRead(HALMET_PIN_D3) == LOW);
        if (gOilAlarm)  ESP_LOGW("HALMET", "Oil pressure LOW");
        if (gTempAlarm) ESP_LOGW("HALMET", "Engine temperature HIGH");
    });

    // N2K slow PGNs: PGN 127489 + PGN 127505 (1 s)
    event_loop()->onRepeat(1000, []() {
        N2kSenders::sendEngineDynamic(gNmea2000, N2K_ENGINE_INSTANCE,
                                      gCoolantK, N2kDoubleNA,
                                      gOilAlarm, gTempAlarm);
        // N2kft_Diesel may be absent in older library versions.
        // The closest available alternative is N2kft_Oil.
        // Upgrade the NMEA2000-library if you need N2kft_Diesel.
        N2kSenders::sendFluidLevel(gNmea2000, 0, N2kft_Oil,
                                   gTankLevelPct, gTankCapacityL->get());
    });

    // Bilge fan state machine tick (1 s)
    event_loop()->onRepeat(INTERVAL_FAN_MS, []() {
        gBilgeFan.update(gEngineRunning, gPurgeDurationSec->get());
    });

    // Signal K supplemental data (5 s)
    event_loop()->onRepeat(5000, []() {
        if (gSkFanState) gSkFanState->set(gBilgeFan.relayOn());
        if (gSkIgnState) gSkIgnState->set(digitalRead(HALMET_PIN_D4) == HIGH);
    });

    // NMEA 2000 message pump (every 1 ms — must be fast)
    event_loop()->onRepeat(1, []() {
        gNmea2000.ParseMessages();
    });

    ESP_LOGI("HALMET", "Setup complete.");
}

// ============================================================
//  Arduino loop() — SensESP v3: just tick the event loop
// ============================================================
void loop() {
    event_loop()->tick();
}
