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

## Sprint 4 — Architecture Refactor (COMPLETE)

Implemented in commit `f48dd79`. `main.cpp` reduced from ~505 to ~230 lines.

| # | Feature | Status |
|---|---------|--------|
| 13 | Shared state struct (`EngineState`) | Done |
| 14 | Decompose monolithic `setup()` into focused modules | Done |

## Sprint 5 — OTA Robustness & Watchdog (COMPLETE — see Sprint 11 #27)

## Sprint 6 — Sensor-Centric 1-Wire Configuration (COMPLETE)

Inverted the config model from slot-centric (6 anonymous slots) to sensor-centric (each detected ROM gets its own config card with dropdown destination picker).

| # | Feature | Status |
|---|---------|--------|
| 17a | Bus scan + ROM-based diagnostics (SK JSON with address, dest, slot, tempK per sensor) | Done |
| 17b | Sensor-centric web UI: each detected sensor gets a config card titled with its ROM address and a dropdown to pick destination | Done |

## Sprint 7 — 1-Wire UX Polish (DEFERRED — items moved to Candidate Pool)

## Sprint 8 — N2K Bilge Fan Switch Control (COMPLETE)

| # | Feature | Status |
|---|---------|--------|
| 20 | N2K bilge fan switch (PGN 127502 receive + PGN 127501 report) | Done |
| 20b | SK bilge fan switch control (PUT listener on `electrical.switches.bilgeFan.state`; `supportsPut:true` metadata for KIP) | Done |

## Sprint 9 — Resistive Tank Sender (COMPLETE)

| # | Feature | Status |
|---|---------|--------|
| 21 | Replace Gobius 3-band logic with continuous resistive sender (constant-current, CurveInterpolator, VDO 10–180 Ω default). Gobius mode retained via `-D TANK_SENSOR_GOBIUS`. | Done |

## Sprint 10 — Code Review Fixes (COMPLETE)

Issues found during 2026-04-14 code review. All low-risk, no functional impact on running firmware.

| # | Issue | Status |
|---|-------|--------|
| 22 | RPM volatile TOCTOU race — consolidated both volatile reads (`_pulseCount`, `_lastPulseTime`) into a single `noInterrupts()` block, eliminating the second critical section | Done |
| 23 | Stale I/O map comment in `main.cpp` — updated A2/A3 to reflect resistive tank sender (Sprint 9) | Done |
| 24 | Dead `OneWireSensors` files — deleted `.cpp` and `.h`, cleaned references in README.md and init_repo.ps1 | Done |
| 25 | Explicit flash size — added `board_build.flash_size = 16MB`, fixed stale "8 MB" comment in platformio.ini | Done |
| 26 | Diagnostics uptime — changed from `SKOutputFloat` (millis()/1000.0f) to `SKOutputInt` (millis()/1000); original float→double fix was a no-op since `SKOutputFloat` truncates back to `float` | Done |

## Sprint 11 — Code Review Findings (2026-04-16)

Issues found during code review. Grouped by severity.

### Safety / Correctness

| # | Issue | Description | Complexity |
|---|-------|-------------|------------|
| 27 | Hardware watchdog (consolidated from Sprint 5 #12) | Register ESP32 task watchdog (~8 s timeout); reset in `loop()`. Deregister from TWDT during OTA (`esp_task_wdt_delete` in `ArduinoOTA.onStart`) — OTA blocks `loop()` for 30–90 s. Prerequisite (Sprint 3 #9 relay safety) is done | Low |
| 28 | Bilge fan manual override vs engine start | `manualOn()` latches relay ON, but if the engine starts while latched, the fan stays on through `RUNNING` state — opposite of design intent. Clear `_manualOverride` and call `setRelay(false)` on transition to `RUNNING` | Low |
| 29 | Decouple RPM N2K send rate from measurement rate | `engine_state_machine.cpp` sends PGN 127488 every 100 ms (10 Hz). RPM *calculation* should stay at 10 Hz for smoothing, but the N2K *send* should be a separate callback at 2–4 Hz to reduce bus load | Low |
| 30 | RpmSensor static ISR — enforce single instance | `_pulseCount` / `_lastPulseTime` are static; a second `RpmSensor` instance would silently share state. Add a guard in `begin()` | Low |

### Cleanup / Hygiene

| # | Issue | Description | Complexity |
|---|-------|-------------|------------|
| 31 | N2K address persistence — avoid unnecessary NVS churn | `ReadResetAddressChanged()` check runs every 10 s and opens/closes NVS each time. Address only changes once after boot. Add a `static bool` flag to stop polling after first save | Low |
| 32 | Magic numbers in N2K setup | Product code `100`, device function `160`, device class `25`, manufacturer code `999` are inline in `setupNmea2000()`. Move to named constants in `halmet_config.h` | Low |
| 33 | OTA password mismatch | `platformio.ini` hardcodes `"SomeOTAPassword"` in `[env:halmet-ota]` and in `builder.enable_ota()`. Both should come from `secrets.h` | Low |
| 34 | Extract `SwitchMetadata` from `main.cpp` | One-off inner class clutters main. Move to a small header | Low |
| 35 | `FW_VERSION_STR` from git tag | Pre-build script (`get_version.py`) runs `git describe --tags --always` and injects `-D FW_VERSION_STR=...` into build_flags. Remove hardcoded version from `platformio.ini`. Tag repo with `v1.2.0` to seed. Dev builds automatically show commit distance (e.g. `v1.2.0-3-gabcdef1`) | Low |

### Functional Gaps

| # | Issue | Description | Complexity |
|---|-------|-------------|------------|
| 36 | Gobius mode missing Signal K output | The `#ifdef TANK_SENSOR_GOBIUS` path in `analog_inputs.cpp` updates `st->tankLevelPct` for N2K but never publishes to Signal K. The resistive path has `connect_to(new SKOutputFloat(...))` | Low |
| 37 | Document `EngineState` single-task invariant | All fields are read/written from the Arduino `loop()` task only. Add a comment to `engine_state.h` noting this invariant so future changes don't introduce data races | Low |

## Sprint 12 — Web UI Configuration

| # | Feature | Description | Complexity |
|---|---------|-------------|------------|
| 38 | Configurable N2K engine instance | Replace compile-time `N2K_ENGINE_INSTANCE` with a `PersistingObservableValue<int>` (web UI config, range 0–252). Thread through to `engine_state_machine` and `n2k_publisher` PGN sends | Low |
| 39 | Configurable send intervals | Replace compile-time `INTERVAL_RPM_N2K_MS`, `INTERVAL_N2K_SLOW_MS`, `INTERVAL_SK_SUPPLEMENTAL_MS` with `PersistingObservableValue<int>` web UI config items. Requires re-registering `onRepeat` callbacks or using dynamic interval checks | Medium |
| 40 | Configurable N2K device constants | Replace compile-time `N2K_PRODUCT_CODE`, `N2K_DEVICE_FUNCTION`, `N2K_DEVICE_CLASS`, `N2K_MANUFACTURER_CODE` with web UI config items. Requires restart after change (N2K device info is set once at init) | Low |

## Sprint 13 — Battery Voltage & Coolant Temp Accuracy

| # | Feature | Description | Complexity |
|---|---------|-------------|------------|
| 41 | Battery voltage sensing on A4 | Read supply voltage on ADS ch3 (A4). Wire to ignition-switched +12V rail — HALMET onboard 0–32V conditioning handles scaling. Publish as PGN 127508 (Battery Status) and SK `electrical.batteries.0.voltage`. Also used as reference voltage for #42 | Low |
| 42 | Coolant temp via INA226 current sensor | INA226 on shared I2C bus (GPIO 21/22), shunt in series with VDO sender. Derive sender resistance from `R = V_bus / I_shunt` (INA226 provides both). Map resistance → °C via CurveInterpolator (#43). A1 no longer used for coolant temp. Requires INA226 library in `lib_deps` | Medium |
| 43 | Runtime-configurable temp curve | `CurveInterpolator` mapping sender resistance (Ω) → temperature (°C). Pre-populated with VDO Type A (European) and VDO Type B (US) NTC curves selectable in web UI. User can fine-tune individual points. Depends on #42 | Medium |

### Archived assumptions (Sprint 13, pre-redesign)

The following were discussed but NOT validated against the actual hardware. Do not use without verification:
- Assumed circuit: +12V → gauge coil → sender → GND, A1 at junction
- Assumed gauge coil resistance ~100 Ω (from design spec comment, unverified)
- Assumed VDO Type A (European) NTC curve: 287Ω/40°C → 16Ω/120°C
- Assumed VDO Type B (US) NTC curve: 648Ω/40°C → 20Ω/120°C
- Assumed zero gauge resistance for direct resistance mapping
- Assumed voltage ratio correction using A4 supply voltage as reference
- None of the above has been confirmed on the actual boat

## Sprint 14 — Code Review Findings (2026-04-16, Sprint 13)

Issues found during post-Sprint-13 code review.

### Safety / Correctness

| # | Issue | Description | Complexity |
|---|-------|-------------|------------|
| 44 | ~~Wrong NMEA2000 function name in `sendBatteryStatus`~~ | **False positive — closed.** `SetN2kDCBatStatus` is a valid inline alias for `SetN2kPGN127508` confirmed in `N2kMessages.h:2123`. No change needed. | N/A |
| 45 | INA226 has no health flag | **Done.** Added `ina226Ok` to `EngineState`, captured `gIna226.init()` result in setup, guarded lambda with `if (!ina \|\| !st->ina226Ok) return`. | Low |

### Design / Fragility

| # | Issue | Description | Complexity |
|---|-------|-------------|------------|
| 46 | `CurveInterpolator` used as synchronous function | `resCurve->set(resistance)` then `resCurve->get()` works today because no downstream observers are wired to `resCurve`. If a future developer wires one (e.g. an SK output), it will fire every 500 ms even when faulted. Add a comment documenting the invariant, or refactor to a proper reactive chain. | Low |

### Cleanup / Hygiene

| # | Issue | Description | Complexity |
|---|-------|-------------|------------|
| 47 | `TEMP_CURVE_POINTS` macro still defined | **Done.** Removed dead macro and comment block from `halmet_config.h`. | Low |
| 48 | Legacy voltage fault constants not removed | **Done.** Removed `COOLANT_VOLT_MIN_V` and `COOLANT_VOLT_MAX_V` from `halmet_config.h`. | Low |
| 49 | Battery N2K send guard too loose | **Done.** Changed to `if (st->adsOk && st->supplyVoltageV > 0.0f)` in `n2k_publisher.cpp`. | Low |
| 50 | `gVoltageMultiplier` declared inside `setup()` | **Done.** Renamed to `voltageMultiplier` (dropped `g` prefix). | Low |

## Candidate Pool — FROZEN (do not pick up unless explicitly ordered)

Features evaluated and deliberately deferred. Do **not** schedule, implement, or re-evaluate these without a direct instruction from the project owner.

| Feature | Reason deferred |
|---------|----------------|
| Two-tank support (second PGN 127505 instance) | Single tank with two Gobius threshold sensors — no second tank to monitor |
| Engine hours counter | Persist accumulated runtime seconds to LittleFS in 1-minute increments. Send in PGN 127489 `EngineTotalHours`. Deferred — low priority for current usage pattern |
| I2C LCD display (2×16 ASCII) showing engine temp, RPM, voltage (from N2K bus), configurable via web UI | Requires I2C display driver, N2K bus listener for voltage PGN, web UI config for display layout |
| Live temperature in 1-Wire config card description (#18) | Low priority UX polish; sensors can be identified by warming/cooling and checking SK diagnostics output |
| Hot-reload 1-Wire sensor assignments without restart (#19) | Medium complexity; SensESP has a restart button in the navbar that serves as a workaround |
