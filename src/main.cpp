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
#include <sensesp/signalk/signalk_output.h>

// --- NMEA 2000 ---
#include <ArduinoOTA.h>
#include <NMEA2000_esp32.h>

// --- Adafruit ADS1115 ---
#include <Adafruit_ADS1X15.h>

// --- Project modules ---
#include "secrets.h"
#include "halmet_config.h"
#include "engine_state.h"
#include "BilgeFan.h"
#include "RpmSensor.h"
#include "analog_inputs.h"
#include "digital_alarms.h"
#include "engine_state_machine.h"
#include "onewire_setup.h"
#include "n2k_publisher.h"
#include "diagnostics.h"

using namespace sensesp;

// ============================================================
//  Global hardware objects
// ============================================================
static tNMEA2000_esp32  gNmea2000;
static Adafruit_ADS1115 gAds;
static RpmSensor        gRpm(HALMET_PIN_D1);
static BilgeFan         gBilgeFan(HALMET_PIN_RELAY, /*activeHigh=*/true);

// ============================================================
//  Shared engine/sensor state
// ============================================================
static EngineState gState;

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
    SetupLogging();

    // --- Digital inputs ---
    pinMode(HALMET_PIN_D2, INPUT_PULLUP);
    pinMode(HALMET_PIN_D3, INPUT_PULLUP);
    pinMode(HALMET_PIN_D4, INPUT_PULLUP);

    // --- RPM pulse counter ---
    gRpm.begin();

    // --- Bilge fan relay ---
    gBilgeFan.begin();

    // --- I2C bus ---
    Wire.setTimeOut(100);
    Wire.begin(HALMET_PIN_SDA, HALMET_PIN_SCL);
    Wire.setClock(400000);

    // --- ADS1115 ADC (ADDR tied to VCC → 0x4B) ---
    gState.adsOk = gAds.begin(ADS1115_I2C_ADDRESS, &Wire);
    if (gState.adsOk) {
        gAds.setGain(GAIN_ONE);
        gAds.setDataRate(RATE_ADS1115_8SPS);
        ESP_LOGI("HALMET", "ADS1115 found at 0x%02X", ADS1115_I2C_ADDRESS);
    } else {
        gState.adsFailCount++;
        ESP_LOGE("HALMET", "ADS1115 not found at 0x4B — will retry");
    }

    // --- NMEA 2000 ---
    setupNmea2000();

    // --- SensESP v3 app builder ---
    SensESPAppBuilder builder;
    builder.set_hostname("halmet-engine")
           ->set_wifi_client(WIFI_SSID, WIFI_PASSWORD)
           ->set_sk_server(SK_SERVER_IP, SK_SERVER_PORT)
           ->get_app();

    // --- Configurable parameters (web UI + persisted to flash) ---
    auto* gPurgeDurationSec = new PersistingObservableValue<float>(
        DEFAULT_PURGE_DURATION_S, "/bilge/purge_duration_s");
    ConfigItem(gPurgeDurationSec)
        ->set_title("Bilge fan purge duration (s)");

    auto* gPulsesPerRev = new PersistingObservableValue<float>(
        DEFAULT_PULSES_PER_REVOLUTION, "/rpm/pulses_per_rev");
    ConfigItem(gPulsesPerRev)
        ->set_title("Alternator pulses per engine revolution");

    auto* gEngineRunningRpm = new PersistingObservableValue<float>(
        DEFAULT_ENGINE_RUNNING_RPM, "/rpm/running_threshold");
    ConfigItem(gEngineRunningRpm)
        ->set_title("RPM threshold: engine considered running");

    auto* gTankCapacityL = new PersistingObservableValue<float>(
        DEFAULT_TANK_CAPACITY_L, "/tank/capacity_l");
    ConfigItem(gTankCapacityL)
        ->set_title("Tank capacity (litres)");

    auto* gCoolantWarnC = new PersistingObservableValue<float>(
        DEFAULT_COOLANT_WARN_C, "/coolant/warn_threshold_c");
    ConfigItem(gCoolantWarnC)
        ->set_title("Coolant warning threshold (°C)");

    auto* gCoolantAlarmC = new PersistingObservableValue<float>(
        DEFAULT_COOLANT_ALARM_C, "/coolant/alarm_threshold_c");
    ConfigItem(gCoolantAlarmC)
        ->set_title("Coolant alarm threshold (°C)");

    // --- Signal K outputs for data with no NMEA 2000 PGN ---
    auto* skFanState = new SKOutputBool("electrical.switches.bilgeFan.state", "",
                                        new SKMetadata("Bilge fan", "Bilge fan purge active"));
    auto* skIgnState = new SKOutputBool("electrical.switches.ignition.state", "",
                                        new SKMetadata("Ignition key", "Ignition key present"));
    skFanState->set(false);
    skIgnState->set(false);

    auto* skCoolantNotification = new SKOutputRawJson(
        "notifications.propulsion.0.coolantTemperature", "");

    // --- OTA safety: force relay OFF before firmware write begins ---
    event_loop()->onDelay(0, []() {
        ArduinoOTA.onStart([]() {
            gBilgeFan.forceOff();
            ESP_LOGW("HALMET", "OTA starting — relay forced OFF");
        });
    });

    // Relay state change callback → Signal K
    gBilgeFan.onRelayChange([skFanState](bool on) {
        if (skFanState) skFanState->set(on);
        ESP_LOGI("BilgeFan", "Relay -> %s", on ? "ON" : "OFF");
    });

    // --- 1-Wire setup ---
    static onewire_setup::Outputs owOut = {};
    onewire_setup::init(owOut);

    // --- Module init (callback registration order preserved) ---
    engine_state_machine::init({
        .state            = &gState,
        .nmea2000         = &gNmea2000,
        .rpm              = &gRpm,
        .pulsesPerRev     = gPulsesPerRev,
        .runningThreshold = gEngineRunningRpm,
    });

    analog_inputs::init({
        .state                 = &gState,
        .ads                   = &gAds,
        .skCoolantNotification = skCoolantNotification,
        .coolantWarnC          = gCoolantWarnC,
        .coolantAlarmC         = gCoolantAlarmC,
    });

    digital_alarms::init(&gState);

    n2k_publisher::init({
        .state        = &gState,
        .nmea2000     = &gNmea2000,
        .tankCapacityL = gTankCapacityL,
        .owDest       = owOut.owDest,
        .owSensors    = owOut.owSensors,
    });

    // Bilge fan state machine tick (1 s)
    event_loop()->onRepeat(INTERVAL_FAN_MS, [gPurgeDurationSec]() {
        gBilgeFan.update(gState.engineRunning, gPurgeDurationSec->get());
    });

    // Signal K supplemental data (5 s)
    event_loop()->onRepeat(5000, [skFanState, skIgnState]() {
        if (skFanState) skFanState->set(gBilgeFan.relayOn());
        if (skIgnState) skIgnState->set(digitalRead(HALMET_PIN_D4) == HIGH);
    });

    diagnostics::init(&gState);

    ESP_LOGI("HALMET", "Setup complete.");
}

// ============================================================
//  Arduino loop() — SensESP v3: just tick the event loop
// ============================================================
void loop() {
    event_loop()->tick();
}
