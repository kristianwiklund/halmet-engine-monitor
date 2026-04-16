# Disclaimer

Completely untested, and 99% was written by Claude - I will do more work on this including QA
once spring arrives and it is possible to do work in the actual boat again.

# HALMET Marine Engine & Tank Monitor

Firmware for the [Hat Labs HALMET](https://docs.hatlabs.fi/halmet/) board,
monitoring a **Volvo Penta MD7A** diesel engine via NMEA 2000.

## Features

| Feature | Implementation |
|---|---|
| Engine RPM | Alternator W-terminal pulse counter → PGN 127488 |
| Coolant temperature | INA226 current sensor on shared I2C bus. Derives sender resistance from `V_bus / I_shunt`; maps resistance → °C via runtime-editable VDO Type A NTC curve → PGN 127489 |
| Oil pressure warning | Binary switch on D2 (active-low) → PGN 127489 status bit |
| Temperature warning | Binary switch on D3 (active-low) → PGN 127489 status bit |
| Engine room temps | DS18B20 1-Wire chain on GPIO 4 → PGN 130316 |
| Tank level | Resistive sender (VDO 10–180 Ω) on A2 via 10 mA CCS → PGN 127505; runtime-calibratable curve (Gobius 3-band mode via `-D TANK_SENSOR_GOBIUS`) |
| Battery voltage | Supply voltage on A4 / ADS ch3, 20 kΩ/2.2 kΩ divider → PGN 127508 + SK `electrical.batteries.0.voltage` |
| Bilge fan purge | Relay on GPIO 32; runs after engine stop for configurable time |
| N2K bilge fan switch | PGN 127502 receive (MFD manual on/off) + PGN 127501 Binary Switch Bank Status at 1 Hz |
| Warning lamp | GPIO 33 HIGH when oil or coolant alarm active |
| Ignition key sense | D4 / GPIO 26 (optional) → Signal K `electrical.switches.ignition.state` |

## Hardware Wiring Quick Reference

```
D1 / GPIO 23   ← Alternator W-terminal (via 1 kΩ + diode clamp circuit)
D2 / GPIO 25   ← Oil pressure switch  (one side), other side to GND
D3 / GPIO 27   ← Temp warning switch  (one side), other side to GND
D4 / GPIO 26   ← Ignition key +12 V rail (optional)
A1 / ADS ch0   ← Spare (coolant temp no longer read from A1 — see INA226 below)
A2 / ADS ch1   ← Resistive tank sender (10 mA CCS, VDO 10–180 Ω)  — enable CCS jumper on A2
A3 / ADS ch2   ← Gobius sensor B OUT1 (Gobius mode only, 10 kΩ pull-up to +3.3 V)
A4 / ADS ch3   ← Battery / supply voltage (20 kΩ/2.2 kΩ divider, 0–32 V)
I2C (GPIO 21/22) ← INA226 current sensor (shunt in series with VDO coolant sender)
GPIO 4         ← DS18B20 1-Wire DQ   (pull-up built into HALMET)
GPIO 32        → Bilge fan relay module IN
GPIO 33        → Warning lamp (HIGH when oil/coolant alarm active)
```

See `HALMET_Marine_Engine_Monitor_Design.md` for full electrical details
and commissioning procedures.

## Setup

Before building, copy the secrets template and fill in your network details:

```bash
cp src/secrets.h.example src/secrets.h
# Edit src/secrets.h with your WiFi SSID, password, and Signal K server address
```

`src/secrets.h` is gitignored and will not be committed.

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
| `/tank/resistance_curve` | VDO 10–180 Ω | CurveInterpolator: resistance (Ω) → level ratio (0–1). Edit in web UI to match your sender. |
| `/coolant/resistance_curve` | VDO Type A NTC | CurveInterpolator: resistance (Ω) → temperature (°C). Pre-loaded with European VDO curve. User-editable. |
| `/coolant/warn_threshold_c` | 95 °C | Signal K warn notification threshold |
| `/coolant/alarm_threshold_c` | 105 °C | Signal K alarm notification threshold |
| `/voltage/multiplier` | 10.09 | A4 voltage divider multiplier. HALMET uses 20 kΩ/2.2 kΩ → (20+2.2)/2.2 = 10.09. Adjust if resistors differ. |
| `/n2k/engine_instance` | 0 | NMEA 2000 engine instance (0–252) |
| `/n2k/product_code` | 100 | N2K product code (requires restart) |
| `/n2k/device_function` | 160 | N2K device function — 160 = Engine Gateway (requires restart) |
| `/n2k/device_class` | 25 | N2K device class — 25 = Propulsion (requires restart) |
| `/n2k/manufacturer_code` | 999 | N2K manufacturer code — 999 = uncertified placeholder (requires restart) |
| `/intervals/rpm_n2k_ms` | 250 ms | PGN 127488 send interval (requires restart) |
| `/intervals/n2k_slow_ms` | 1000 ms | PGN 127489/127505/127501/127508 send interval (requires restart) |
| `/intervals/sk_supplemental_ms` | 5000 ms | Signal K-only data interval (requires restart) |

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
├── platformio.ini
├── include/
│   ├── halmet_config.h         Compile-time defaults & pin definitions
│   ├── engine_state.h          Shared EngineState struct & CoolantAlertState enum
│   ├── BilgeFan.h              Bilge fan purge state machine
│   ├── RpmSensor.h             Alternator W-terminal RPM counter
│   ├── N2kSenders.h            NMEA 2000 PGN construction helpers
│   ├── analog_inputs.h         ADS1115 coolant temp & tank level callbacks
│   ├── digital_alarms.h        Oil/temp alarm debounce callbacks
│   ├── engine_state_machine.h  RPM debounce & engine running detection
│   ├── n2k_publisher.h         Periodic N2K PGN send callbacks
│   ├── onewire_setup.h         1-Wire bus scan & sensor-centric dest config
│   └── diagnostics.h           SK heartbeat (uptime, version, reset reason)
└── src/
    ├── main.cpp
    ├── BilgeFan.cpp
    ├── RpmSensor.cpp
    ├── N2kSenders.cpp
    ├── analog_inputs.cpp
    ├── digital_alarms.cpp
    ├── engine_state_machine.cpp
    ├── n2k_publisher.cpp
    ├── onewire_setup.cpp
    └── diagnostics.cpp
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
| INA226_WE | `wollewald/INA226_WE` | I2C current/voltage sensor for coolant sender resistance measurement |

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
