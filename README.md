# HALMET Marine Engine & Tank Monitor

Firmware for the [Hat Labs HALMET](https://docs.hatlabs.fi/halmet/) board,
monitoring a **Volvo Penta MD7A** diesel engine via NMEA 2000.

## Features

| Feature | Implementation |
|---|---|
| Engine RPM | Alternator W-terminal pulse counter → PGN 127488 |
| Coolant temperature | VP/VDO NTC sender on A1, parallel to gauge → PGN 127489 |
| Oil pressure warning | Binary switch on D2 (active-low) → PGN 127489 status bit |
| Temperature warning | Binary switch on D3 (active-low) → PGN 127489 status bit |
| Engine room temps | DS18B20 1-Wire chain on GPIO 4 → PGN 130316 |
| Tank level | Two Gobius Pro sensors (3/4 + 1/4 thresholds) on A2/A3 → single PGN 127505 |
| Bilge fan purge | Relay on GPIO 32; runs after engine stop for configurable time |
| Ignition key sense | D4 / GPIO 26 (optional) → Signal K `electrical.switches.ignition.state` |

## Hardware Wiring Quick Reference

```
D1 / GPIO 23   ← Alternator W-terminal (via 1 kΩ + diode clamp circuit)
D2 / GPIO 25   ← Oil pressure switch  (one side), other side to GND
D3 / GPIO 27   ← Temp warning switch  (one side), other side to GND
D4 / GPIO 26   ← Ignition key +12 V rail (optional)
A1 / ADS ch0   ← VP coolant temp sender terminal (parallel to existing gauge)
A2 / ADS ch1   ← Gobius Pro sensor A OUT1  (10 kΩ pull-up to +3.3 V)  ← "below 3/4"
A3 / ADS ch2   ← Gobius Pro sensor B OUT1  (10 kΩ pull-up to +3.3 V)  ← "below 1/4"
GPIO 4         ← DS18B20 1-Wire DQ   (4.7 kΩ pull-up to +3.3 V)
GPIO 32        → Bilge fan relay module IN
```

See `HALMET_Marine_Engine_Monitor_Design.md` for full electrical details
and commissioning procedures.

## Build & Flash

```bash
# Install PlatformIO CLI or open in VSCode with PlatformIO IDE extension
pip install platformio

# Build
pio run -e halmet

# Upload (board connected via USB)
pio run -e halmet -t upload

# Monitor serial output
pio device monitor -b 115200
```

## Configuration

All runtime parameters are adjustable via the SensESP web UI at
`http://halmet-engine.local/config` (mDNS) or `http://<board-ip>/config`:

| Parameter | Default | Description |
|---|---|---|
| `/rpm/pulses_per_rev` | 10.0 | W-terminal pulses per crankshaft rev — **calibrate first!** |
| `/rpm/running_threshold` | 200 RPM | RPM above which engine is "running" |
| `/bilge/purge_duration_s` | 600 s | Bilge fan on-time after engine stop |
| `/tank/capacity_l` | 100 L | Tank volume for PGN 127505 |

## RPM Calibration

1. Start the engine.
2. Open `http://halmet-engine.local` or Serial monitor.
3. Compare reported RPM against a handheld optical tachometer.
4. Adjust `/rpm/pulses_per_rev` in the web UI until both agree.
5. For a Paris Rhone 14-V alternator on the MD7A, expect a value in the
   range **10–14** (6 pole pairs × ~1.8–2.3 pulley ratio).

## Project Structure

```
halmet-engine/
├── platformio.ini          PlatformIO project & build config
├── include/
│   ├── halmet_config.h     Compile-time defaults & pin definitions
│   ├── BilgeFan.h          Bilge fan purge state machine
│   ├── RpmSensor.h         Alternator W-terminal RPM counter
│   ├── OneWireSensors.h    DS18B20 1-Wire chain manager
│   └── N2kSenders.h        NMEA 2000 PGN transmission helpers
└── src/
    ├── main.cpp            Application entry point & main loop
    ├── BilgeFan.cpp
    ├── RpmSensor.cpp
    ├── OneWireSensors.cpp
    └── N2kSenders.cpp
```

## Dependencies

Managed automatically by PlatformIO from `platformio.ini`:

| Library | Source | Notes |
|---|---|---|
| SensESP v3 | `SignalK/SensESP @ ^3.1.0` | PlatformIO registry |
| NMEA2000-library | `ttlappalainen/NMEA2000-library` | Registry name has a **hyphen** |
| NMEA2000_esp32 | GitHub URL | Not in registry — pulled directly |
| esp_websocket_client | IDF Component Registry URL | IDF 5.x managed component |
| SensESP/OneWire | `SensESP/OneWire @ ^3.0.1` | Replaces raw OneWire + DallasTemperature |
| Adafruit ADS1X15 | `adafruit/Adafruit ADS1X15 @ ^2.5` | PlatformIO registry |

## Troubleshooting

### `UnknownPackageError: Could not find the package with 'ttlappalainen/NMEA2000 @ ^4.19'`

Three separate issues were present in the original configuration — all fixed in the current `platformio.ini`:

**1. Wrong NMEA2000 library name**
The PlatformIO registry name is `ttlappalainen/NMEA2000-library` (with a hyphen). The name `ttlappalainen/NMEA2000` does not exist in the registry. Additionally, the library uses a date-based version scheme — there is no version `4.19`, so the `@ ^4.19` specifier also fails. Omitting the version constraint lets PlatformIO pick the latest published release.

```ini
; ❌ Wrong — does not exist in registry
ttlappalainen/NMEA2000 @ ^4.19

; ✅ Correct
ttlappalainen/NMEA2000-library
```

**2. Wrong `NMEA2000_esp32` specifier**
The ESP32 TWAI/CAN driver is not published to the PlatformIO registry at all, so `ttlappalainen/NMEA2000_esp32 @ ^1.0` also fails with `UnknownPackageError`. It must be pulled directly from GitHub:

```ini
; ❌ Wrong — not in registry
ttlappalainen/NMEA2000_esp32 @ ^1.0

; ✅ Correct
https://github.com/ttlappalainen/NMEA2000_esp32.git
```

**3. Wrong platform for SensESP v3**
SensESP v3 requires Arduino ESP32 Core 3.x (ESP-IDF 5.x). The official PlatformIO `espressif32` platform is frozen at Core 2.0.17 (Espressif ended support for the official PlatformIO platform in late 2023 after the Core 3.x release). The SensESP project itself uses the community **pioarduino** fork:

```ini
; ❌ Wrong — frozen at Arduino Core 2.0.17, incompatible with SensESP v3
platform = espressif32

; ✅ Correct — pioarduino, tracks Arduino Core 3.x releases
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
```

Note that pioarduino also requires two additional `build_flags` that SensESP v3 needs:
```ini
build_flags =
    -D USE_ESP_IDF_LOG
    -D CORE_DEBUG_LEVEL=ARDUHAL_LOG_LEVEL_WARN
```

### `fatal error: esp_websocket_client.h: No such file or directory`

In ESP-IDF 4.x, `esp_websocket_client` was bundled with the SDK. In IDF 5.x (pioarduino) it became a separately managed component. Declare it in `lib_deps` using the `name=url` syntax so PlatformIO registers it as a named IDF component:

```ini
lib_deps =
    esp_websocket_client=https://components.espressif.com/api/downloads/?object_type=component&object_id=dbc87006-9a4b-45e6-a6ab-b286174cb413
```

Note the `name=` prefix — a bare URL without it causes the component to be treated as an anonymous download and not wired into the IDF component build system correctly.

### `fatal error: OneWire.h: No such file or directory` / `sensesp.h` / `NMEA2000.h`

All three share the same root cause: PlatformIO's default Library Dependency Finder mode is `chain`, which only scans one level of `#include` directives. It reads your `src/` files, finds the libraries they include directly, but **stops there** — it does not recurse into those libraries' own headers.

`OneWire.h`, `sensesp.h`, and `NMEA2000.h` are all included from **inside** library headers (not directly from `src/`), so LDF in `chain` mode never discovers them.

The fix is two lines:

```ini
; Recurse into all library headers to find transitive dependencies
lib_ldf_mode = deep

; Link object files directly — required by pioarduino (weak-symbol handling)
lib_archive = no
```

`lib_ldf_mode = deep` makes LDF recurse into every header of every library it finds, so transitive dependencies are discovered automatically. `lib_archive = no` is a separate but related pioarduino requirement: without it the linker discards weak-symbol overrides in the ESP-IDF framework, causing obscure link failures after compilation succeeds.

## License

MIT — see `LICENSE`.
