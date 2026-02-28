# Design files for a gobius-to-vdo interface

NOT tested yet!

The schematics png is a screenshot from the kicad files in the g2v subdirectory.

The basic principle is that the gobiusen are switching in and out resistors in a resistance ladder, thus emulating a standard resistive instrument.

![schematics](schematics.png)

* R4 / R6 - must be dimensioned to not burn out the open collector output on the gobiusen or the led in the opto, and at the same time provide enough current to the optos to switch them.
** https://forums.raspberrypi.com/viewtopic.php?t=355542
** https://learnabout-electronics.org/Semiconductors/opto_52.php
** https://www.farnell.com/datasheets/73758.pdf -> 2-4 mA is enough

* LA1 is a "lamp" - can be a LED with a resistor or a ready-made lamp. I've ordered one of these -> https://s.click.aliexpress.com/e/_c4oVMiaf (affiliate link)


