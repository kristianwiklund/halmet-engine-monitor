# HALMET Engine & Tank Monitor — Specification

## Goal

Monitor a Volvo Penta MD7A diesel engine and fuel tank on a 1981 Sunwind 29 sailboat. Provide engine RPM, oil pressure and coolant temperature warnings, coolant temperature reading, fuel tank level, distributed 1-Wire temperature sensing, and automatic bilge fan purge control. Primary output is NMEA 2000; Signal K via WiFi is supplemental.

## Hardware

- **Board**: Hat Labs HALMET (ESP32-WROOM-32E, 16 MB flash)
- **Framework**: SensESP v3 + NMEA2000 library (Timo Lappalainen)
- **Signal K server**: HALpi2 on the boat's WiFi network

## Sensors/Inputs

| Input | Signal | Connection |
|-------|--------|------------|
| D1 / GPIO 23 | Alternator W-terminal (RPM pulse) | Via conditioning circuit |
| D2 / GPIO 25 | Oil pressure warning switch | Active-low, normally-open to GND |
| D3 / GPIO 27 | Coolant temperature warning switch | Active-low, normally-open to GND |
| D4 / GPIO 26 | Ignition key sense (optional) | +12V present when key ON |
| A1 / ADS ch0 | Coolant temp sender voltage | Passive, parallel with VP gauge |
| A2 / ADS ch1 | Fuel tank (resistive, 10 mA CCS) | VDO 10-180 ohm default curve |
| 1-Wire / GPIO 4 | Up to 6x DS18B20 temperature probes | Sensor-centric config (ROM-keyed) |

## Outputs

| Output | Type | Details |
|--------|------|---------|
| GPIO 32 | Bilge fan relay | ON during PURGE state only |
| GPIO 33 | Warning lamp | HIGH when oil pressure or coolant alarm active |
| NMEA 2000 | PGN 127488, 127489, 127501, 127505, 130316 | Engine, tank, switch, temperature data |
| Signal K | WiFi WebSocket | Supplemental: bilge fan state, ignition, notifications |

## Expected Behavior

- Engine RPM displayed on N2K instruments via PGN 127488
- Oil pressure and coolant overtemp warnings transmitted in PGN 127489 status bits and drive warning lamp
- Coolant temperature read from analog sender, transmitted in PGN 127489 with configurable warn/alarm thresholds (Signal K notifications)
- Fuel tank level from resistive sender via CurveInterpolator, transmitted in PGN 127505
- 1-Wire sensors each configurable to a destination (engine room, exhaust, sea water, cabin, etc.) via web UI dropdown
- Bilge fan runs a timed purge cycle after engine shutdown; controllable via N2K PGN 127502 or Signal K PUT
- OTA firmware updates supported; relay forced OFF during OTA flash
- Diagnostics heartbeat (uptime, firmware version, ADS fail count) sent to Signal K every 10s

## Dependencies

```ini
lib_deps =
    SignalK/SensESP @ ^3.1.0
    ttlappalainen/NMEA2000-library
    https://github.com/ttlappalainen/NMEA2000_esp32.git
    esp_websocket_client=https://components.espressif.com/api/downloads/?object_type=component&object_id=dbc87006-9a4b-45e6-a6ab-b286174cb413
    SensESP/OneWire @ ^3.0.1
    adafruit/Adafruit ADS1X15 @ ^2.5
```
