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
