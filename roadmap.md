# HALMET Engine Monitor — Roadmap

Prioritised improvements grouped into implementation sprints.
See `HALMET_Design_Specification.md` for full design context.

---

## Sprint 1 — Safety & Quick Wins (COMPLETE)

All items implemented and verified on hardware (commit `8534703`).

| # | Feature | Status |
|---|---------|--------|
| 1 | Alarm input debouncing (4-of-5 majority vote on D2/D3) | Done |
| 2 | Coolant sensor fault detection (out-of-range voltage → `N2kDoubleNA`) | Done |
| 3 | Stale data guard (>5 s without valid ADS read → `N2kDoubleNA`) | Done |
| 4 | I2C bus fault recovery (periodic `Wire.begin()` + `gAds.begin()` retry) | Done |
| 5 | Fix fluid type (`N2kft_Oil` → `N2kft_Fuel` in PGN 127505) | Done |

Hardware watchdog was originally Sprint 1 item 1 but deferred to Sprint 5 due to OTA bricking risk (watchdog firing mid-flash corrupts firmware).

## Sprint 2 — High-Value Features (COMPLETE)

All items implemented and verified on hardware (commit `0af9730`).

| # | Feature | Status |
|---|---------|--------|
| 6 | Fix cold-boot coolant sentinel (`gCoolantK` init to `N2kDoubleNA`, stale guard fires when no valid read ever) | Done |
| 7 | Temperature threshold alerting (configurable warn 95°C / alarm 105°C → Signal K notifications) | Done |
| 8 | Diagnostics heartbeat (uptime, firmware version, ADS fail count, reset reason → Signal K every 10 s) | Done |

## Sprint 2.5 — 1-Wire Temperature Source Assignment (COMPLETE)

| # | Feature | Status |
|---|---------|--------|
| 8b | Configurable 1-Wire → N2K/SK destination per sensor slot (6 slots, 10 destinations, web UI config, PGN 130316 send) | Done |

## Sprint 3 — 1-Wire Completeness & Relay Safety (COMPLETE)

| # | Feature | Status |
|---|---------|--------|
| 9 | Safe relay state before OTA (ArduinoOTA.onStart → forceOff relay) | Done |
| 10 | Firmware version in N2K product info (`FW_VERSION_STR` → `SetProductInformation()`) | Done |
| 15 | Add `propulsion.0.intakeManifoldTemperature` to 1-Wire destination list (index 10) | Done |
| 16 | Add `propulsion.0.engineBlockTemperature` to 1-Wire destination list (index 11) | Done |

## Sprint 4 — Architecture Refactor

`main.cpp` is at ~500 lines — the 500-line threshold is effectively reached. Refactor before adding medium+ complexity features.

| # | Feature | Description | Complexity |
|---|---------|-------------|------------|
| 13 | Shared state struct | Replace scattered `static` globals with a single `EngineState` struct. Required before the module split so all modules can read/write shared data without cross-including each other | Low |
| 14 | Decompose monolithic setup() | Split `main.cpp` into focused modules (analog_inputs, digital_alarms, engine_state, n2k_publisher, diagnostics). Each module exposes an `init()` function that registers its own event-loop callbacks | Medium |

## Sprint 5 — OTA Robustness & Watchdog

Depends on Sprint 3 item 9 (relay safety) being verified on hardware before item 12 is merged.

| # | Feature | Description | Complexity |
|---|---------|-------------|------------|
| 12 | Hardware watchdog | Register ESP32 task watchdog (~8 s timeout); reset in `loop()` and inside analog callback after I2C reads. Must deregister from TWDT during OTA (`esp_task_wdt_delete`), not just reset — OTA blocks `loop()` for 30–90 s | Low |

## Sprint 6 — ROM-Based 1-Wire Sensor Selection

Benefits from clean module structure (Sprint 4). Consider splitting into 17a (low: publish discovered ROM addresses as read-only SK JSON output) and 17b (high: dropdown UI in web config).

| # | Feature | Description | Complexity |
|---|---------|-------------|------------|
| 17 | Improve 1-Wire sensor selection: list detected sensors by ROM address (+ live value) instead of slot index | Current slot-based model doesn't reflect 1-Wire parallel bus topology; users should pick from discovered addresses | Medium–High |

## Candidate Pool — FROZEN (do not pick up unless explicitly ordered)

Features evaluated and deliberately deferred. Do **not** schedule, implement, or re-evaluate these without a direct instruction from the project owner.

| Feature | Reason deferred |
|---------|----------------|
| Two-tank support (second PGN 127505 instance) | Single tank with two Gobius threshold sensors — no second tank to monitor |
| Battery voltage on A4 (PGN 127508) | Victron equipment already provides battery monitoring on the N2K bus |
| Configurable N2K engine instance | Single engine on the bus; no conflict risk with current installation |
| Runtime-configurable temp curve | High complexity, low value for single-boat install. Compile-time `TEMP_CURVE_POINTS` in `halmet_config.h` is easy to edit and reflash. Risk of malformed runtime config producing silently wrong temperatures |
| Engine hours counter | Persist accumulated runtime seconds to LittleFS in 1-minute increments. Send in PGN 127489 `EngineTotalHours`. Deferred — low priority for current usage pattern |
| I2C LCD display (2×16 ASCII) showing engine temp, RPM, voltage (from N2K bus), configurable via web UI | Requires I2C display driver, N2K bus listener for voltage PGN, web UI config for display layout |
