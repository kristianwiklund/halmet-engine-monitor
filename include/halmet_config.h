#pragma once

// ============================================================
//  halmet_config.h  —  Runtime-configurable parameter defaults
//  All values here are the compile-time defaults only.
//  Actual runtime values are stored in NVS and edited via the
//  SensESP web configuration UI (http://<board-ip>/config).
// ============================================================

// ----------------------------------------------------------
//  Engine / RPM
// ----------------------------------------------------------

/// Pulses emitted per engine crankshaft revolution by the
/// alternator W-terminal.  Equals (alternator pole pairs) ×
/// (alternator pulley dia / engine pulley dia).
/// Calibrate against a handheld optical tachometer on first run.
#define DEFAULT_PULSES_PER_REVOLUTION   10.0f

/// RPM threshold above which the engine is considered "running".
/// Apply hysteresis in code (see fan state machine).
#define DEFAULT_ENGINE_RUNNING_RPM      200.0f

/// Moving-average window (in samples) applied to the raw RPM value.
#define RPM_SMOOTHING_SAMPLES           5

/// Debounce time (ms) for engine-running state transitions.
#define ENGINE_STATE_DEBOUNCE_MS        5000

// ----------------------------------------------------------
//  Bilge fan purge
// ----------------------------------------------------------

/// How long (seconds) to run the bilge fan after the engine stops.
#define DEFAULT_PURGE_DURATION_S        600.0f

// ----------------------------------------------------------
//  Tank configuration — common
// ----------------------------------------------------------

/// Reported capacity (litres) transmitted in PGN 127505.
#define DEFAULT_TANK_CAPACITY_L         100.0f

// ----------------------------------------------------------
//  Tank sensor — resistive mode (DEFAULT)
//
//  Uses HALMET constant-current source (10 mA) on A2 / ADS ch1.
//  Resistance = V_adc / I.  Level ratio derived from a runtime-
//  configurable CurveInterpolator (web UI calibration table).
//
//  Default curve: European VDO fuel sender
//    10 Ω → 0.0 (empty)
//   180 Ω → 1.0 (full)
//
//  To switch to Gobius Pro sensors instead, define TANK_SENSOR_GOBIUS
//  in platformio.ini build_flags.
// ----------------------------------------------------------
#define TANK_SENDER_CHANNEL         1           // ADS ch1 = A2 screw terminal
#define TANK_MEASUREMENT_CURRENT    0.01f       // 10 mA constant-current source
#define TANK_RESISTANCE_MAX_OHM     320.0f      // hardware limit; above = fault
#define TANK_RESISTANCE_EMPTY_OHM   10.0f       // VDO: empty
#define TANK_RESISTANCE_FULL_OHM    180.0f      // VDO: full
#define INTERVAL_TANK_MS            500         // resistive sender read interval

// ----------------------------------------------------------
//  Tank sensor — Gobius Pro mode (optional, define TANK_SENSOR_GOBIUS)
//
//  Two binary threshold sensors on A2+A3 / ADS ch1+ch2.
//  Level reported as one of three fixed bands.
// ----------------------------------------------------------
#ifdef TANK_SENSOR_GOBIUS
/// Gobius output voltage threshold (V).
/// Below this → output is sinking to GND (threshold reached).
#define GOBIUS_THRESHOLD_VOLTAGE    1.5f

/// Estimated level percentages for each combination of sensor states.
/// Midpoints of the three bands: <1/4, 1/4–3/4, >=3/4.
#define TANK_LEVEL_LOW_PCT          12.5f   // both sensors triggered  (< 1/4)
#define TANK_LEVEL_MID_PCT          50.0f   // only 3/4 sensor triggered (1/4–3/4)
#define TANK_LEVEL_HIGH_PCT         87.5f   // no sensor triggered     (>= 3/4)
#endif

// ----------------------------------------------------------
//  Temperature sender (Volvo Penta / VDO-type NTC)
// ----------------------------------------------------------
//  These are the (voltage, °C) calibration knots for the
//  CurveInterpolator.  Voltage is what HALMET A1 measures when
//  the original VP gauge is in parallel (gauge coil ~100 Ω).
//  Adjust empirically during commissioning.
//
//  voltage (V)  →  temperature (°C)
#define TEMP_CURVE_POINTS \
    {3.10f, 40.0f}, \
    {2.50f, 60.0f}, \
    {1.80f, 80.0f}, \
    {1.20f, 100.0f}, \
    {0.70f, 120.0f}

// ----------------------------------------------------------
//  NMEA 2000
// ----------------------------------------------------------

/// Engine instance number on the NMEA 2000 bus.
#define N2K_ENGINE_INSTANCE             0

/// Device serial number (arbitrary, must be unique on bus).
#define N2K_DEVICE_SERIAL               "12345678"

/// Model ID reported on the N2K bus.
#define N2K_MODEL_ID                    "HALMET Engine Monitor"

// ----------------------------------------------------------
//  I2C bus & ADS1115 (HALMET PCB-fixed, not variant-configurable)
//  HALMET routes SDA→GPIO21, SCL→GPIO22.
//  ADS1115 ADDR pin is tied to VCC → address 0x4B.
// ----------------------------------------------------------
#define HALMET_PIN_SDA          21
#define HALMET_PIN_SCL          22
#define ADS1115_I2C_ADDRESS     0x4B

// ----------------------------------------------------------
//  Alarm debouncing (shift-register majority vote)
// ----------------------------------------------------------
#define ALARM_DEBOUNCE_SAMPLES      5
#define ALARM_DEBOUNCE_THRESHOLD    4       // 4-of-5 = alarm asserted

// ----------------------------------------------------------
//  I2C / ADS1115 recovery
// ----------------------------------------------------------
#define INTERVAL_ADS_RETRY_MS       5000

// ----------------------------------------------------------
//  Coolant sensor fault detection
// ----------------------------------------------------------
#define COOLANT_VOLT_MIN_V          0.50f   // below = open/shorted sender
#define COOLANT_VOLT_MAX_V          3.50f   // above = open/shorted sender

// ----------------------------------------------------------
//  Coolant temperature threshold alerting
// ----------------------------------------------------------
#define DEFAULT_COOLANT_WARN_C      95.0f   // Signal K "warn" notification
#define DEFAULT_COOLANT_ALARM_C     105.0f  // Signal K "alarm" notification

// ----------------------------------------------------------
//  Stale data guard
// ----------------------------------------------------------
#define STALE_DATA_TIMEOUT_MS       5000

// ----------------------------------------------------------
//  1-Wire → N2K/SK temperature source assignment
// ----------------------------------------------------------
#define NUM_ONEWIRE_SLOTS           6
#define INTERVAL_ONEWIRE_N2K_MS     10000   // match 1-Wire read interval

// ----------------------------------------------------------
//  Polling intervals (ms)
// ----------------------------------------------------------
#define INTERVAL_ANALOG_MS              200     // A1 coolant temp read
#define INTERVAL_DIGITAL_ALARM_MS       500     // D2/D3 alarm inputs
#define INTERVAL_1WIRE_MS               10000   // DS18B20 chain (slow)
#define INTERVAL_RPM_MS                 100     // RPM counter update
#define INTERVAL_FAN_MS                 1000    // Fan state machine tick
#define INTERVAL_DIAG_MS                10000   // Diagnostics heartbeat
#define INTERVAL_ONEWIRE_DIAG_MS        10000   // 1-Wire sensor list to SK
