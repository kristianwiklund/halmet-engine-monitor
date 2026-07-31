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
| A4 | Spare | — | No longer used for coolant temperature. Coolant measurement moved to INA226 on I2C (see §3.2). |
| A2 | Resistive tank sender (10 mA CCS) | Active resistance | VDO 10–180 Ω default; Gobius 3-band mode via `-D TANK_SENSOR_GOBIUS` build flag. CCS jumper must be installed on A2 in default mode. See §3.3. |
| A3 | Gobius sensor B OUT1 (Gobius mode only) | Passive voltage w/ pull-up | Only used when `-D TANK_SENSOR_GOBIUS` is set. See §3.3. |
| A1 | Battery / supply voltage | Passive voltage | HALMET onboard 20 kΩ/2.2 kΩ voltage divider (10.09:1). Publishes PGN 127508 and SK `electrical.batteries.0.voltage`. Multiplier is runtime-configurable. See §3.4. |

### 2.3 1-Wire Bus (GPIO 4)

Used for DS18B20 temperature probes scattered across the engine room. Up to ~10 sensors on a single parasitic or powered bus. Six sensor slots are supported, each with a web-UI-configurable destination that controls which N2K temperature source and Signal K path the reading is sent to (see §4.5).

### 2.4 I²C Bus (GPIO 21/22)

Shared between the onboard ADS1115 ADC (address 0x4B) and the INA226 current sensor (address 0x40, default A0/A1 = GND). Both devices operate on the same 400 kHz bus. **Do not connect other I²C devices while NMEA 2000 isolation is required** — the I²C header shares ground with the MCU and would break galvanic isolation.

### 2.5 GPIO Header — Relay Output

One free GPIO (e.g. GPIO 32) is used to drive the bilge fan relay. Attach a small DIN-rail or PCB relay module rated for 12 V coil, 10 A/12 V contacts. Interpose a flyback diode if using a bare relay coil directly. Many pre-built relay modules (SRD-05VDC-SL-C style with an optocoupler) work directly at 3.3 V logic level, which makes them ideal here.

```
HALMET GPIO 32 ──→ Relay module IN
HALMET GND      ──→ Relay module GND
12 V supply     ──→ Relay module VCC
Relay COM       ──→ 12 V (fused, 5 A)
Relay NO        ──→ Bilge fan +12 V
```

GPIO 33 is used as a **warning lamp output**. It goes HIGH when either the oil pressure alarm (D2) or the coolant temperature alarm (D3) is active. Connect a small indicator lamp or LED (with suitable series resistor) between GPIO 33 and GND.

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

**Coolant temperature sender (INA226 on I2C):**

Coolant temperature is derived from the VDO NTC sender resistance, measured via an INA226 current sensor on the shared I2C bus. A 100 mΩ shunt resistor is placed in series with the sender. The INA226 measures both the bus voltage (across the sender) and the shunt current simultaneously. Firmware computes:

```
R_sender = V_bus / I_shunt
```

This avoids dependence on the gauge coil resistance (which would affect a parallel voltage measurement) and gives a direct resistance reading regardless of supply voltage variation.

A `CurveInterpolator` maps resistance to temperature using the VDO Type A (European) NTC curve as default. The table is pre-populated in firmware and editable via the web UI (`/coolant/resistance_curve`).

```
Ignition +12V ──── [Gauge coil] ──┬──── [100 mΩ shunt] ──── [Sender NTC] ──── GND (block)
                                  │           │                    │
                               (existing    INA226 shunt        INA226 bus
                                gauge)      current sense       voltage sense
                                            → I2C (GPIO 21/22) on HALMET
```

INA226 configuration: shunt = 100 mΩ, max current = 1 A, averaging = 64 samples (hardware filter). I2C address: 0x40 (A0=GND, A1=GND).

**Note:** A1 / ADS ch0 is no longer connected to the coolant sender.

**Oil pressure warning switch (to D2):** Normally-closed switch that opens when oil pressure drops below ~0.5 bar. Connect one side to D2, other side to GND (engine block). Active-low input. D2 is pulled up internally by HALMET.

**Temperature warning switch (to D3):** Same wiring as D2 — normally open, closes to GND on high temperature. Active-low.

### 3.3 Tank Sensor

#### Default mode — Resistive sender on A2 (10 mA constant-current source)

The default firmware reads a conventional resistive tank sender on A2 using the HALMET's built-in 10 mA constant-current source (CCS). Install the CCS jumper on the A2 screw terminal header. The firmware computes resistance as:

```
R = V_adc / I     (I = 0.010 A)
```

A CurveInterpolator maps resistance to level ratio (0.0–1.0). The default curve matches the **European VDO standard**: 10 Ω = empty, 180 Ω = full. The curve is runtime-configurable via the SensESP web UI — edit the calibration table to match any sender that uses a continuous resistive output.

No external components are required beyond the sender wire connected directly to A2.

#### Optional mode — Gobius Pro 3-band sensing (build flag)

To use Gobius Pro sensors instead, build with `-D TANK_SENSOR_GOBIUS` in `platformio.ini`. In this mode:

- **A2** reads the Gobius sensor A OUT1 ("below 3/4" threshold) in passive voltage mode with a 10 kΩ pull-up to +3.3 V. Remove the CCS jumper on A2.
- **A3** reads the Gobius sensor B OUT1 ("below 1/4" threshold) in passive voltage mode with a 10 kΩ pull-up to +3.3 V.

Each Gobius Pro sensor provides **two digital outputs** (OUT1 and OUT2) that switch between open-collector GND and floating (high-impedance), representing configurable level thresholds. Supply voltage for the sensors is 12–24 V (do not power from HALMET GPIO pins).

**Wiring (Gobius mode only):**

```
+3.3V (from HALMET GPIO header VCC) ──[10 kΩ]──┬── A2 (or A3)
                                                │
                                          Gobius OUT1
                                                │
                                               GND
```

When the Gobius output is floating (high): A2 reads ~3.3 V → "threshold NOT reached"
When the Gobius output sinks to GND: A2 reads 0 V → "threshold reached"

**Note on BLE:** Although the ESP32 has BLE, the Gobius Pro uses a proprietary BLE profile. The wired outputs are simpler and more reliable — use them.

### 3.4 Battery / Supply Voltage Sensing (A1)

A1 / ADS ch1 reads the supply voltage rail via the HALMET's onboard 20 kΩ/2.2 kΩ resistive divider, giving a scaling factor of (20 + 2.2) / 2.2 = **10.09:1**. This allows measuring up to ~33 V on a 12 V or 24 V boat.

Connect A1 to the N2K positive terminal. No external resistors are needed — the HALMET's internal divider handles voltage scaling.

The firmware multiplier (`/voltage/multiplier`, default 10.09) is runtime-configurable to compensate for resistor tolerance. The Signal K output path (`/voltage/sk_path`, default `electrical.batteries.0.voltage`) is also runtime-configurable via the web UI. Published as:
- PGN 127508 (Battery Status), battery instance 0
- SK path configurable via `/voltage/sk_path` (default `electrical.batteries.0.voltage`)

PGN 127508 is sent only when `adsOk` is true and `supplyVoltageV > 0.0`, to avoid transmitting zero/garbage readings during startup or I2C fault.

### 3.5 1-Wire Temperature Sensors (Engine Room)

DS18B20 waterproof probes wired in a bus topology from the 1-Wire header (GPIO 4). Recommended wiring for long cable runs (up to ~20 m total):

```
HALMET 1W header:  VDD ──── All DS18B20 VDD pins
                   DQ  ──── All DS18B20 DQ pins  (pull-up built into HALMET)
                   GND ──── All DS18B20 GND pins
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
| SensESP/OneWire | `SensESP/OneWire @ ^3.0.1` | DS18B20 bus protocol + SensESP integration (replaces raw OneWire + DallasTemperature) |
| Adafruit ADS1X15 | `adafruit/Adafruit ADS1X15 @ ^2.5` | 16-bit ADC readings (ADS1115 on I2C) |
| INA226_WE | `wollewald/INA226_WE` | INA226 current/voltage sensor for coolant sender resistance measurement |

Build system: **PlatformIO** with the **pioarduino** platform fork (required for Arduino ESP32 Core 3.x / IDF 5.x, which SensESP v3 depends on). The official `espressif32` platform is frozen at Core 2.0.17 and is incompatible with SensESP v3.

### 4.2 NMEA 2000 PGN Strategy

All data is sent via both NMEA 2000 and Signal K where applicable. Data that fits within standard NMEA 2000 PGNs is sent on the N2K bus; the same data is also available via the Signal K WebSocket. Data with no appropriate NMEA 2000 PGN (bilge fan state, ignition key) is sent via Signal K only.

| Data | NMEA 2000 PGN | Signal Path |
|---|---|---|
| Engine RPM | PGN 127488 (Engine Rapid Update) | N2K primary |
| Oil pressure warning | PGN 127489 field: Status1 bit "Low Oil Pressure" | N2K primary |
| Temperature warning | PGN 127489 field: Status1 bit "Over Temperature" | N2K primary |
| Coolant temperature | PGN 127489 field: Engine Temperature | N2K primary |
| Battery / supply voltage | PGN 127508 (Battery Status), instance 0 | N2K primary + SK `electrical.batteries.0.voltage` |
| 1-Wire temperatures (configurable) | PGN 130316 (Temperature Extended Range) | N2K + SK (destination-dependent, see §4.5) |
| Tank level (resistive sender, default) | PGN 127505 (Fluid Level) | N2K primary |
| Tank level (Gobius 3-band mode) | PGN 127505 (Fluid Level) — synthesised from threshold crossings | N2K primary (build flag `-D TANK_SENSOR_GOBIUS`) |
| Bilge fan manual control | PGN 127502 (Switch Bank Control) receive | N2K receive |
| Bilge fan status | PGN 127501 (Binary Switch Bank Status) at 1 Hz | N2K primary |
| Bilge fan state | No standard N2K PGN → Signal K key `electrical.switches.bilgeFan.state` | WiFi / Signal K WS |
| Ignition key state (optional) | No standard PGN → Signal K key `electrical.switches.ignition.state` | WiFi / Signal K WS |
| Coolant temp notifications | No N2K PGN → Signal K `notifications.propulsion.0.coolantTemperature` | WiFi / Signal K WS |

### 4.3 Engine Running Detection & Bilge Fan State Machine

```
IDLE  ──(engine starts, debounced)──▶  RUNNING  ──(engine stops, debounced)──▶  PURGE
  ▲                                                                                  │
  └──────────────────────────── (purge timer expires) ──────────────────────────────┘

Relay is OFF in IDLE and RUNNING.
Relay is ON in PURGE only.
If engine restarts during PURGE: → RUNNING immediately, relay OFF.
```

**Manual override via NMEA 2000:** The bilge fan can also be toggled on or off from an MFD by sending PGN 127502 (Switch Bank Control). The `BilgeFan` class exposes a `manualOn()` latch (`_manualOverride` flag) that activates the relay independently of the automatic purge state. The fan status is broadcast back on PGN 127501 (Binary Switch Bank Status) at 1 Hz.

### 4.4 NMEA 2000 PGN Strategy

See §4.2 table above.

### 4.5 1-Wire Temperature Destination Assignment

Each of the 6 DS18B20 sensor slots has a web-UI-configurable destination that determines both the N2K temperature source type (PGN 130316) and the Signal K path. The destination is resolved at boot from persisted config; changing it requires a reboot.

| Index | Label | N2K `tN2kTempSource` | Signal K path |
|---|---|---|---|
| 0 | Disabled (raw SK) | — (no N2K) | `environment.inside.temperature.{i}` |
| 1 | Engine room | `N2kts_EngineRoomTemperature` (3) | `environment.inside.engineRoom.temperature.{i}` |
| 2 | Exhaust gas | `N2kts_ExhaustGasTemperature` (14) | `propulsion.0.exhaustTemperature.{i}` |
| 3 | Sea water | `N2kts_SeaTemperature` (0) | `environment.water.temperature.{i}` |
| 4 | Outside air | `N2kts_OutsideTemperature` (1) | `environment.outside.temperature.{i}` |
| 5 | Inside / cabin | `N2kts_InsideTemperature` (2) | `environment.inside.temperature.{i}` |
| 6 | Refrigeration | `N2kts_RefrigerationTemperature` (7) | `environment.inside.refrigerator.temperature.{i}` |
| 7 | Freezer | `N2kts_FreezerTemperature` (13) | `environment.inside.freezer.temperature.{i}` |
| 8 | Alternator (SK only) | — (no N2K) | `electrical.alternators.0.temperature.{i}` |
| 9 | Oil sump (SK only) | — (no N2K) | `propulsion.0.oilTemperature.{i}` |
| 10 | Intake manifold (SK only) | — (no N2K) | `propulsion.0.intakeManifoldTemperature.{i}` |
| 11 | Engine block (SK only) | — (no N2K) | `propulsion.0.engineBlockTemperature.{i}` |

Destinations with `n2kSource = -1` (indices 0, 8, 9) emit to Signal K only. All other destinations send on both N2K (PGN 130316) and Signal K. The `{i}` suffix is the sensor slot index (0–5).

Coolant temperature is **not** part of this system — it is derived via the INA226 current sensor and sent in PGN 127489.

---

## 5. Complete I/O Summary Table

| # | Physical | GPIO | Signal | Type | Notes |
|---|---|---|---|---|---|
| 1 | D1 | 23 | Alternator W → RPM | Digital counter | Via diode clamp circuit |
| 2 | D2 | 25 | Oil pressure warning | Digital alarm | Active-low, NPN switch |
| 3 | D3 | 27 | Temperature warning | Digital alarm | Active-low, NPN switch |
| 4 | D4 | 26 | Ignition key sense | Digital input | Optional, +12 V sense |
| 5 | A4 | ADS1115 ch1 | Spare | — | No longer used. Coolant temp now via INA226 on I2C. |
| 6 | A2 | ADS1115 ch1 | Resistive tank sender (CCS) | Active resistance | Default mode; CCS jumper installed. VDO 10–180 Ω. Gobius OUT1 in Gobius mode (build flag). |
| 7 | A3 | ADS1115 ch2 | Gobius sensor B OUT1 | Analog w/ pull-up | Gobius mode only (`-D TANK_SENSOR_GOBIUS`). Not used in default mode. |
| 8 | A1 | ADS1115 ch0 | Battery / supply voltage | Analog passive | HALMET 20 kΩ/2.2 kΩ divider (10.09:1). 0–32 V range. PGN 127508 + SK. |
| 9 | I2C | GPIO 21 (SDA) / 22 (SCL) | INA226 (coolant sender) + ADS1115 (ADC) | I2C bus | INA226 at 0x40, ADS1115 at 0x4B. 400 kHz. |
| 10 | 1-Wire | GPIO 4 | DS18B20 chain | 1-Wire bus | Multiple sensors; pull-up built into HALMET |
| 11 | GPIO 32 | GPIO header | Bilge fan relay | Digital output | Via relay module |
| 12 | GPIO 33 | GPIO header | Warning lamp | Digital output | HIGH when oil or coolant alarm active |
| 13 | N2K | CAN bus | All engine/tank data | NMEA 2000 | Primary data bus |
| 14 | WiFi | Integrated | Fan/key state, battery V, OTA, config | TCP/IP | Supplemental and config only |

---

## 6. Commissioning Checklist

1. **Wire W-terminal circuit** — verify sine-wave signal present at W terminal with oscilloscope or AC voltmeter before connecting to HALMET.
2. **Wire INA226** — install 100 mΩ shunt in series with the VDO coolant sender. Connect INA226 V+ and V− across the shunt; connect INA226 SDA/SCL to HALMET I2C header (GPIO 21/22). Address pins A0/A1 to GND (default 0x40).
3. **Calibrate RPM** — start engine, compare HALMET RPM readout against a handheld optical tachometer. Adjust `pulses_per_rev` until both agree. Typical starting value: 10–13.
4. **Test alarm inputs** — with engine off, short D2 to GND momentarily to verify oil pressure alarm registers on MFD.
5. **Calibrate coolant temperature curve** — with the engine cold, open the web UI and note the resistance reading at `/coolant/resistance_curve`. At operating temperature, compare HALMET's reported coolant value against the original gauge. Adjust the CurveInterpolator table at `/coolant/resistance_curve` if needed. The pre-loaded VDO Type A curve is a good starting point for European VDO senders.
6. **Verify battery voltage** — check `electrical.batteries.0.voltage` in Signal K against a known-good voltmeter. Adjust `/voltage/multiplier` if the readings differ (default 10.09 assumes exact resistor values).
7. **Test tank sensor** — in default (resistive) mode: fill tank to known level, verify PGN 127505 level reading against the expected value for the measured sender resistance. Adjust the CurveInterpolator calibration table at `/tank/resistance_curve` in the web UI if needed. If using Gobius mode (`-D TANK_SENSOR_GOBIUS`): verify OUT1 transitions with the phone app showing level crossing the configured threshold.
8. **Test bilge fan logic** — start engine (fan should stay OFF), stop engine (fan should activate), wait `T_purge` (fan should stop). Verify fan never runs before engine starts.
9. **Verify NMEA 2000** — open MFD or Actisense Reader; confirm PGN 127488, 127489, and 127508 appearing with correct engine instance.
10. **Verify Signal K** — check `electrical.switches.bilgeFan.state` and `electrical.batteries.0.voltage` updating via the Signal K dashboard.

---

## 7. Key Configurable Parameters

| Parameter path | Default | Description |
|---|---|---|
| `/rpm/pulses_per_rev` | 10.0 | W-terminal pulses per engine crankshaft revolution (calibrate!) |
| `/rpm/running_threshold` | 200 RPM | RPM above which engine is considered running |
| `/bilge/purge_duration_s` | 600 s | How long to run bilge fan after engine stop |
| `/tank/capacity_l` | 100 L | Volume of tank (for PGN 127505 scaling) |
| `/tank/resistance_curve` | VDO 10–180 Ω | Runtime-editable CurveInterpolator: sender resistance (Ω) → level ratio (0.0–1.0). Default: 10 Ω = 0.0 (empty), 180 Ω = 1.0 (full). Active in default resistive sender mode only. |
| `/coolant/resistance_curve` | VDO Type A NTC | Runtime-editable CurveInterpolator: sender resistance (Ω) → temperature (°C). Pre-populated with European VDO NTC values. User-editable for non-VDO senders. |
| `/coolant/warn_threshold_c` | 95 °C | Coolant temperature Signal K "warn" notification threshold |
| `/coolant/alarm_threshold_c` | 105 °C | Coolant temperature Signal K "alarm" notification threshold |
| `/voltage/multiplier` | 10.09 | A4 voltage divider multiplier. HALMET onboard divider = (20 kΩ + 2.2 kΩ) / 2.2 kΩ = 10.09. Adjust if resistor values differ. |
| `/voltage/sk_path` | `electrical.batteries.0.voltage` | Signal K path for battery voltage output. Runtime-configurable — no restart required. |
| `/n2k/engine_instance` | 0 | NMEA 2000 engine instance number (0–252) |
| `/n2k/product_code` | 100 | N2K product code. Requires restart. |
| `/n2k/device_function` | 160 | N2K device function (160 = Engine Gateway). Requires restart. |
| `/n2k/device_class` | 25 | N2K device class (25 = Propulsion). Requires restart. |
| `/n2k/manufacturer_code` | 999 | N2K manufacturer code (999 = uncertified placeholder). Requires restart. |
| `/intervals/rpm_n2k_ms` | 250 ms | PGN 127488 (RPM) send interval. Requires restart. |
| `/intervals/n2k_slow_ms` | 1000 ms | PGN 127489/127505/127501/127508 send interval. Requires restart. |
| `/intervals/sk_supplemental_ms` | 5000 ms | Signal K supplemental data (fan state, ignition) interval. Requires restart. |
| `/onewire/<rom_hex>/dest` | 1 (Engine room) | 1-Wire sensor destination index per ROM address (see §4.5). Requires reboot after change. |

---

## 8. Enclosure & Installation Notes

- Use a **waterproof ABS box at least 120×80×55 mm**.
- Route the W-terminal cable with a 100 mA inline fuse as close to the alternator as practical.
- Keep 1-Wire cable runs under 20 m total; the HALMET board has a built-in pull-up on the 1-Wire header.
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

### Sprint 2.5 — 1-Wire Temperature Source Assignment (COMPLETE)

| # | Feature | Status |
|---|---------|--------|
| 8b | Configurable 1-Wire → N2K/SK destination per sensor slot (6 slots, 10 destinations, web UI config, PGN 130316 send) | Done |

### Sprint 3 — 1-Wire Completeness & Relay Safety (COMPLETE)

| # | Feature | Status |
|---|---------|--------|
| 9 | Safe relay state before OTA (ArduinoOTA.onStart → forceOff relay) | Done |
| 10 | Firmware version in N2K product info (`FW_VERSION_STR` → `SetProductInformation()`) | Done |
| 15 | Add `propulsion.0.intakeManifoldTemperature` to 1-Wire destination list (index 10) | Done |
| 16 | Add `propulsion.0.engineBlockTemperature` to 1-Wire destination list (index 11) | Done |

### Sprint 4 — Architecture Refactor (COMPLETE)

| # | Feature | Status |
|---|---------|--------|
| 13 | Shared state struct (`EngineState`) | Done |
| 14 | Decompose monolithic `setup()` into modules (`analog_inputs`, `digital_alarms`, `engine_state_machine`, `n2k_publisher`, `diagnostics`) | Done |

### Sprint 5 — OTA Robustness & Watchdog (COMPLETE)

| # | Feature | Status |
|---|---------|--------|
| 12 | Hardware watchdog: ESP32 task watchdog (8 s), deregistered before OTA begins | Done |

### Sprint 6 — ROM-Based 1-Wire Sensor Selection (COMPLETE)

| # | Feature | Status |
|---|---------|--------|
| 17 | List detected 1-Wire sensors by ROM address; dropdown destination picker per ROM in web UI | Done |

### Sprint 8 — N2K Bilge Fan Switch & Warning Lamp (COMPLETE)

| # | Feature | Status |
|---|---------|--------|
| 18 | N2K bilge fan switch: receive PGN 127502 (Switch Bank Control) from MFD | Done |
| 19 | PGN 127501 (Binary Switch Bank Status) transmit at 1 Hz | Done |
| 20 | Warning lamp on GPIO 33: HIGH when any alarm active | Done |
| 21 | PGN 127489 cleanup: removed unmeasurable fields; CheckEngine status bit set on alarm | Done |

### Sprint 9 — Resistive Tank Sender (COMPLETE)

| # | Feature | Status |
|---|---------|--------|
| 22 | Default tank sensor: continuous resistive sender on A2 (10 mA CCS); R = V_adc / I | Done |
| 23 | Runtime-configurable CurveInterpolator (resistance Ω → level ratio); default: VDO 10 Ω (empty) / 180 Ω (full) | Done |
| 24 | Gobius 3-band mode retained as optional via `-D TANK_SENSOR_GOBIUS` build flag | Done |

### Sprint 10 — Code Review Quick Wins (COMPLETE)

| # | Feature | Status |
|---|---------|--------|
| 22 | RPM volatile read TOCTOU race — consolidated into single critical section | Done |
| 23 | Stale I/O map comment in main.cpp | Done |
| 24 | Dead OneWireSensors files cleaned up | Done |
| 25 | Explicit flash size in platformio.ini (16 MB, corrected from 8 MB comment) | Done |
| 26 | Diagnostics uptime: switched to `SKOutputInt` (integer seconds) to avoid float truncation | Done |

### Sprint 11 — Quick Wins (COMPLETE)

| # | Feature | Status |
|---|---------|--------|
| 27 | Hardware watchdog consolidated from Sprint 5 | Done |
| 28 | Bilge fan manual override not cleared on engine restart — fixed | Done |
| 29 | Decouple RPM N2K send rate from measurement rate | Done |
| 30 | RPM single-instance guard | Done |
| 31 | N2K address persistence — stop polling after first save | Done |
| 32 | N2K device constants moved to named macros in `halmet_config.h` | Done |
| 33 | OTA password from `secrets.h` | Done |
| 34 | `SwitchMetadata` extracted to separate header | Done |
| 35 | `FW_VERSION_STR` derived from git tag via `get_version.py` pre-build script | Done |
| 36 | Gobius mode Signal K output added | Done |
| 37 | `EngineState` single-task invariant documented | Done |

### Sprint 12 — Web UI Configuration (COMPLETE)

| # | Feature | Status |
|---|---------|--------|
| 38 | N2K engine instance configurable via web UI (`/n2k/engine_instance`) | Done |
| 39 | Send intervals configurable via web UI (`/intervals/rpm_n2k_ms`, `/intervals/n2k_slow_ms`, `/intervals/sk_supplemental_ms`). Requires restart. | Done |
| 40 | N2K device constants configurable via web UI (`/n2k/product_code`, etc.). `setupNmea2000()` moved after SensESP app builder init. Requires restart. | Done |

### Sprint 13 — Battery Voltage & Coolant Temp Accuracy (COMPLETE)

| # | Feature | Status |
|---|---------|--------|
| 41 | Battery voltage on A4 (ADS ch3, 20 kΩ/2.2 kΩ divider). PGN 127508 + SK `electrical.batteries.0.voltage`. | Done |
| 42 | Coolant temp via INA226 on I2C. Derives sender resistance from `V_bus / I_shunt`. Replaces A1 voltage-based measurement. | Done |
| 43 | `CurveInterpolator` for resistance → °C mapping. Pre-populated with VDO Type A (European) NTC curve. User-editable in web UI. | Done |

### Sprint 14 — Post-Sprint-13 Code Review (COMPLETE)

| # | Issue | Status |
|---|-------|--------|
| 44 | ~~Wrong NMEA2000 function name in `sendBatteryStatus`~~ | False positive — closed. `SetN2kDCBatStatus` is a valid alias. |
| 45 | INA226 health flag (`ina226Ok`) missing from `EngineState` | Done |
| 47 | Dead `TEMP_CURVE_POINTS` macro removed from `halmet_config.h` | Done |
| 48 | Dead `COOLANT_VOLT_MIN_V` / `COOLANT_VOLT_MAX_V` constants removed | Done |
| 49 | Battery N2K send guard tightened: `if (st->adsOk && st->supplyVoltageV > 0.0f)` | Done |
| 50 | Local variable `gVoltageMultiplier` renamed to `voltageMultiplier` (dropped incorrect `g` prefix) | Done |

### Candidate Pool — FROZEN (do not pick up unless explicitly ordered)

Features evaluated and deliberately deferred. Do **not** schedule, implement, or re-evaluate these without a direct instruction from the project owner.

| Feature | Reason deferred |
|---------|----------------|
| Two-tank support (second PGN 127505 instance) | Single tank — no second tank to monitor |
| Engine hours counter | Persist accumulated runtime seconds to LittleFS in 1-minute increments; send in PGN 127489 `EngineTotalHours`. Low priority for current usage pattern. |
| I2C LCD display (2×16 ASCII) | Requires I2C display driver, N2K bus listener, web UI config for display layout |
| Live temperature in 1-Wire config card description | Low-priority UX polish; sensors can be identified by warming/cooling and checking SK diagnostics |
| Hot-reload 1-Wire sensor assignments without restart | Medium complexity; SensESP restart button serves as workaround |
