# Troubleshooting Guide: Gobius Pro to VDO Interface

This guide covers common issues encountered during the installation and commissioning of the ultrasonic-to-resistive level interface. Problems generally fall into three categories: **Logic**, **Resistance**, or **Power**.

---

## 1. The "Logic Flip" (Gauge works backwards)
* **Symptom:** Gauge reads "Full" when the tank is empty and "Empty" when the tank is full.
* **Cause:** The Gobius Pro sensor output logic is inverted in the app.
* **Fix:** Open the Gobius App for each sensor. Navigate to the **Digital Output** settings. If it is set to "Normally Open," switch it to **"Normally Closed"** (or "Active when BELOW sensor").

## 2. The "Dead Needle" (No movement)
* **Symptom:** Gauge stays at "Empty" (or below) regardless of tank level.
* **Cause A:** The VDO signal wire is shorted to ground before it reaches the resistors.
* **Cause B:** The PC817 board is not receiving 12V power at the `VCC` pin, preventing the optical switches from closing.
* **Diagnosis:** 1. Use a multimeter to check for 12V between `VCC` and `GND` on the PC817 board.
    2. Disconnect the gauge signal wire from the interface board; the needle should jump to "Full" (infinite resistance). If it doesn't move, the issue is in the gauge or the vessel's wiring.

## 3. "Inaccurate Readings" (Needle misses the marks)
* **Symptom:** "Full" only reaches 3/4, or "Empty" sits slightly above the 'E' line.
* **Cause:** Optocouplers have a small internal resistance when "ON" (Saturation Voltage), adding ~1.2V to 2V drop or equivalent resistance.
* **Fix:** * **If Empty is too high:** Replace the $10\ \Omega$ base resistor with an $8.2\ \Omega$ or $5.6\ \Omega$ resistor to compensate for the optocoupler's internal overhead.
    * **If Full is too low:** Increase $R_2$ from $120\ \Omega$ to $130\ \Omega$.



## 4. "The Jittery Needle"
* **Symptom:** The needle bounces or vibrates, especially when the engine or pumps are running.
* **Cause:** Electrical noise on the 12V line or a loose common ground.
* **Fix:** 1. Ensure all Black wires (GND) are tied to a single "Star Ground" point on the DC bus.
    2. Check that the PG7 cable glands are tight; moisture or salt-air ingress inside the box can cause "leakage" between terminals.

## 5. "LED is Dim or Always ON"
* **Symptom:** The low-level LED flickers or stays on regardless of the liquid level.
* **Cause A:** Shared ground loop between the LED and the Gobius sensor.
* **Cause B:** The input trigger jumper on the PC817 breakout board is set to High-Level.
* **Fix:** Ensure the PC817 board jumpers are set to **"L" (Low-Level Trigger)**. This ensures the optocoupler activates when the Gobius pulls the input to Ground.

---

## Quick Resistance Reference Table
*Disconnect the gauge signal wire from the interface board and measure resistance between the **Signal Output Terminal** and **System Ground**.*

| Expected Tank State | Target Resistance | Multimeter Reading | Pass/Fail |
| :--- | :--- | :--- | :--- |
| **Empty** (Both Sensors ON) | ~10–15 $\Omega$ | _________ $\Omega$ | |
| **1/4 Full** (1/4 Sensor OFF) | ~65–75 $\Omega$ | _________ $\Omega$ | |
| **Full** (Both Sensors OFF) | ~180–195 $\Omega$ | _________ $\Omega$ | |

---

## Emergency Safety Bypass
If the interface board fails while underway:
* **To force "Empty" reading:** Touch the VDO Signal wire directly to Ground.
* **To force "Full" reading:** Leave the VDO Signal wire disconnected (Open Circuit).