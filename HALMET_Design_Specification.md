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

### Sprint 1 — Safety & Quick Wins

| # | Feature | Description | Complexity |
|---|---------|-------------|------------|
| 1 | Fix fluid type | Change `N2kft_Oil` → `N2kft_Diesel` in PGN 127505 so MFDs show the correct fuel gauge icon | Trivial |
| 2 | Hardware watchdog | Register ESP32 task watchdog with ~8 s timeout; reset in `loop()`. Prevents silent hangs from I2C deadlock or WiFi stack stalls that would freeze the relay in its current state | Low |
| 3 | Alarm input debouncing | Add majority-vote filter (4-of-5 samples) on D2/D3 oil and temperature alarm switches. Mechanical contacts on a vibrating diesel generate false alarms without debouncing | Low |
| 4 | Coolant sensor fault detection | Detect open-circuit / short-circuit on A1 (voltage outside interpolation range) and send `N2kDoubleNA` instead of a misleading −1°C value | Low |
| 5 | Stale data guard | Track age of last successful ADS1115 read; send `N2kDoubleNA` for coolant temperature in PGN 127489 if data is older than 5 s | Low |
| 6 | Two-tank support | Send a second PGN 127505 (instance 1) for tank 2. The Gobius sensors and wiring already exist but only one tank is transmitted. Add `/tank/tank2_capacity_l` ConfigItem | Low |
| 7 | I2C bus fault recovery | Periodically retry `Wire.begin()` + `gAds.begin()` if the ADS1115 init failed or stops responding. Engine vibration and EMI can glitch the I2C bus | Low |

### Sprint 2 — High-Value Features

| # | Feature | Description | Complexity |
|---|---------|-------------|------------|
| 8 | Engine hours counter | Accumulate run-time while `gEngineRunning` is true, persist to LittleFS on engine stop, send in PGN 127489 `EngineTotalHours` field. Add a ConfigItem for the initial offset so the skipper can match the existing mechanical hour meter. This is the most visible gap vs. competitors (Yacht Devices, Maretron) | Medium |
| 9 | Battery voltage (A4) | Read house bank voltage on ADS1115 channel 3 via a resistive divider (47 kΩ / 10 kΩ), send PGN 127508 (Battery Status). Add a ConfigItem for the divider calibration factor. Requires a two-resistor voltage divider on A4 — the only item needing a wiring change | Low |
| 10 | Configurable N2K engine instance | Replace hardcoded `N2K_ENGINE_INSTANCE 0` with a `PersistingObservableValue` + ConfigItem. Any second N2K engine gateway on the bus using instance 0 will cause PGN conflicts on every chartplotter | Low |
| 11 | Temperature threshold alerting | Use the precise analog coolant temperature from A1 to trigger a Signal K notification *before* the binary alarm switch on D3 trips. Configurable warning threshold (e.g. 95°C) and alarm threshold (e.g. 105°C). The analog reading gives early warning the binary switch cannot | Medium |
| 12 | Diagnostics heartbeat | Publish uptime, firmware version, ADS fail count, and `esp_reset_reason()` to Signal K every 10 s. Provides remote health visibility without physical access to the boat | Low |

### Sprint 3 — OTA & Robustness

| # | Feature | Description | Complexity |
|---|---------|-------------|------------|
| 13 | Safe relay state before OTA | Force the bilge fan relay OFF when an OTA update begins. Without this, the relay is frozen in its current state for 30–90 s during the firmware write | Low |
| 14 | Firmware version in N2K product info | Derive version string from git tag at build time (`-D FW_VERSION_STR`), pass to `SetProductInformation()`. After OTA the MFD can show the installed version | Low |
| 15 | Runtime-configurable temp curve | Replace the compile-time `TEMP_CURVE_POINTS` macro with a `PersistingObservableValue<String>` parsed at runtime. Eliminates the need to recompile for different engines or sender variants | High |

### Future — Architecture

| # | Feature | Description | Complexity |
|---|---------|-------------|------------|
| 16 | Decompose monolithic setup() | Split `main.cpp` into focused modules (analog_inputs, digital_alarms, engine_state, n2k_publisher, diagnostics). Each module exposes an `init()` function that registers its own event-loop callbacks | Medium |
| 17 | Shared state struct | Replace scattered `static` globals with a single `EngineState` struct. Required before the module split so all modules can read/write shared data without cross-including each other | Low |
