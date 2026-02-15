# Design Specification: Gobius Pro to VDO Resistive Interface

## 1. Project Overview
This project defines an electrical interface to convert digital "Open Collector" signals from two Gobius Pro ultrasonic sensors into a stepped resistive signal compatible with standard European VDO tank gauges (10–180 Ω).

## 2. Background Research
### 2.1 Gobius Pro Electrical Interface
* **Output Type:** Open Collector (NPN). When active, the sensor pulls the output wire to ground.
* **Logic:** Configurable via Bluetooth. For this design, "Normally Closed" logic is used (Output is ON when liquid is BELOW the sensor).
* **Current Limit:** ~200mA per output.

### 2.2 VDO Gauge Requirements (European Standard)
* **Empty State:** 10 Ω (Low resistance = Low needle position).
* **Full State:** 180 Ω (High resistance = High needle position).
* **Signal Type:** The gauge sends a small sense current through the sender to ground; the voltage drop determines the needle position.

## 3. System Architecture
The design utilizes a **PC817 4-Channel Optocoupler** board to isolate the sensor power circuit from the gauge measurement circuit.

### 3.1 Resistance Ladder Logic
A series resistor ladder is used. Optocouplers act as bypass switches across specific resistors.
* **Base Resistor ($R_{base}$):** 10 Ω (Constant "Floor" value).
* **Step 1 ($R_1$):** 56 Ω (Activated when level rises above 1/4).
* **Step 2 ($R_2$):** 113-120 Ω (Activated when level rises above 3/4).

### 3.2 Logic Truth Table
| Tank Level | Sensor 1 (1/4) | Sensor 2 (3/4) | Opto 1 | Opto 2 | Total Resistance | Gauge Reading |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| < 1/4 | ON | ON | Closed | Closed | 10 Ω | Empty |
| > 1/4 | OFF | ON | Open | Closed | 66 Ω | ~1/3 |
| > 3/4 | OFF | OFF | Open | Open | 186 Ω | Full |

## 4. Hardware Components
* **Sensors:** 2x Gobius Pro.
* **Isolation:** PC817 4-Channel Breakout Module.
* **Resistors:** 1% Metal Film, 0.5W (10 Ω, 56 Ω, 120 Ω).
* **Alarm:** 12V LED with integrated resistor (Red).