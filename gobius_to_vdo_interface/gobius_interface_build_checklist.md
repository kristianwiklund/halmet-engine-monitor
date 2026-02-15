# Build & Commissioning Checklist: Tank Level Interface

## Phase 1: Preparation & Materials
- [ ] **Resistors Acquired:** 10 Ω, 56 Ω, 120 Ω (Metal Film, 0.5W).
- [ ] **PC817 Board:** 4-channel version with screw terminals.
- [ ] **Enclosure:** IP67 ABS Junction box + 4x PG7 Cable Glands.
- [ ] **Dashboard LED:** 12V Chrome Bezel LED (Red).
- [ ] **Tools:** Soldering iron, wire strippers, multimeter, heat shrink tubing.

## Phase 2: Internal Assembly (Bench Work)
- [ ] **Mount Board:** Secure PC817 board inside the enclosure.
- [ ] **Ladder Solder:** Solder $R_2$ across Output Pins C2-E2.
- [ ] **Ladder Solder:** Solder $R_1$ across Output Pins C1-E1.
- [ ] **Series Link:** Jumper Pin E2 to Pin C1.
- [ ] **Base Link:** Connect $R_{base}$ (10 Ω) between Pin E1 and System Ground.
- [ ] **Alarm Jumper:** Jumper Input "IN 1" to "IN 3" on the PC817 board.

## Phase 3: Field Wiring
- [ ] **Gobius Power:** Connect Red (+) and Black (-) to DC power bus.
- [ ] **Signal Inputs:** - 1/4 Sensor Green wire -> PC817 IN 1.
    - 3/4 Sensor Green wire -> PC817 IN 2.
- [ ] **Gauge Connection:** VDO Signal wire -> PC817 Pin C2.
- [ ] **LED Wiring:** - +12V -> PC817 Pin C3.
    - PC817 Pin E3 -> LED Red wire.
    - LED Black wire -> Ground.

## Phase 4: Configuration & Testing
- [ ] **App Setup:** Open Gobius App. Set both sensors to "ON when level is BELOW sensor."
- [ ] **Dry Test (Empty):** Verify gauge reads 'E' and LED is **ON**.
- [ ] **Simulation (Full):** Use the App 'Test' function to turn sensors OFF. Verify gauge rises to 'F' and LED turns **OFF**.
- [ ] **Final Seal:** Tighten all cable glands and screw the enclosure lid on with the gasket in place.
