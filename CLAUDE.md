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
- `N2kSenders` — Static helpers wrapping tNMEA2000 message construction for PGN 127488 (RPM), 127489 (engine dynamic), 127505 (fluid level), 130316 (temperature extended).
- `onewire_setup` — Sensor-centric 1-Wire config: scans bus at boot, creates a config card per detected ROM with dropdown destination picker, maps selected sensors to internal slots for N2K/SK output.
- `halmet_config.h` — All compile-time defaults and pin definitions. Runtime values are persisted to LittleFS/NVS and edited via the SensESP web UI.

**Data flow:** Sensor reads → shared global state variables → periodic N2K sender callbacks transmit PGNs. Signal K outputs (bilge fan state, ignition key) go over WiFi WebSocket.

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
- `/onewire/<rom_hex>/dest` — destination for each detected 1-Wire sensor (dropdown: "Not used", "Engine room", etc.). Config paths are keyed by ROM address (stable across discovery order). Requires reboot after changing.

## Key Gotchas

- `ConfigItem` is a **free function template** (`ConfigItem(ptr)`), not a class to `new`. It deduces the template type from the pointer.
- OneWire classes (`DallasTemperatureSensors`, `OneWireTemperature`) are in namespace `sensesp::onewire`, not `sensesp`.
- PlatformIO upload on Windows may hit a `UnicodeEncodeError` in progress bars. Workaround: set `PYTHONIOENCODING=utf-8`.
- SensESP web UI dropdown schema must use `"type":"array","format":"select","uniqueItems":true` with enum inside `"items":{...}`. Plain `"type":"string","enum":[...]` renders as a text field — the frontend does NOT use jsoneditor.js, it has a custom Preact `EditControl` switch.
- `kTempDests[]` is **APPEND ONLY** — inserting shifts persisted config. Index 0 is `"Not used"` (the default for new sensors).
