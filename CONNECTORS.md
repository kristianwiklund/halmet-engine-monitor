# Physical Connector Allocation

Colors from convenience / existing dupont cables. Roll your own.

┌────────────────────────────────┬──────────┬──────────────────────────────────────────────┐
│ Connector                      │ Pin      │ Signal                                       │
├────────────────────────────────┼──────────┼──────────────────────────────────────────────┤
│ NMEA 2000 (dedicated)          │ —        │ CAN bus + power (5–32 V)                     │
├────────────────────────────────┼──────────┼──────────────────────────────────────────────┤
│ 4-pin                          │ 1 orange │ 3.3 V                                        │
│                                │ 2 blue   │ GND                                          │
│                                │ 3 yellow │ SDA (GPIO 21)                                │
│                                │ 4 green  │ SCL (GPIO 22)                                │
│                                │          │ (I²C to INA226 coolant sender, engine block) │
├────────────────────────────────┼──────────┼──────────────────────────────────────────────┤
│ Triple-pin                     │ 1 orange │ VDD                                          │
│                                │ 2 yellow │ DQ (GPIO 4)                                  │
│                                │ 3 green  │ GND                                          │
│                                │          │ (1-Wire DS18B20 probes, engine room)         │
├────────────────────────────────┼──────────┼──────────────────────────────────────────────┤
│ 4-pin #2                       │ 1 purple │ D1 — Alternator W (RPM)                      │
│                                │ 2 green  │ D4 — Ignition key sense (+12 V)              │
|                                | 3 blue   | D2 — Oil pressure switch                     │
│                                │ 4 yellow │ D3 — Temp warning switch                     │
│                                │          │ (GND-referenced to engine block)             │
├────────────────────────────────┼──────────┼──────────────────────────────────────────────┤
│ 2-pin                          │ 1 purple │ A2 signal (tank sender)                      │
│                                │ 2        │ GPIO 33 (bilge fan relay)*                   │
│                                │          │ GND (engine block)                           │
└────────────────────────────────┴──────────┴──────────────────────────────────────────────┘
* possibly via a "servo controlled" power switch instead.

```

## Internal / Onboard (no external wiring)

```
┌─────────────────────────────────┬──────────┬──────────────────────────────────────────────┐
│ Resource                        │ Bus      │ Notes                                        │
├─────────────────────────────────┼──────────┼──────────────────────────────────────────────┤
│ A1 — Battery voltage            │ internal │ Derived from N2K power supply rail onboard   │
│                                 │          │ (20 kΩ/2.2 kΩ divider, 10.09:1)              │
├─────────────────────────────────┼──────────┼──────────────────────────────────────────────┤
│ ADS1115 (addr 0x4B)             │ I²C      │ Onboard ADC; serves A1/A2/A3/A4              │
│                                 │          │ Already on same I²C bus as INA226            │
├─────────────────────────────────┼──────────┼──────────────────────────────────────────────┤
│ GPIO 33 — Warning lamp          │ internal │ Not wired per decision                       │
└─────────────────────────────────┴──────────┴──────────────────────────────────────────────┘
```

## Not Used (no connection)

```
┌─────────────────────────────────┬──────────┬──────────────────────────────────────────────┐
│ Resource                        │ Reason                                                  │
├─────────────────────────────────┼─────────────────────────────────────────────────────────┤
│ A1 (ADS1115 ch0)                │ Formerly coolant temp; replaced by INA226 on I²C        │
├─────────────────────────────────┼─────────────────────────────────────────────────────────┤
│ A3 (ADS1115 ch2)                │ Only needed in Gobius Pro mode (build flag) — not used  │
└─────────────────────────────────┴─────────────────────────────────────────────────────────┘
```
