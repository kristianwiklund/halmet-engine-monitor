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
| Tank levels | Gobius Pro OUT1 binary on A2/A3 → PGN 127505 |
| Bilge fan purge | Relay on GPIO 32; runs after engine stop for configurable time |
| Ignition key sense | D4 / GPIO 26 (optional) → Signal K `electrical.switches.ignition.state` |

## Hardware Wiring Quick Reference

```
D1 / GPIO 23   ← Alternator W-terminal (via 1 kΩ + diode clamp circuit)
D2 / GPIO 25   ← Oil pressure switch  (one side), other side to GND
D3 / GPIO 27   ← Temp warning switch  (one side), other side to GND
D4 / GPIO 26   ← Ignition key +12 V rail (optional)
A1 / ADS ch0   ← VP coolant temp sender terminal (parallel to existing gauge)
A2 / ADS ch1   ← Gobius Pro #1 OUT1  (10 kΩ pull-up to +3.3 V)
A3 / ADS ch2   ← Gobius Pro #2 OUT1  (10 kΩ pull-up to +3.3 V)
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
| `/tank/tank1_capacity_l` | 100 L | Tank 1 volume for PGN 127505 |
| `/tank/tank2_capacity_l` | 100 L | Tank 2 volume for PGN 127505 |

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

- [SensESP v3](https://github.com/SignalK/SensESP)
- [NMEA2000](https://github.com/ttlappalainen/NMEA2000)
- [NMEA2000_esp32](https://github.com/ttlappalainen/NMEA2000_esp32)
- [OneWire](https://github.com/PaulStoffregen/OneWire)
- [DallasTemperature](https://github.com/milesburton/Arduino-Temperature-Control-Library)
- [Adafruit ADS1X15](https://github.com/adafruit/Adafruit_ADS1X15)

## License

MIT — see `LICENSE`.
