# HALMET Marine Engine & Tank Monitor — Design Specification

**Target Hardware:** Hat Labs HALMET (ESP32-WROOM-32E, SensESP v3 framework)
**Engine:** Volvo Penta MD7A (Paris Rhone alternator)
**Primary bus:** NMEA 2000 (WiFi as fallback / supplemental)
**Framework:** SensESP v3 + NMEA2000 library (Timo Lappalainen)

---

## 1. Hardware Overview

The HALMET board provides the following I/O natively:

| Resource | Count | GPIO / Interface | Notes |
|---|---|---|---|
| Digital inputs (D1–D4) | 4 | GPIO 23, 25, 27, 26 | Galvanically isolated, ±32 V tolerant, Schmitt-triggered |
| Analog inputs (A1–A4) | 4 | ADS1115 I²C ADC | 16-bit, 0–32 V range, 160 Hz LPF, 10 mA CCS for resistance mode |
| 1-Wire bus | 1 | GPIO 4 | 3-pin 2.54 mm header |
| I²C bus | 1 | GPIO 21 (SDA), 22 (SCL) | 4-pin header |
| GPIO header | 13 | 2×10 pin, 2.54 mm | Broken-out ESP32 GPIOs |
| NMEA 2000 | 1 | Built-in CAN transceiver | Powers the board too (5–32 V) |
| WiFi / BLE | 1 | ESP32 integrated | 802.11 b/g/n + BT 4.2 / BLE |

The board does **not** have a relay output on-board. A small relay module must be attached via the GPIO header (see §2.5).

---

## 2. I/O Assignment & Signal Mapping

### 2.1 Digital Inputs

| Input | Signal | Notes |
|---|---|---|
| D1 / GPIO 23 | Alternator W-terminal → RPM pulse | Via conditioning circuit (§3.1) |
| D2 / GPIO 25 | Oil pressure warning (binary, active-low) | Normally-open switch to GND |
| D3 / GPIO 27 | Temperature warning (binary, active-low) | Normally-open switch to GND |
| D4 / GPIO 26 | Ignition / starter key sense *(optional, §2.6)* | +12 V present when key ON |

### 2.2 Analog Inputs

| Input | Signal | Mode | Notes |
|---|---|---|---|
| A1 | Coolant/oil temperature sender | Passive voltage | In parallel with VP gauge, high-impedance |
| A2 | Gobius Pro tank 1 — digital OUT1 | Passive voltage or resistance | See §3.3 |
| A3 | Gobius Pro tank 2 — digital OUT1 | Passive voltage or resistance | See §3.3 |
| A4 | Spare (e.g. battery voltage sense) | Passive voltage | Optional |

### 2.3 1-Wire Bus (GPIO 4)

Used for DS18B20 temperature probes scattered across the engine room. Up to ~10 sensors on a single parasitic or powered bus.

### 2.4 I²C Bus (GPIO 21/22)

Reserved for the onboard ADS1115 ADC. **Do not connect other I²C devices while NMEA 2000 isolation is required** — the I²C header shares ground with the MCU and would break galvanic isolation.

### 2.5 GPIO Header — Relay Output

One free GPIO (e.g. GPIO 32) is used to drive the bilge fan relay. Attach a small DIN-rail or PCB relay module rated for 12 V coil, 10 A/12 V contacts. Interpose a flyback diode if using a bare relay coil directly. Many pre-built relay modules (SRD-05VDC-SL-C style with an optocoupler) work directly at 3.3 V logic level, which makes them ideal here.

```
HALMET GPIO 32 ──→ Relay module IN
HALMET GND      ──→ Relay module GND
12 V supply     ──→ Relay module VCC
Relay COM       ──→ 12 V (fused, 5 A)
Relay NO        ──→ Bilge fan +12 V
```

### 2.6 Optional Ignition Sense (D4)

Use D4 to sense +12 V on the ignition rail (the switched terminal of the starter key). A simple voltage divider (15 kΩ top, 10 kΩ bottom) keeps the input safely within the 5 V logic threshold even on a 24 V system if ever needed. On 12 V this is unnecessary — D4 is already tolerant to 32 V.

---

## 3. Electrical Interfaces

### 3.1 Alternator W-Terminal to D1 (RPM Input)

**Background:** The Paris Rhone alternator on the Volvo Penta MD7A has a **W terminal** (sometimes also called P or stator terminal). It outputs an unrectified, floating AC sine-like waveform whose frequency is proportional to alternator speed. Typical amplitudes range from a few volts at idle to ~14 V peak-to-peak at full speed. The frequency depends on the number of pole pairs and the pulley ratio.

For a common 6-pole-pair alternator with a 2.2:1 pulley ratio:
- Engine idle (~750 RPM) → alternator ~1650 RPM → ~150 Hz at W terminal
- Engine max (~3000 RPM) → alternator ~6600 RPM → ~600 Hz

**Conditioning circuit** (keeps everything low-cost with standard parts):

```
W terminal ──[1 kΩ]──┬── D1 input of HALMET
                     │
                    [1N4148]  (anode to rail, cathode to +5V)   ← clamp high
                     │
                    [1N4148]  (cathode to rail, anode to GND)   ← clamp low
                     │
                    GND (engine block)
```

More robustly, use a small signal transformer or a purpose-designed tach interface IC (e.g. the LM2917 frequency-to-voltage converter), but the resistor + dual-diode clamp works well for the HALMET because:
- D1 is Schmitt-triggered with a 1.5 V threshold, providing hysteresis against noise
- The input is already galvanically isolated from the NMEA 2000 bus
- The LP (low-pass) solder jumper on the back of the HALMET should be **left open** for the RPM input — the 160 Hz hardware LPF on the analog path does not apply to the digital inputs, and the firmware's pulse counter works directly on the raw edge count

**Inline fuse:** Add a 100 mA slow-blow fuse in series with the wire from the W terminal to protect against chafing shorts.

**Pulse-per-revolution calibration:** In firmware, configure `pulses_per_revolution` as:

```
pulses_per_rev = pole_pairs × (alternator_pulley_diameter / engine_pulley_diameter)
```

This value is typically 6–14 depending on the specific alternator. Measure pulley diameters with a calliper and verify against a handheld optical tachometer during commissioning.

### 3.2 Temperature & Alarm Senders (Volvo Penta MD7A)

The MD7A uses VDO-compatible senders that are ground-referenced through the engine block.

**Temperature sender (to A1):**

The original gauge connects between the switched ignition supply (+12 V via ignition switch) and the sender terminal. The sender resistance drops as temperature rises (typical VDO NTC characteristic: ~250 Ω at 40°C, ~50 Ω at 100°C, ~20 Ω at 120°C). The HALMET should be connected in **passive voltage measurement mode** — i.e., A1 measures the voltage at the sender terminal with respect to the engine block ground, in parallel with the existing gauge. Leave the A1 current-source jumper **open** (jumper removed). The existing gauge provides the excitation.

```
Ignition +12V ──── [Gauge coil ~100 Ω] ──┬──── [Sender NTC]  ──── GND (block)
                                          │
                                      A1 of HALMET (high-impedance)
```

A CurveInterpolator in firmware maps the measured voltage to degrees Celsius using a calibration table derived from the VDO sender spec or measured empirically.

**Oil pressure warning switch (to D2):** This is a normally-closed switch that opens when pressure drops below ~0.5 bar. Connect one side to D2, other side to GND (engine block). D2 sees a floating high state (pulled up internally by HALMET) when oil pressure is good, and a short-to-GND when the warning activates. Configure in firmware as an active-low alarm input.

**Temperature warning switch (to D3):** Same wiring as D2 — normally open, closes to GND on high temperature. Active-low.

### 3.3 Gobius Pro Tank Sensors

Each Gobius Pro sensor provides **two digital outputs** (OUT1 and OUT2) that switch between open-collector GND and floating (high-impedance open), representing configurable tank level thresholds. Supply voltage for the sensors is 12–24 V.

**Recommended approach — use the wired digital outputs, not BLE:**

Although the ESP32 has BLE, the Gobius Pro uses a proprietary BLE profile documented only for their own smartphone app. Reverse-engineering that protocol is fragile and unsupported. The wired outputs are simpler and more reliable.

Each Gobius Pro OUT pin can be treated as a binary level signal. For two sensors (two tanks), use two analog inputs (A2, A3) in passive voltage mode with a pull-up resistor:

```
+3.3V (from HALMET GPIO header VCC) ──[10 kΩ]──┬── A2 (or A3)
                                                │
                                          Gobius OUT1
                                                │
                                               GND
```

When the Gobius output is floating (high): A2 reads ~3.3 V → "threshold NOT reached"
When the Gobius output sinks to GND: A2 reads 0 V → "threshold reached"

Since each Gobius Pro sensor has two outputs (allowing two configurable thresholds per tank), you could use two digital inputs per sensor instead. With two sensors and using D1 for RPM, the remaining D2 and D3 are reserved for alarms — so the analog approach frees up all digital inputs for their intended purpose.

**For a more continuous level reading (not just binary):** Use the Gobius Hub accessory, which provides a 10–180 Ω or 240–33 Ω resistive output — directly compatible with HALMET's active resistance measurement mode (install the A-input current source jumper, no external gauge).

### 3.4 1-Wire Temperature Sensors (Engine Room)

DS18B20 waterproof probes wired in a bus topology from the 1-Wire header (GPIO 4). Recommended wiring for long cable runs (up to ~20 m total):

```
HALMET 1W header:  VDD ─┬─ [4.7 kΩ] ─── DQ (data)
                        │
                     All DS18B20 VDD pins
                    All DS18B20 DQ pins ──── DQ
                    All DS18B20 GND    ──── GND
```

Use CAT5 cable (one pair per bus segment). Each sensor has a unique 64-bit ROM address — they self-identify in firmware without any manual configuration.

---

## 4. Software Architecture

### 4.1 Framework & Libraries

| Library | PlatformIO reference | Purpose |
|---|---|---|
| SensESP v3 | `SignalK/SensESP @ ^3.1.0` | Reactive sensor pipeline, WiFi config, OTA, Signal K |
| NMEA2000-library | `ttlappalainen/NMEA2000-library` | NMEA 2000 node on CAN bus |
| NMEA2000_esp32 | GitHub URL (not in registry) | ESP32 TWAI/CAN driver |
| OneWire | `paulstoffregen/OneWire @ ^2.3` | DS18B20 bus protocol |
| DallasTemperature | `milesburton/DallasTemperature @ ^3.9` | DS18B20 temperature reads |
| Adafruit ADS1X15 | `adafruit/Adafruit ADS1X15 @ ^2.5` | 16-bit ADC readings |

Build system: **PlatformIO** with the **pioarduino** platform fork (required for Arduino ESP32 Core 3.x / IDF 5.x, which SensESP v3 depends on). The official `espressif32` platform is frozen at Core 2.0.17 and is incompatible with SensESP v3.

### 4.2 NMEA 2000 PGN Strategy

All data that fits within standard NMEA 2000 PGNs is sent exclusively via NMEA 2000. WiFi/Signal K HTTP or WebSocket is used **only** for data that has no appropriate NMEA 2000 PGN.

| Data | NMEA 2000 PGN | Signal Path |
|---|---|---|
| Engine RPM | PGN 127488 (Engine Rapid Update) | N2K primary |
| Oil pressure warning | PGN 127489 field: Status1 bit "Low Oil Pressure" | N2K primary |
| Temperature warning | PGN 127489 field: Status1 bit "Over Temperature" | N2K primary |
| Coolant temperature | PGN 127489 field: Engine Temperature | N2K primary |
| Engine room temperatures (1-Wire) | PGN 130316 (Temperature Extended Range) | N2K primary |
| Tank level threshold (Gobius) | PGN 127505 (Fluid Level) | N2K primary |
| Bilge fan state | No standard N2K PGN → Signal K key `electrical.switches.bilgeFan.state` | WiFi / Signal K WS |
| Ignition key state (optional) | No standard PGN → Signal K key `electrical.switches.ignition.state` | WiFi / Signal K WS |

### 4.3 Engine Running Detection & Bilge Fan State Machine

```
IDLE  ──(engine starts, debounced)──▶  RUNNING  ──(engine stops, debounced)──▶  PURGE
  ▲                                                                                  │
  └──────────────────────────── (purge timer expires) ──────────────────────────────┘

Relay is OFF in IDLE and RUNNING.
Relay is ON in PURGE only.
If engine restarts during PURGE: → RUNNING immediately, relay OFF.
```

### 4.4 NMEA 2000 PGN Strategy

See §4.2 table above.

---

## 5. Complete I/O Summary Table

| # | Physical | GPIO | Signal | Type | Notes |
|---|---|---|---|---|---|
| 1 | D1 | 23 | Alternator W → RPM | Digital counter | Via diode clamp circuit |
| 2 | D2 | 25 | Oil pressure warning | Digital alarm | Active-low, NPN switch |
| 3 | D3 | 27 | Temperature warning | Digital alarm | Active-low, NPN switch |
| 4 | D4 | 26 | Ignition key sense | Digital input | Optional, +12V sense |
| 5 | A1 | ADS1115 ch0 | VP coolant temp sender | Analog passive | Parallel to gauge |
| 6 | A2 | ADS1115 ch1 | Gobius Pro tank 1 OUT | Analog w/ pull-up | Binary or resistive (Hub) |
| 7 | A3 | ADS1115 ch2 | Gobius Pro tank 2 OUT | Analog w/ pull-up | Binary or resistive (Hub) |
| 8 | A4 | ADS1115 ch3 | Battery voltage / spare | Analog passive | Optional |
| 9 | 1-Wire | GPIO 4 | DS18B20 chain | 1-Wire bus | Multiple sensors |
| 10 | GPIO 32 | GPIO header | Bilge fan relay | Digital output | Via relay module |
| 11 | N2K | CAN bus | All engine/tank data | NMEA 2000 | Primary data bus |
| 12 | WiFi | Integrated | Fan/key state, OTA, config | TCP/IP | Supplemental only |

---

## 6. Commissioning Checklist

1. **Wire W-terminal circuit** — verify sine-wave signal present at W terminal with oscilloscope or AC voltmeter before connecting to HALMET.
2. **Calibrate RPM** — start engine, compare HALMET RPM readout against a handheld optical tachometer. Adjust `pulses_per_revolution` until both agree. Typical starting value: 10–13.
3. **Test alarm inputs** — with engine off, short D2 to GND momentarily to verify oil pressure alarm registers on MFD.
4. **Calibrate temperature curve** — record voltage on A1 at known coolant temperatures (e.g. engine cold = ambient, engine warm = ~85°C per coolant gauge). Adjust CurveInterpolator points.
5. **Test Gobius sensors** — verify OUT1 transitions with the phone app showing level crossing the configured threshold.
6. **Test bilge fan logic** — start engine (fan should stay OFF), stop engine (fan should activate), wait `T_purge` (fan should stop). Verify fan never runs before engine starts.
7. **Verify NMEA 2000** — open MFD or Actisense Reader; confirm PGN 127488 and 127489 appearing with correct engine instance.
8. **Verify Signal K** — check `electrical.switches.bilgeFan.state` updating via the Signal K dashboard.

---

## 7. Key Configurable Parameters

| Parameter path | Default | Description |
|---|---|---|
| `/rpm/pulses_per_rev` | 10.0 | W-terminal pulses per engine crankshaft revolution (calibrate!) |
| `/rpm/running_threshold` | 200 RPM | RPM above which engine is considered running |
| `/bilge/purge_duration_s` | 600 s | How long to run bilge fan after engine stop |
| `/tank/tank1_capacity_l` | 100 L | Volume of tank 1 (for PGN 127505 scaling) |
| `/tank/tank2_capacity_l` | 100 L | Volume of tank 2 |

---

## 8. Enclosure & Installation Notes

- Use a **waterproof ABS box at least 120×80×55 mm**.
- Route the W-terminal cable with a 100 mA inline fuse as close to the alternator as practical.
- Keep 1-Wire cable runs under 20 m total; use a 4.7 kΩ pull-up near the HALMET.
- The Gobius sensors require their own 12 V supply (500 mA recommended per sensor). Do not power them from the HALMET GPIO pins.
- Label all wiring with heat-shrink ferrule markers before final assembly.
- The HALMET draws ~90 mA at 12 V with WiFi active — budget this from the NMEA 2000 backbone or a separate fused supply.

---

## 9. Known Benign Startup Errors

The following error messages appear on the serial console during boot. They are caused by startup race conditions inside SensESP and do not affect functionality.

### `Failed adding service http.tcp.` / `Failed adding service signalk-sensesp.tcp.`

SensESP registers its HTTP server and Signal K WebSocket client as mDNS services via `MDNS.addService()`. These calls are scheduled as deferred `event_loop()->onDelay(0, ...)` callbacks, as is the `MDNS.begin()` call that initialises the mDNS responder. The `addService` callbacks can fire before `MDNS.begin()` has run or before WiFi is connected, causing the registration to fail.

**Impact:** The web UI and WebSocket client work normally — these services are only used for mDNS browser discovery (e.g. Bonjour). The mDNS hostname (`halmet-engine.local`) still resolves correctly because `MDNS.begin()` eventually completes. This is a SensESP upstream issue, not a firmware bug.

### `deserializeJson error: EmptyInput`

The Signal K WebSocket client (`signalk_ws_client.cpp`) attempts to parse every incoming WebSocket frame as JSON. During connection establishment, the server may send empty frames (pings, keep-alives, or initial handshake artefacts). The `deserializeJson()` call returns `EmptyInput` for these, which is logged as an error but otherwise ignored — the client continues and processes subsequent valid messages normally.

**Impact:** None. The Signal K connection recovers and operates normally after the initial empty frame.

---

## 10. Roadmap

Prioritised improvements grouped into implementation sprints.

### Sprint 1 — Safety & Quick Wins (COMPLETE)

All items implemented and verified on hardware (commit `8534703`).

| # | Feature | Status |
|---|---------|--------|
| 1 | Alarm input debouncing (4-of-5 majority vote on D2/D3) | Done |
| 2 | Coolant sensor fault detection (out-of-range voltage → `N2kDoubleNA`) | Done |
| 3 | Stale data guard (>5 s without valid ADS read → `N2kDoubleNA`) | Done |
| 4 | I2C bus fault recovery (periodic `Wire.begin()` + `gAds.begin()` retry) | Done |
| 5 | Fix fluid type (`N2kft_Oil` → `N2kft_Fuel` in PGN 127505) | Done |

Hardware watchdog was originally Sprint 1 item 1 but deferred to Sprint 3 due to OTA bricking risk (watchdog firing mid-flash corrupts firmware).

### Sprint 2 — High-Value Features (COMPLETE)

All items implemented and verified on hardware (commit `0af9730`).

| # | Feature | Status |
|---|---------|--------|
| 6 | Fix cold-boot coolant sentinel (`gCoolantK` init to `N2kDoubleNA`, stale guard fires when no valid read ever) | Done |
| 7 | Temperature threshold alerting (configurable warn 95°C / alarm 105°C → Signal K notifications) | Done |
| 8 | Diagnostics heartbeat (uptime, firmware version, ADS fail count, reset reason → Signal K every 10 s) | Done |

### Sprint 3 — OTA & Robustness (NEXT)

| # | Feature | Description | Complexity |
|---|---------|-------------|------------|
| 9 | Safe relay state before OTA | Force bilge fan relay OFF when OTA begins. Without this, relay freezes in current state for 30–90 s during firmware write | Low |
| 10 | Firmware version in N2K product info | Pass `FW_VERSION_STR` (from Sprint 2 item 8) to `SetProductInformation()`. MFD shows installed version after OTA | Low |
| 11 | Engine hours counter | Persist accumulated runtime seconds to LittleFS in 1-minute increments. Send in PGN 127489 `EngineTotalHours` field. Low complexity, high long-term value for service tracking | Low |
| 12 | Hardware watchdog | Register ESP32 task watchdog with ~8 s timeout; reset in `loop()`. Now safe: OTA path is instrumented (item 9), recovery is verified | Low |

### Future — Architecture

Trigger this sprint when `main.cpp` crosses ~500 lines or adding a new sensor requires touching more than two files.

| # | Feature | Description | Complexity |
|---|---------|-------------|------------|
| 13 | Shared state struct | Replace scattered `static` globals with a single `EngineState` struct. Required before the module split so all modules can read/write shared data without cross-including each other | Low |
| 14 | Decompose monolithic setup() | Split `main.cpp` into focused modules (analog_inputs, digital_alarms, engine_state, n2k_publisher, diagnostics). Each module exposes an `init()` function that registers its own event-loop callbacks | Medium |

### Candidate Pool

Features evaluated but not prioritised for the current boat. May be revisited for other installations or if requirements change.

| Feature | Reason deferred |
|---------|----------------|
| Two-tank support (second PGN 127505 instance) | Single tank with two Gobius threshold sensors — no second tank to monitor |
| Battery voltage on A4 (PGN 127508) | Victron equipment already provides battery monitoring on the N2K bus |
| Configurable N2K engine instance | Single engine on the bus; no conflict risk with current installation |
| Runtime-configurable temp curve | High complexity, low value for single-boat install. Compile-time `TEMP_CURVE_POINTS` in `halmet_config.h` is easy to edit and reflash. Risk of malformed runtime config producing silently wrong temperatures |
