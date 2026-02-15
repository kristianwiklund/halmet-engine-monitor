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
#include <cmath>
#include <esp_system.h>

// --- SensESP v3 ---
#include <sensesp.h>
#include <sensesp_app_builder.h>
#include <sensesp/ui/config_item.h>
//#include <sensesp/system/persisting_observablevalue.h>
#include <sensesp/signalk/signalk_output.h>

// --- NMEA 2000 ---
#include <ArduinoOTA.h>
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
static PersistingObservableValue<float>* gCoolantWarnC    = nullptr;
static PersistingObservableValue<float>* gCoolantAlarmC   = nullptr;

// ============================================================
//  Shared state (written by sensor callbacks, read by N2K senders)
// ============================================================
static double gCoolantK    = N2kDoubleNA;
static bool  gOilAlarm     = false;
static bool  gTempAlarm    = false;
static float gTankLevelPct = TANK_LEVEL_HIGH_PCT;

// ============================================================
//  ADS1115 status (file-scope so retry callback can see it)
// ============================================================
static bool gAdsOk = false;

// ============================================================
//  Alarm input debounce (shift-register, 4-of-5 majority vote)
// ============================================================
static uint8_t gOilAlarmHistory  = 0;   // bit-packed last N samples
static uint8_t gTempAlarmHistory = 0;

// ============================================================
//  Stale data guard — coolant temperature
// ============================================================
static uint32_t gCoolantLastUpdateMs = 0;

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
    // Out-of-range voltage → sensor fault (open/shorted)
    if (volt < COOLANT_VOLT_MIN_V || volt > COOLANT_VOLT_MAX_V) return NAN;

    if (volt <= kTempCurve[kTempCurveLen - 1].v) return kTempCurve[kTempCurveLen - 1].c;
    if (volt >= kTempCurve[0].v)                 return kTempCurve[0].c;
    for (int i = 0; i < kTempCurveLen - 1; i++) {
        if (volt <= kTempCurve[i].v && volt > kTempCurve[i + 1].v) {
            float ratio = (volt - kTempCurve[i + 1].v)
                        / (kTempCurve[i].v - kTempCurve[i + 1].v);
            return kTempCurve[i + 1].c + ratio * (kTempCurve[i].c - kTempCurve[i + 1].c);
        }
    }
    return NAN;
}

// ============================================================
//  Signal K outputs (WiFi, for data with no NMEA 2000 PGN)
// ============================================================
static SKOutputBool* gSkFanState = nullptr;
static SKOutputBool* gSkIgnState = nullptr;
static SKOutputRawJson* gSkCoolantNotification = nullptr;

// ============================================================
//  1-Wire → N2K/SK temperature destination lookup table
// ============================================================
struct TempDestination {
    const char* label;     // web UI display name
    int         n2kSource; // tN2kTempSource enum, or -1 for SK-only / disabled
    const char* skPath;    // SK path, or nullptr for raw sensor index
};

static const TempDestination kTempDests[] = {
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
static constexpr int kNumTempDests = sizeof(kTempDests) / sizeof(TempDestination);

// ============================================================
//  1-Wire sensor/config/output arrays
// ============================================================
static OneWireTemperature*             gOwSensors[NUM_ONEWIRE_SLOTS]  = {};
static PersistingObservableValue<int>* gOwDest[NUM_ONEWIRE_SLOTS]     = {};
static SKOutputFloat*                  gOwSkOutput[NUM_ONEWIRE_SLOTS] = {};

// ============================================================
//  Diagnostics counters
// ============================================================
static uint32_t gAdsFailCount = 0;

// ============================================================
//  Coolant alert state (avoid repeated notifications)
// ============================================================
static enum { COOLANT_NORMAL, COOLANT_WARN, COOLANT_ALARM } gCoolantAlertState = COOLANT_NORMAL;

// ============================================================
//  NMEA 2000 setup
// ============================================================
static void setupNmea2000() {
    gNmea2000.SetProductInformation(N2K_DEVICE_SERIAL, 100, N2K_MODEL_ID,
                                    FW_VERSION_STR, "1.0.0");
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

    // --- I2C bus ---
    Wire.setTimeOut(100);             // 100 ms I2C timeout (bus recovery)
    Wire.begin(HALMET_PIN_SDA, HALMET_PIN_SCL);
    Wire.setClock(400000);

    // --- ADS1115 ADC (ADDR tied to VCC → 0x4B) ---
    gAdsOk = gAds.begin(ADS1115_I2C_ADDRESS, &Wire);
    if (gAdsOk) {
        gAds.setGain(GAIN_ONE);           // ±4.096 V range, 0.125 mV/bit
        gAds.setDataRate(RATE_ADS1115_8SPS);
        ESP_LOGI("HALMET", "ADS1115 found at 0x%02X", ADS1115_I2C_ADDRESS);
    } else {
        gAdsFailCount++;
        ESP_LOGE("HALMET", "ADS1115 not found at 0x4B — will retry");
    }

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

    gCoolantWarnC = new PersistingObservableValue<float>(
        DEFAULT_COOLANT_WARN_C, "/coolant/warn_threshold_c");
    ConfigItem(gCoolantWarnC)
        ->set_title("Coolant warning threshold (°C)");

    gCoolantAlarmC = new PersistingObservableValue<float>(
        DEFAULT_COOLANT_ALARM_C, "/coolant/alarm_threshold_c");
    ConfigItem(gCoolantAlarmC)
        ->set_title("Coolant alarm threshold (°C)");

    // --- Signal K outputs for data with no NMEA 2000 PGN ---
    gSkFanState = new SKOutputBool("electrical.switches.bilgeFan.state", "",
                                   new SKMetadata("Bilge fan", "Bilge fan purge active"));
    gSkIgnState = new SKOutputBool("electrical.switches.ignition.state", "",
                                   new SKMetadata("Ignition key", "Ignition key present"));
    gSkFanState->set(false);
    gSkIgnState->set(false);

    gSkCoolantNotification = new SKOutputRawJson(
        "notifications.propulsion.0.coolantTemperature", "");

    // --- OTA safety: force relay OFF before firmware write begins ---
    // Deferred so it runs after SensESP's OTA handler (if ever enabled).
    // ArduinoOTA stores a single onStart pointer — this replaces SensESP's
    // log-only handler, which is an acceptable trade-off for relay safety.
    event_loop()->onDelay(0, []() {
        ArduinoOTA.onStart([]() {
            gBilgeFan.forceOff();
            ESP_LOGW("HALMET", "OTA starting — relay forced OFF");
        });
    });

    // Relay state change callback → Signal K
    gBilgeFan.onRelayChange([](bool on) {
        if (gSkFanState) gSkFanState->set(on);
        ESP_LOGI("BilgeFan", "Relay -> %s", on ? "ON" : "OFF");
    });

    // --- DS18B20 1-Wire via SensESP/OneWire ---
    // Each slot gets a configurable destination (N2K+SK, SK-only, or raw).
    // Changing the destination in the web UI requires a reboot.
    auto* dts = new DallasTemperatureSensors(HALMET_PIN_1WIRE);
    for (int i = 0; i < NUM_ONEWIRE_SLOTS; i++) {
        // Configurable destination per sensor slot
        String destCfg = "/onewire/sensor" + String(i) + "/dest";
        gOwDest[i] = new PersistingObservableValue<int>(
            DEFAULT_ONEWIRE_DEST, destCfg);
        ConfigItem(gOwDest[i])
            ->set_title(("Sensor " + String(i) + " dest (0=off,1=engRoom,2=exhaust,3=sea,4=outside,5=cabin,6=fridge,7=freezer,8=alt,9=oil,10=intake,11=block)").c_str());

        // OneWireTemperature reads in Kelvin; ROM address auto-discovered, configurable via web UI
        String owCfg = "/onewire/sensor" + String(i) + "/address";
        gOwSensors[i] = new OneWireTemperature(dts, INTERVAL_1WIRE_MS, owCfg.c_str());

        // SK output — path depends on configured destination
        int dest = gOwDest[i]->get();
        String skPath;
        if (dest > 0 && dest < kNumTempDests && kTempDests[dest].skPath) {
            skPath = String(kTempDests[dest].skPath) + "." + String(i);
        } else {
            skPath = "environment.inside.temperature." + String(i);
        }
        gOwSkOutput[i] = new SKOutputFloat(skPath);
        gOwSensors[i]->connect_to(gOwSkOutput[i]);
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
        if (!gAdsOk) return;
        // A1 — VP coolant temperature sender (passive voltage)
        float volts0 = gAds.computeVolts(gAds.readADC_SingleEnded(0));
        float celsius = voltageToCelsius(volts0);
        if (std::isnan(celsius)) {
            gCoolantK = N2kDoubleNA;        // sensor fault → MFD shows blank
        } else {
            gCoolantK = celsius + 273.15f;
            gCoolantLastUpdateMs = millis(); // mark valid reading

            // Coolant threshold alerting → Signal K notification
            float warnC  = gCoolantWarnC  ? gCoolantWarnC->get()  : DEFAULT_COOLANT_WARN_C;
            float alarmC = gCoolantAlarmC ? gCoolantAlarmC->get() : DEFAULT_COOLANT_ALARM_C;
            auto  newState = COOLANT_NORMAL;
            if (celsius >= alarmC)      newState = COOLANT_ALARM;
            else if (celsius >= warnC)  newState = COOLANT_WARN;

            if (newState != gCoolantAlertState) {
                gCoolantAlertState = newState;
                if (gSkCoolantNotification) {
                    if (newState == COOLANT_NORMAL) {
                        gSkCoolantNotification->set("null");
                    } else {
                        const char* state = (newState == COOLANT_ALARM) ? "alarm" : "warn";
                        char buf[192];
                        snprintf(buf, sizeof(buf),
                            "{\"state\":\"%s\",\"method\":[\"visual\",\"sound\"],"
                            "\"message\":\"Coolant %.0f°C (%s threshold)\"}",
                            state, celsius, state);
                        gSkCoolantNotification->set(String(buf));
                    }
                }
            }
        }

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

    // Alarm digital inputs with debounce (500 ms)
    // Shift-register majority vote: alarm asserts only when
    // ALARM_DEBOUNCE_THRESHOLD of the last ALARM_DEBOUNCE_SAMPLES agree.
    event_loop()->onRepeat(INTERVAL_DIGITAL_ALARM_MS, []() {
        constexpr uint8_t mask = (1 << ALARM_DEBOUNCE_SAMPLES) - 1;

        gOilAlarmHistory  = ((gOilAlarmHistory  << 1) | (digitalRead(HALMET_PIN_D2) == LOW)) & mask;
        gTempAlarmHistory = ((gTempAlarmHistory << 1) | (digitalRead(HALMET_PIN_D3) == LOW)) & mask;

        gOilAlarm  = (__builtin_popcount(gOilAlarmHistory)  >= ALARM_DEBOUNCE_THRESHOLD);
        gTempAlarm = (__builtin_popcount(gTempAlarmHistory) >= ALARM_DEBOUNCE_THRESHOLD);
    });

    // ADS1115 I2C recovery (retry when not present)
    event_loop()->onRepeat(INTERVAL_ADS_RETRY_MS, []() {
        if (gAdsOk) return;
        Wire.begin(HALMET_PIN_SDA, HALMET_PIN_SCL);
        Wire.setClock(400000);
        gAdsOk = gAds.begin(ADS1115_I2C_ADDRESS, &Wire);
        if (gAdsOk) {
            gAds.setGain(GAIN_ONE);
            gAds.setDataRate(RATE_ADS1115_8SPS);
            ESP_LOGI("HALMET", "ADS1115 recovered on I2C retry");
        } else {
            gAdsFailCount++;
        }
    });

    // N2K slow PGNs: PGN 127489 + PGN 127505 (1 s)
    event_loop()->onRepeat(1000, []() {
        // Stale data guard: send NA if no valid coolant reading ever, or >5 s stale
        double coolantToSend = gCoolantK;
        if (gCoolantLastUpdateMs == 0 ||
            (millis() - gCoolantLastUpdateMs) > STALE_DATA_TIMEOUT_MS) {
            coolantToSend = N2kDoubleNA;
        }
        N2kSenders::sendEngineDynamic(gNmea2000, N2K_ENGINE_INSTANCE,
                                      coolantToSend, N2kDoubleNA,
                                      gOilAlarm, gTempAlarm);
        N2kSenders::sendFluidLevel(gNmea2000, 0, N2kft_Fuel,
                                   gTankLevelPct, gTankCapacityL->get());
    });

    // 1-Wire → N2K PGN 130316 (10 s, matching 1-Wire read interval)
    event_loop()->onRepeat(INTERVAL_ONEWIRE_N2K_MS, []() {
        for (int i = 0; i < NUM_ONEWIRE_SLOTS; i++) {
            if (!gOwDest[i] || !gOwSensors[i]) continue;
            int dest = gOwDest[i]->get();
            if (dest <= 0 || dest >= kNumTempDests) continue;
            int n2kSrc = kTempDests[dest].n2kSource;
            if (n2kSrc < 0) continue;  // SK-only destination, no N2K
            float tempK = gOwSensors[i]->get();
            if (std::isnan(tempK) || tempK <= 0) continue;
            N2kSenders::sendTemperatureExtended(
                gNmea2000, i,
                static_cast<tN2kTempSource>(n2kSrc),
                tempK);
        }
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

    // Diagnostics heartbeat → Signal K (10 s)
    static auto* skDiagUptime    = new SKOutputFloat("design.halmet.diagnostics.uptimeSeconds", "");
    static auto* skDiagVersion   = new SKOutputString("design.halmet.diagnostics.firmwareVersion", "");
    static auto* skDiagAdsFails  = new SKOutputInt("design.halmet.diagnostics.adsFailCount", "");
    static auto* skDiagResetCode = new SKOutputInt("design.halmet.diagnostics.lastResetReason", "");

    skDiagVersion->set(FW_VERSION_STR);

    event_loop()->onRepeat(INTERVAL_DIAG_MS, []() {
        skDiagUptime->set(millis() / 1000.0f);
        skDiagAdsFails->set(static_cast<int>(gAdsFailCount));
        skDiagResetCode->set(static_cast<int>(esp_reset_reason()));
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
