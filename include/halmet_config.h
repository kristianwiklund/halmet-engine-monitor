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
//  Tank configuration
// ----------------------------------------------------------

/// Reported capacity (litres) transmitted in PGN 127505.
/// Configure to match the actual tank volumes.
#define DEFAULT_TANK1_CAPACITY_L        100.0f
#define DEFAULT_TANK2_CAPACITY_L        100.0f

/// Gobius OUT1 voltage threshold (V).
/// Below this → threshold reached (output sinking to GND).
#define GOBIUS_THRESHOLD_VOLTAGE        1.5f

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
//  Polling intervals (ms)
// ----------------------------------------------------------
#define INTERVAL_ANALOG_MS              200     // A1 temp/tank reads
#define INTERVAL_DIGITAL_ALARM_MS       500     // D2/D3 alarm inputs
#define INTERVAL_1WIRE_MS               10000   // DS18B20 chain (slow)
#define INTERVAL_RPM_MS                 100     // RPM counter update
#define INTERVAL_FAN_MS                 1000    // Fan state machine tick
