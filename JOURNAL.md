# Work Journal

## 2026-04-14 — Imported into SensESP workspace

Project imported from `../halmet-engine-monitor` with full git history (20+ commits across 9 sprints). Created SPEC.md and JOURNAL.md for workspace integration.

### Project status at import

**Completed sprints:**
- Sprint 1: Safety & quick wins (alarm debouncing, coolant fault detection, stale data guard, I2C recovery, fluid type fix)
- Sprint 2: High-value features (cold-boot coolant fix, temperature threshold alerting, diagnostics heartbeat)
- Sprint 2.5: 1-Wire configurable destination per sensor slot
- Sprint 3: OTA relay safety, firmware version in N2K product info, additional 1-Wire destinations
- Sprint 4: Architecture refactor (shared EngineState struct, decomposed main.cpp into modules)
- Sprint 6: Sensor-centric 1-Wire config (ROM-based config cards with dropdown destination picker)
- Sprint 8: N2K bilge fan switch control (PGN 127502 receive, PGN 127501 report, SK PUT)
- Sprint 9: Resistive tank sender (CurveInterpolator, VDO 10-180 ohm default, Gobius mode retained via build flag)

**Remaining work:**
- Sprint 5: Hardware watchdog (deferred — OTA bricking risk, needs careful implementation)
- Sprint 7: 1-Wire UX polish (live temp in config card descriptions, hot-reload sensor assignments)
- Candidate pool: frozen, do not pick up without explicit instruction

**Build status:** Compiles successfully. Not yet tested on hardware in this workspace.

## 2026-04-14 — Code review

Full firmware review across correctness, hardware compatibility, SensESP patterns, and performance.

**Result: No must-fix issues.** The firmware is safe and well-structured.

**5 should-fix / suggestion items added to roadmap as Sprint 10:**
- #22: RPM volatile read TOCTOU race (minor, one-tick false zero at worst)
- #23: Stale I/O map comment in main.cpp (misleading after Sprint 9)
- #24: Dead OneWireSensors files (cleanup)
- #25: Explicit flash size in platformio.ini (correctness)
- #26: Diagnostics uptime float precision (cosmetic)

All trivial fixes, no functional impact on running firmware.

### Sprint 10 implementation
- Discussed plan with new hire — they improved 3 of 5 items:
  - #22: Consolidate into one critical section instead of patching the second (cleaner)
  - #24: Also clean up README.md and init_repo.ps1 references
  - #25: Fix wrong "8 MB" comment (board has 16 MB)
  - #26: Caught that SKOutputFloat truncates to float, making the double fix a no-op → switched to SKOutputInt with integer seconds instead
- All 5 fixes implemented and committed.
- **Build not verified** — PlatformIO not installed on this machine. Needs `pio run -e halmet` before flashing.

<<<<<<< HEAD
## 2026-04-16 — Sprints 11–14

### Sprint 11 — Quick wins (multiple commits)

Seven items from the roadmap candidate and review pools implemented:
- #27/#28/#29/#30: Hardware watchdog consolidated, bilge manual override bug fixed, RPM N2K rate decoupled from measurement rate, RPM single-instance guard
- #31–#37: N2K address persistence fix, N2K device constants to named macros, OTA password from secrets.h, SwitchMetadata header, FW_VERSION_STR from git tag, Gobius SK output, EngineState invariant documented
- Rebased to SensESP 3.3.0

### Sprint 12 — Web UI configuration (#38 #39 #40)

All three items implemented:
- N2K engine instance now configurable via web UI (`/n2k/engine_instance`)
- Send intervals (RPM N2K, slow N2K, SK supplemental) configurable via web UI — require restart
- N2K device constants (product code, device function, device class, manufacturer code) configurable via web UI — require restart
- `setupNmea2000()` moved to run **after** SensESP app builder so `PersistingObservableValue` config items are loaded before N2K init
- Build verified.

### Sprint 13 — Battery voltage & coolant temp accuracy (#41 #42 #43)

Three significant features:
- **#41 Battery voltage**: A4 / ADS ch3 reads supply voltage via HALMET's onboard 20 kΩ/2.2 kΩ divider (10.09:1). Published as PGN 127508 and SK `electrical.batteries.0.voltage`. Multiplier is runtime-configurable.
- **#42 Coolant temp via INA226**: A1 voltage-based approach retired. INA226 current sensor placed on shared I2C bus (0x40). Shunt resistor (100 mΩ) in series with VDO sender. Firmware computes `R = V_bus / I_shunt` — direct resistance regardless of supply voltage or gauge coil uncertainty.
- **#43 VDO NTC CurveInterpolator**: `CurveInterpolator` pre-populated with VDO Type A (European) NTC curve (287 Ω/40 °C → 16 Ω/120 °C). User-editable via web UI at `/coolant/resistance_curve`.
- Added INA226_WE to lib_deps. Build verified.

### Sprint 14 — Post-Sprint-13 code review (#44–#50)

Code review after Sprint 13 found 7 items:
- #44: False positive — `SetN2kDCBatStatus` is a valid alias, confirmed in N2kMessages.h. Closed without change.
- #45: Added `ina226Ok` flag to `EngineState`; guarded INA226 callbacks. Protects against missing hardware.
- #46: Noted (not fixed): `CurveInterpolator` used synchronously — safe today, document the invariant before adding downstream observers.
- #47/#48: Removed dead macros `TEMP_CURVE_POINTS`, `COOLANT_VOLT_MIN_V`, `COOLANT_VOLT_MAX_V` from `halmet_config.h`.
- #49: Battery N2K send guard tightened to `if (st->adsOk && st->supplyVoltageV > 0.0f)`.
- #50: Renamed `gVoltageMultiplier` → `voltageMultiplier` (local variable, `g` prefix was incorrect).
- Build verified.
=======
## 2026-04-14 — Build verified in SensESP workspace

Session: new-project import

- Installed PlatformIO 6.1.19 via pip on this machine.
- Created placeholder `src/secrets.h` (dummy SSID/password/SK IP) — gitignored, needs real values before flashing.
- Build succeeds: 1.68 MB / 51% flash, 17% RAM. All libraries resolved correctly.
- HALMET not connected to this machine — flash deferred until device is available.
- **Next step**: Flash to device and run hardware testing (Phase 7).
>>>>>>> e6a8a81 (chores: PIO update and secrets placeholder)
