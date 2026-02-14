// ============================================================
//  main.cpp  —  HALMET Marine Engine & Tank Monitor
//
//  Hardware:  Hat Labs HALMET (ESP32-WROOM-32E)
//  Engine:    Volvo Penta MD7A / Paris Rhone alternator
//  Framework: Arduino (PlatformIO)
//
//  Communication strategy:
//    Primary  → NMEA 2000 (engine/tank data via standard PGNs)
//    Fallback → WiFi / Signal K WebSocket (relay state, key sense)
//
//  Input map:
//    D1 / GPIO 23  → Alternator W-terminal RPM pulses
//    D2 / GPIO 25  → Oil pressure warning (active-low)
//    D3 / GPIO 27  → Coolant temperature warning (active-low)
//    D4 / GPIO 26  → Ignition key sense (+12 V present = ON) [optional]
//    A1 / ADS ch0  → VP coolant temp sender voltage (parallel to gauge)
//    A2 / ADS ch1  → Gobius Pro sensor A OUT1  ("below 3/4" threshold)
//    A3 / ADS ch2  → Gobius Pro sensor B OUT1  ("below 1/4" threshold)
//    1-Wire        → DS18B20 engine-room temperature probes
//    GPIO 32       → Bilge fan relay output
// ============================================================

#include <Arduino.h>

// --- SensESP ---
#include <sensesp.h>
#include <sensesp/sensors/sensor.h>
#include <sensesp/signalk/signalk_output.h>
#include <sensesp/system/lambda_consumer.h>
#include <sensesp_app_builder.h>

// --- NMEA 2000 ---
#include <NMEA2000_esp32.h>
#include <N2kMessages.h>

// --- Adafruit ADS1115 ---
#include <Adafruit_ADS1X15.h>

// --- Project modules ---
#include "halmet_config.h"
#include "BilgeFan.h"
#include "RpmSensor.h"
#include "OneWireSensors.h"
#include "N2kSenders.h"

using namespace sensesp;

// ============================================================
//  Global objects
// ============================================================
static tNMEA2000_esp32 gNmea2000;
static Adafruit_ADS1115 gAds;
static RpmSensor        gRpm(HALMET_PIN_D1);
static OneWireSensors   gOneWire(HALMET_PIN_1WIRE);
static BilgeFan         gBilgeFan(HALMET_PIN_RELAY, /*activeHigh=*/true);

// Runtime-configurable parameters (stored in NVS by SensESP)
// These objects expose themselves to the web config UI automatically.
static auto* gPurgeDurationSec  = new NumberConfig(DEFAULT_PURGE_DURATION_S,
                                                    "/bilge/purge_duration_s",
                                                    "Bilge fan purge duration (seconds)");
static auto* gPulsesPerRev      = new NumberConfig(DEFAULT_PULSES_PER_REVOLUTION,
                                                    "/rpm/pulses_per_rev",
                                                    "Alternator pulses per engine revolution");
static auto* gEngineRunningRpm  = new NumberConfig(DEFAULT_ENGINE_RUNNING_RPM,
                                                    "/rpm/running_threshold",
                                                    "RPM threshold: engine considered running");
static auto* gTankCapacityL     = new NumberConfig(DEFAULT_TANK_CAPACITY_L,
                                                    "/tank/capacity_l",
                                                    "Tank capacity (litres)");

// ============================================================
//  Engine-state debounce
// ============================================================
static bool    gEngineRunning      = false;
static bool    gEngineRunningRaw   = false;
static uint32_t gEngineStateMs     = 0;

static bool debounceEngineState(bool rawRunning) {
    if (rawRunning != gEngineRunningRaw) {
        gEngineRunningRaw = rawRunning;
        gEngineStateMs    = millis();
    }
    if ((millis() - gEngineStateMs) >= ENGINE_STATE_DEBOUNCE_MS) {
        gEngineRunning = gEngineRunningRaw;
    }
    return gEngineRunning;
}

// ============================================================
//  Voltage → Temperature curve (VP / VDO NTC sender)
//  Points: {voltage_V, temperature_C}
// ============================================================
struct CurvePoint { float v; float c; };
static const CurvePoint kTempCurve[] = { TEMP_CURVE_POINTS };
static constexpr int    kTempCurveLen = sizeof(kTempCurve) / sizeof(CurvePoint);

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
    return -1.0f;  // Should not reach here
}

// ============================================================
//  Signal K output producers (WiFi supplemental)
//  These fire whenever their source observable emits a value.
// ============================================================
static SKOutputBool* gSkFanState    = nullptr;
static SKOutputBool* gSkIgnState    = nullptr;

// ============================================================
//  N2K setup
// ============================================================
static void setupNmea2000() {
    gNmea2000.SetProductInformation(
        N2K_DEVICE_SERIAL,
        100,                          // product code
        N2K_MODEL_ID,
        "1.0.0",                      // firmware version
        "1.0.0"                       // model version
    );
    gNmea2000.SetDeviceInformation(
        123456789UL,                  // Unique device number (change per unit)
        160,                          // Device function: Engine Gateway
        25,                           // Device class: Inter/Intranetwork Device
        2046                          // Manufacturer code (use Nmea.org assigned)
    );
    gNmea2000.SetMode(tNMEA2000::N2km_NodeOnly, 23);
    gNmea2000.EnableForward(false);
    gNmea2000.Open();
}

// ============================================================
//  Arduino setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n[HALMET] Engine & Tank Monitor starting...");

    // --- Alarm inputs (active-low, external pull via HALMET) ---
    pinMode(HALMET_PIN_D2, INPUT_PULLUP);
    pinMode(HALMET_PIN_D3, INPUT_PULLUP);
    pinMode(HALMET_PIN_D4, INPUT_PULLUP);   // Ignition key sense

    // --- RPM sensor ---
    gRpm.begin();

    // --- Bilge fan relay ---
    gBilgeFan.begin();

    // --- ADS1115 ---
    Wire.begin();
    if (!gAds.begin(ADS1115_ADDRESS)) {
        Serial.println("[ERROR] ADS1115 not found — check I2C wiring!");
    }
    // 0–3.3 V range with PGA = ±4.096 V (gain 1) → ~0.125 mV/bit
    gAds.setGain(GAIN_ONE);
    gAds.setDataRate(RATE_ADS1115_8SPS);   // Slow rate for stable readings

    // --- DS18B20 1-Wire ---
    gOneWire.begin();

    // --- NMEA 2000 ---
    setupNmea2000();

    // --- SensESP (WiFi, web config, Signal K) ---
    SensESPAppBuilder builder;
    auto* sensesp_app = builder
        .set_hostname("halmet-engine")
        .set_wifi_client("your_ssid", "your_password")   // or use AP mode
        .set_sk_server("signalk-server-ip", 3000)
        .get_app();

    // --- Signal K outputs for data without N2K PGNs ---
    gSkFanState = new SKOutputBool(
        "electrical.switches.bilgeFan.state", "",
        new SKMetadata("Bilge fan", "Bilge fan purge active"));

    gSkIgnState = new SKOutputBool(
        "electrical.switches.ignition.state", "",
        new SKMetadata("Ignition key", "Ignition key present"));

    // Push initial values
    gSkFanState->set(false);
    gSkIgnState->set(false);

    // --- Bilge fan relay change → Signal K ---
    gBilgeFan.onRelayChange([](bool on) {
        if (gSkFanState) gSkFanState->set(on);
        Serial.printf("[BilgeFan] Relay -> %s\n", on ? "ON" : "OFF");
    });

    Serial.println("[HALMET] Setup complete.");
    sensesp_app->start();
}

// ============================================================
//  Arduino loop()
// ============================================================
void loop() {
    static uint32_t lastRpmMs     = 0;
    static uint32_t lastAnalogMs  = 0;
    static uint32_t lastAlarmMs   = 0;
    static uint32_t last1WireMs   = 0;
    static uint32_t last1WireWaitMs = 0;
    static bool     oneWirePending  = false;
    static uint32_t lastFanMs     = 0;
    static uint32_t lastN2kFastMs = 0;
    static uint32_t lastN2kSlowMs = 0;
    static uint32_t lastSkMs      = 0;

    uint32_t now = millis();

    // ---- Pump SensESP event loop ----
    app.tick();
    gNmea2000.ParseMessages();

    // --------------------------------------------------------
    //  RPM update  (every INTERVAL_RPM_MS)
    // --------------------------------------------------------
    if (now - lastRpmMs >= INTERVAL_RPM_MS) {
        lastRpmMs = now;
        gRpm.setPulsesPerRev(gPulsesPerRev->getValue());
        float rpm = gRpm.update();
        bool rawRunning = (rpm > gEngineRunningRpm->getValue());
        debounceEngineState(rawRunning);
    }

    // --------------------------------------------------------
    //  N2K fast PGN 127488 — Engine Rapid Update  (~10 Hz)
    // --------------------------------------------------------
    if (now - lastN2kFastMs >= 100) {
        lastN2kFastMs = now;
        N2kSenders::sendEngineRapidUpdate(gNmea2000,
                                          N2K_ENGINE_INSTANCE,
                                          gRpm.getRpm());
    }

    // --------------------------------------------------------
    //  Alarm inputs (D2 oil, D3 temp)  every 500 ms
    // --------------------------------------------------------
    if (now - lastAlarmMs >= INTERVAL_DIGITAL_ALARM_MS) {
        lastAlarmMs = now;
        // Active-low: LOW = alarm active
        bool oilAlarm  = (digitalRead(HALMET_PIN_D2) == LOW);
        bool tempAlarm = (digitalRead(HALMET_PIN_D3) == LOW);

        if (oilAlarm)  Serial.println("[ALARM] Oil pressure LOW");
        if (tempAlarm) Serial.println("[ALARM] Engine temperature HIGH");

        // Store for N2K slow sender below
        // (static locals — visible within this block only)
        static bool lastOilAlarm  = false;
        static bool lastTempAlarm = false;
        lastOilAlarm  = oilAlarm;
        lastTempAlarm = tempAlarm;
    }

    // --------------------------------------------------------
    //  Analog reads: temp sender + Gobius tanks  (every 200 ms)
    // --------------------------------------------------------
    if (now - lastAnalogMs >= INTERVAL_ANALOG_MS) {
        lastAnalogMs = now;

        // A1 — coolant temperature sender (passive voltage mode)
        int16_t raw0 = gAds.readADC_SingleEnded(0);
        float   volts0 = gAds.computeVolts(raw0);
        float   coolantC = voltageToCelsius(volts0);
        float   coolantK = coolantC + 273.15f;

        // A2 — Gobius sensor A: output sinks LOW when tank is below 3/4
        int16_t raw1     = gAds.readADC_SingleEnded(1);
        float   volts1   = gAds.computeVolts(raw1);
        bool    below3q  = (volts1 < GOBIUS_THRESHOLD_VOLTAGE);  // true = below 3/4

        // A3 — Gobius sensor B: output sinks LOW when tank is below 1/4
        int16_t raw2     = gAds.readADC_SingleEnded(2);
        float   volts2   = gAds.computeVolts(raw2);
        bool    below1q  = (volts2 < GOBIUS_THRESHOLD_VOLTAGE);  // true = below 1/4

        // Combine both thresholds into a single level estimate.
        // Note: below1q implies below3q; if hardware disagrees, trust below3q.
        float tankLevelPct;
        if (below1q) {
            // Tank is below 1/4 — both sensors should be triggered
            tankLevelPct = TANK_LEVEL_LOW_PCT;   // 12.5 %
        } else if (below3q) {
            // Tank is between 1/4 and 3/4 — only the 3/4 sensor is triggered
            tankLevelPct = TANK_LEVEL_MID_PCT;   // 50.0 %
        } else {
            // Tank is at or above 3/4 — neither sensor triggered
            tankLevelPct = TANK_LEVEL_HIGH_PCT;  // 87.5 %
        }

        // A4 — spare (battery voltage, optional)
        // int16_t raw3 = gAds.readADC_SingleEnded(3);
        // float volts3 = gAds.computeVolts(raw3) * (68.0f + 10.0f) / 10.0f; // divider

        // ---- N2K slow PGNs (1 Hz) ----
        if (now - lastN2kSlowMs >= 1000) {
            lastN2kSlowMs = now;

            // Read alarm state — captured 500 ms ago above
            bool oilAlarm  = (digitalRead(HALMET_PIN_D2) == LOW);
            bool tempAlarm = (digitalRead(HALMET_PIN_D3) == LOW);

            N2kSenders::sendEngineDynamic(gNmea2000,
                                          N2K_ENGINE_INSTANCE,
                                          coolantK,
                                          N2kDoubleNA,    // oil pressure (Pa) — binary only
                                          oilAlarm,
                                          tempAlarm);

            // Single tank, single PGN 127505 message (instance 0).
            // Level is derived from two Gobius threshold sensors (see above).
            N2kSenders::sendFluidLevel(gNmea2000,
                                       0,                            // tank instance 0
                                       N2kft_Diesel,                 // adjust to actual fluid type
                                       tankLevelPct,
                                       gTankCapacityL->getValue());
        }
    }

    // --------------------------------------------------------
    //  DS18B20 1-Wire sensors  (request every 10 s, read after 800 ms)
    // --------------------------------------------------------
    if (!oneWirePending && (now - last1WireMs >= INTERVAL_1WIRE_MS)) {
        last1WireMs      = now;
        last1WireWaitMs  = now;
        gOneWire.requestAll();
        oneWirePending = true;
    }
    if (oneWirePending && (now - last1WireWaitMs >= 800)) {
        oneWirePending = false;
        for (int i = 0; i < gOneWire.count(); i++) {
            float tempK = gOneWire.getTemperatureK(i);
            if (tempK > 0.0f) {
                N2kSenders::sendTemperatureExtended(
                    gNmea2000,
                    i,                           // sensor instance
                    N2kts_EngineRoomTemperature,
                    tempK);
            }
        }
    }

    // --------------------------------------------------------
    //  Bilge fan state machine tick  (every 1 s)
    // --------------------------------------------------------
    if (now - lastFanMs >= INTERVAL_FAN_MS) {
        lastFanMs = now;
        gBilgeFan.update(gEngineRunning, gPurgeDurationSec->getValue());
    }

    // --------------------------------------------------------
    //  Signal K supplemental outputs  (every 5 s)
    //  For data with no suitable NMEA 2000 PGN
    // --------------------------------------------------------
    if (now - lastSkMs >= 5000) {
        lastSkMs = now;

        // Bilge fan relay state (published via onRelayChange callback too)
        if (gSkFanState) gSkFanState->set(gBilgeFan.relayOn());

        // Ignition key sense (D4 — optional)
        bool ignitionOn = (digitalRead(HALMET_PIN_D4) == HIGH);
        if (gSkIgnState) gSkIgnState->set(ignitionOn);
    }
}
