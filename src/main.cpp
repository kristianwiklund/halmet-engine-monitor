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
//    A2 / ADS ch1  → Resistive tank sender (10 mA CCS, default)
//                     or Gobius Pro sensor A (-D TANK_SENSOR_GOBIUS)
//    A3 / ADS ch2  → Spare; Gobius Pro sensor B (-D TANK_SENSOR_GOBIUS)
//    1-Wire        → DS18B20 engine-room temperature probes
//    GPIO 32       → Bilge fan relay output
//    GPIO 33       → Engine warning lamp (HIGH = any alarm active)
// ============================================================

#include <Arduino.h>

// --- SensESP v3 ---
#include <sensesp.h>
#include <sensesp_app_builder.h>
#include <sensesp/ui/config_item.h>
#include <sensesp/signalk/signalk_output.h>
#include <sensesp/signalk/signalk_put_request_listener.h>
#include <sensesp/signalk/signalk_value_listener.h>
#include <sensesp/system/lambda_consumer.h>

// --- NMEA 2000 ---
#include <ArduinoOTA.h>
#include <NMEA2000_esp32.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

// --- Adafruit ADS1115 ---
#include <Adafruit_ADS1X15.h>

// --- INA226 current sensor ---
#include <INA226_WE.h>

// --- Project modules ---
#include "secrets.h"
#include "halmet_config.h"
#include "engine_state.h"
#include "BilgeFan.h"
#include "RpmSensor.h"
#include "SwitchMetadata.h"
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
static INA226_WE        gIna226(INA226_I2C_ADDRESS);
static RpmSensor        gRpm(HALMET_PIN_D1);
static BilgeFan         gBilgeFan(HALMET_PIN_RELAY, /*activeHigh=*/true);

// ============================================================
//  Shared engine/sensor state
// ============================================================
static EngineState gState;

// ============================================================
//  NMEA 2000 setup
// ============================================================
struct N2kConfig {
    uint16_t productCode;
    uint8_t  deviceFunction;
    uint8_t  deviceClass;
    uint16_t manufacturerCode;
};

static void setupNmea2000(const N2kConfig& cfg) {
    // Derive a unique 21-bit device number from the chip's MAC address.
    // The N2K uniqueNumber field is 21 bits wide (max 2,097,151); passing a
    // larger value causes silent truncation in the library.
    uint32_t uniqueNum = (uint32_t)(ESP.getEfuseMac() & 0x1FFFFFUL);

    // Restore the last address claimed on this bus so we don't restart address
    // negotiation from scratch on every reboot (N2K standard requirement).
    Preferences prefs;
    prefs.begin("n2k", /*readOnly=*/true);
    uint8_t savedAddr = prefs.getUChar("addr", 23);
    prefs.end();

    gNmea2000.SetProductInformation(N2K_DEVICE_SERIAL, cfg.productCode,
                                    N2K_MODEL_ID, FW_VERSION_STR, "1.0.0");
    gNmea2000.SetDeviceInformation(uniqueNum, cfg.deviceFunction,
                                   cfg.deviceClass, cfg.manufacturerCode);
    gNmea2000.SetN2kCANSendFrameBufSize(250);
    gNmea2000.SetN2kCANReceiveFrameBufSize(250);
    gNmea2000.SetMode(tNMEA2000::N2km_NodeOnly, savedAddr);
    gNmea2000.EnableForward(false);

    // Declare transmitted PGNs so MFDs can discover this device properly.
    static const unsigned long kTransmitPGNs[] PROGMEM = {
        127488UL,   // Engine Rapid Update (RPM)
        127489UL,   // Engine Dynamic Parameters (coolant temp, oil alarm)
        127501UL,   // Binary Switch Bank Status (bilge fan relay)
        127505UL,   // Fluid Level (tank)
        127508UL,   // Battery Status (supply voltage)
        130316UL,   // Temperature Extended Range (1-Wire sensors)
        0
    };
    gNmea2000.ExtendTransmitMessages(kTransmitPGNs);

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

    // --- Warning lamp (off until first alarm read) ---
    pinMode(HALMET_PIN_WARN_LAMP, OUTPUT);
    digitalWrite(HALMET_PIN_WARN_LAMP, LOW);

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

    // --- INA226 current sensor (coolant temp sender) ---
    gState.ina226Ok = gIna226.init();
    if (gState.ina226Ok) {
        gIna226.setResistorRange(INA226_SHUNT_RESISTANCE_OHM, 1.0);  // 1A max expected
        gIna226.setAverage(INA226_AVERAGE_64);
        ESP_LOGI("HALMET", "INA226 found at 0x%02X", INA226_I2C_ADDRESS);
    } else {
        ESP_LOGE("HALMET", "INA226 not found at 0x%02X", INA226_I2C_ADDRESS);
    }

    // --- NMEA 2000 (moved after app builder so PersistingObservableValue works) ---
    // Config items created below, N2K init uses their persisted values.

    // --- SensESP v3 app builder ---
    SensESPAppBuilder builder;
    builder.set_hostname("halmet-engine")
           ->set_wifi_client(WIFI_SSID, WIFI_PASSWORD)
           ->set_sk_server(SK_SERVER_IP, SK_SERVER_PORT)
           ->enable_ota(OTA_PASSWORD)
           ->get_app();

    // --- Persist N2K source address after address claiming ---
    event_loop()->onRepeat(10000, []() {
        static bool saved = false;
        if (saved) return;
        if (gNmea2000.ReadResetAddressChanged()) {
            uint8_t addr = gNmea2000.GetN2kSource();
            Preferences prefs;
            prefs.begin("n2k", /*readOnly=*/false);
            prefs.putUChar("addr", addr);
            prefs.end();
            saved = true;
            ESP_LOGI("HALMET", "N2K address claimed: %d (saved)", addr);
        }
    });

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

    auto* gVoltageMultiplier = new PersistingObservableValue<float>(
        DEFAULT_VOLTAGE_MULTIPLIER, "/voltage/multiplier");
    ConfigItem(gVoltageMultiplier)
        ->set_title("A4 voltage divider multiplier (20k/2.2k = 10.09)");

    // --- N2K device config (requires restart) ---
    auto* gN2kProductCode = new PersistingObservableValue<float>(
        N2K_PRODUCT_CODE, "/n2k/product_code");
    ConfigItem(gN2kProductCode)
        ->set_title("N2K product code")
        ->set_requires_restart(true);

    auto* gN2kDeviceFunction = new PersistingObservableValue<float>(
        N2K_DEVICE_FUNCTION, "/n2k/device_function");
    ConfigItem(gN2kDeviceFunction)
        ->set_title("N2K device function (160=Engine Gateway)")
        ->set_requires_restart(true);

    auto* gN2kDeviceClass = new PersistingObservableValue<float>(
        N2K_DEVICE_CLASS, "/n2k/device_class");
    ConfigItem(gN2kDeviceClass)
        ->set_title("N2K device class (25=Propulsion)")
        ->set_requires_restart(true);

    auto* gN2kManufacturerCode = new PersistingObservableValue<float>(
        N2K_MANUFACTURER_CODE, "/n2k/manufacturer_code");
    ConfigItem(gN2kManufacturerCode)
        ->set_title("N2K manufacturer code (999=uncertified)")
        ->set_requires_restart(true);

    auto* gN2kEngineInstance = new PersistingObservableValue<float>(
        N2K_ENGINE_INSTANCE, "/n2k/engine_instance");
    ConfigItem(gN2kEngineInstance)
        ->set_title("N2K engine instance (0–252)");

    // --- N2K send intervals (requires restart) ---
    auto* gIntervalRpmN2k = new PersistingObservableValue<float>(
        INTERVAL_RPM_N2K_MS, "/intervals/rpm_n2k_ms");
    ConfigItem(gIntervalRpmN2k)
        ->set_title("PGN 127488 send interval (ms)")
        ->set_requires_restart(true);

    auto* gIntervalN2kSlow = new PersistingObservableValue<float>(
        INTERVAL_N2K_SLOW_MS, "/intervals/n2k_slow_ms");
    ConfigItem(gIntervalN2kSlow)
        ->set_title("PGN 127489/127505/127501 send interval (ms)")
        ->set_requires_restart(true);

    auto* gIntervalSkSupplemental = new PersistingObservableValue<float>(
        INTERVAL_SK_SUPPLEMENTAL_MS, "/intervals/sk_supplemental_ms");
    ConfigItem(gIntervalSkSupplemental)
        ->set_title("Signal K supplemental data interval (ms)")
        ->set_requires_restart(true);

    // --- NMEA 2000 init (after config values are loaded) ---
    setupNmea2000({
        .productCode      = (uint16_t)gN2kProductCode->get(),
        .deviceFunction   = (uint8_t)gN2kDeviceFunction->get(),
        .deviceClass      = (uint8_t)gN2kDeviceClass->get(),
        .manufacturerCode = (uint16_t)gN2kManufacturerCode->get(),
    });

    // --- Signal K outputs for data with no NMEA 2000 PGN ---

    auto* skFanState = new SKOutputBool("electrical.switches.bilgeFan.state", "",
                                        new SwitchMetadata("Bilge fan"));
    auto* skIgnState = new SKOutputBool("electrical.switches.ignition.state", "",
                                        new SKMetadata("", "Ignition turned on"));
    skFanState->set(false);
    skIgnState->set(false);

    auto* skCoolantNotification = new SKOutputRawJson(
        "notifications.propulsion.0.coolantTemperature", "");

    // --- OTA safety: force relay OFF before firmware write begins ---
    event_loop()->onDelay(0, []() {
        ArduinoOTA.onStart([]() {
            esp_task_wdt_delete(NULL);
            gBilgeFan.forceOff();
            ESP_LOGW("HALMET", "OTA starting — watchdog removed, relay forced OFF");
        });
    });

    // Relay state change callback → Signal K
    gBilgeFan.onRelayChange([skFanState](bool on) {
        if (skFanState) skFanState->set(on);
        ESP_LOGI("BilgeFan", "Relay -> %s", on ? "ON" : "OFF");
    });

    // SK PUT listener — allows KIP (and other SK clients) to control the fan
    auto* fanPutListener = new SKPutRequestListener<bool>(
        "electrical.switches.bilgeFan.state");
    fanPutListener->connect_to(new LambdaConsumer<bool>([](bool v) {
        if (v) gBilgeFan.manualOn();
        else   gBilgeFan.forceOff();
        ESP_LOGI("BilgeFan", "SK PUT -> %s", v ? "ON" : "OFF");
    }));

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
        .engineInstance   = gN2kEngineInstance,
        .intervalRpmN2k   = gIntervalRpmN2k,
    });

    analog_inputs::init({
        .state                 = &gState,
        .ads                   = &gAds,
        .ina226                = &gIna226,
        .skCoolantNotification = skCoolantNotification,
        .coolantWarnC          = gCoolantWarnC,
        .coolantAlarmC         = gCoolantAlarmC,
        .voltageMultiplier     = gVoltageMultiplier,
    });

    digital_alarms::init(&gState);

    n2k_publisher::init({
        .state         = &gState,
        .nmea2000      = &gNmea2000,
        .tankCapacityL = gTankCapacityL,
        .engineInstance = gN2kEngineInstance,
        .intervalN2kSlow = gIntervalN2kSlow,
        .owDest        = owOut.owDest,
        .owSensors     = owOut.owSensors,
        .bilgeFan      = &gBilgeFan,
    });

    // Bilge fan state machine tick (1 s)
    event_loop()->onRepeat(INTERVAL_FAN_MS, [gPurgeDurationSec]() {
        gBilgeFan.update(gState.engineRunning, gPurgeDurationSec->get());
    });

    // Signal K supplemental data (configurable interval)
    int skSupMs = (int)gIntervalSkSupplemental->get();
    event_loop()->onRepeat(skSupMs, [skFanState, skIgnState]() {
        if (skFanState) skFanState->set(gBilgeFan.relayOn());
        if (skIgnState) skIgnState->set(digitalRead(HALMET_PIN_D4) == HIGH);
    });

    diagnostics::init(&gState);

    // --- Hardware watchdog (must be last in setup) ---
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WATCHDOG_TIMEOUT_MS,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);

    ESP_LOGI("HALMET", "Setup complete.");
}

// ============================================================
//  Arduino loop() — SensESP v3: just tick the event loop
// ============================================================
void loop() {
    esp_task_wdt_reset();
    event_loop()->tick();
}
