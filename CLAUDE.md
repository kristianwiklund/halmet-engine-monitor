# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

HALMET Marine Engine & Tank Monitor — ESP32 firmware for a Hat Labs HALMET board monitoring a Volvo Penta MD7A diesel engine. Sends engine/tank data over NMEA 2000 (primary) and WiFi/Signal K (supplemental, for data without standard N2K PGNs).

## Build Commands

```bash
pio run -e halmet              # Build
pio run -e halmet -t upload    # Flash via USB
pio device monitor -b 115200   # Serial monitor
```

Single environment: `halmet`. No test suite exists.

## Platform & Build Constraints

- **Must use pioarduino** (community fork), not the official `espressif32` platform — SensESP v3 requires Arduino ESP32 Core 3.x / IDF 5.x, while official espressif32 is frozen at Core 2.0.17.
- `lib_ldf_mode = deep` and `lib_archive = no` are required — without these, transitive includes (OneWire.h, sensesp.h, NMEA2000.h) are not discovered, and pioarduino weak-symbol overrides fail at link time.
- NMEA2000 registry name is `ttlappalainen/NMEA2000-library` (hyphen). `NMEA2000_esp32` is not in the registry — pulled from GitHub directly.
- `esp_websocket_client` must use `name=url` syntax in lib_deps (IDF managed component).

## Architecture

**Framework:** SensESP v3 + NMEA2000 library (Timo Lappalainen) on Arduino/PlatformIO.

**Event-driven model:** All periodic work runs as ReactESP event callbacks registered in `setup()`. The `loop()` body is just `event_loop()->tick()`. No FreeRTOS tasks or manual millis() timing.

**Key modules:**

- `main.cpp` — Wires everything together: SensESP app builder, configurable parameters (PersistingObservableValue + ConfigItem), sensor read callbacks, N2K send callbacks, Signal K outputs.
- `RpmSensor` — ESP32 hardware pulse counter on D1 (alternator W-terminal). Returns RPM based on configurable pulses-per-revolution.
- `BilgeFan` — State machine (IDLE → RUNNING → PURGE → IDLE). Drives relay on GPIO 32. Purge duration is runtime-configurable.
- `N2kSenders` — Static helpers wrapping tNMEA2000 message construction for PGN 127488 (RPM), 127489 (engine dynamic), 127501 (binary switch status), 127505 (fluid level), 127508 (battery status), 130316 (temperature extended).
- `analog_inputs` — ADS1115 reads (tank level, battery voltage) and INA226 reads (coolant sender resistance → temperature via CurveInterpolator). Registers all event-loop callbacks for the analog pipeline.
- `onewire_setup` — Sensor-centric 1-Wire config: scans bus at boot, creates a config card per detected ROM with dropdown destination picker, maps selected sensors to internal slots for N2K/SK output.
- `halmet_config.h` — All compile-time defaults and pin definitions. Runtime values are persisted to LittleFS/NVS and edited via the SensESP web UI.

**Data flow:** Sensor reads → shared `EngineState` struct → periodic N2K sender callbacks transmit PGNs. Signal K outputs (bilge fan state, ignition key, battery voltage, coolant notifications) go over WiFi WebSocket.

**Coolant temperature path:** INA226 current sensor measures bus voltage and shunt current across the VDO sender. Firmware derives `R = V_bus / I_shunt`, then maps resistance to °C via a runtime-editable `CurveInterpolator` (web UI path `/coolant/resistance_curve`). A1 / ADS ch0 is **no longer used** for coolant temperature.

**Pin assignments** are defined as `-D` build flags in `platformio.ini`, not hardcoded in source.

## SensESP v3 API Notes

These differ from v2 docs/examples found online:
- No global `ReactESP` app object; use `event_loop()` free function
- No `sensesp_app->start()` — removed in v3
- `NumberConfig` → `PersistingObservableValue<T>` + `ConfigItem(ptr)` (free function, not a class)
- `set_wifi()` is deprecated → use `set_wifi_client()`
- `SetupSerialDebug()` → `SetupLogging()`
- Builder methods return pointer (use `->` not `.`)

## Configuration

Runtime parameters editable via SensESP web UI at `http://halmet-engine.local/config`:
- `/rpm/pulses_per_rev` — alternator pulses per crankshaft revolution (calibrate first)
- `/rpm/running_threshold` — RPM above which engine is "running"
- `/bilge/purge_duration_s` — bilge fan on-time after engine stop
- `/tank/capacity_l` — tank volume for PGN 127505
- `/tank/resistance_curve` — CurveInterpolator table: sender resistance (Ω) → level ratio (0–1). Default: VDO 10 Ω empty / 180 Ω full.
- `/coolant/resistance_curve` — CurveInterpolator table: sender resistance (Ω) → temperature (°C). Default: VDO Type A (European) NTC curve. User-editable.
- `/coolant/warn_threshold_c` — coolant warn notification threshold (default 95 °C)
- `/coolant/alarm_threshold_c` — coolant alarm notification threshold (default 105 °C)
- `/voltage/multiplier` — A4 voltage divider multiplier (default 10.09 for HALMET 20 kΩ/2.2 kΩ divider)
- `/voltage/sk_path` — Signal K path for battery voltage output (default `electrical.batteries.0.voltage`)
- `/n2k/engine_instance` — NMEA 2000 engine instance number (default 0)
- `/n2k/product_code`, `/n2k/device_function`, `/n2k/device_class`, `/n2k/manufacturer_code` — N2K device identity constants. Require restart after change.
- `/intervals/rpm_n2k_ms`, `/intervals/n2k_slow_ms`, `/intervals/sk_supplemental_ms` — PGN and SK send intervals in ms. Require restart after change.
- `/onewire/<rom_hex>/dest` — destination for each detected 1-Wire sensor (dropdown: "Not used", "Engine room", etc.). Config paths are keyed by ROM address (stable across discovery order). Requires reboot after changing.

## Key Gotchas

- `ConfigItem` is a **free function template** (`ConfigItem(ptr)`), not a class to `new`. It deduces the template type from the pointer.
- OneWire classes (`DallasTemperatureSensors`, `OneWireTemperature`) are in namespace `sensesp::onewire`, not `sensesp`.
- PlatformIO upload on Windows may hit a `UnicodeEncodeError` in progress bars. Workaround: set `PYTHONIOENCODING=utf-8`.
- SensESP web UI dropdown schema must use `"type":"array","format":"select","uniqueItems":true` with enum inside `"items":{...}`. Plain `"type":"string","enum":[...]` renders as a text field — the frontend does NOT use jsoneditor.js, it has a custom Preact `EditControl` switch.
- `kTempDests[]` is **APPEND ONLY** — inserting shifts persisted config. Index 0 is `"Not used"` (the default for new sensors).
- `setupNmea2000()` is called **after** the SensESP app builder so that `PersistingObservableValue` config items are loaded before their values are passed to the N2K init call. N2K device constants (`product_code`, etc.) require restart because they are passed once at `setupNmea2000()` time.
- The `CurveInterpolator` for coolant temp (`resCurve`) is used synchronously: `resCurve->set(resistance)` followed immediately by `resCurve->get()`. This works because no downstream observers are attached. If you ever wire a reactive observer to `resCurve`, it will fire on every INA226 read (500 ms) including fault conditions.
- Battery N2K send (PGN 127508) is guarded with `if (st->adsOk && st->supplyVoltageV > 0.0f)` — intentional: avoids sending zero/garbage voltage when ADS is not yet initialised.
- `ina226Ok` in `EngineState` is set once at boot from `gIna226.init()`. All INA226 callbacks guard on this flag. If the INA226 is absent at boot, coolant temperature will always read as `N2kDoubleNA`.
