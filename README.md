# chIDEO-Brite

Design files for the chIDEO-Brite interactive display

### Status

Peripherals are set up and working! Deeper functionality now needs to be developed. (More details on status items at the top of `brite_fw.c`)

### Building

Install Arduino IDE (which installs necessary avr tools).

Under `Tools > Board:... > Board Manager` search for and install `megaTinyCore`. This will install necessary header files for the attiny402.

Open terminal and change directories to wherever you cloned/downloaded this project

```
cd firmware
make
```

### Flashing

Uses https://github.com/ElTangas/jtag2updi running on an arduino to program. Change `AVRCONFIG` to point to the `avrdude.conf` inside of that the jtag2updi directory that you cloned/downloaded.

Either alias avrdude as the location of avrdude on your machine, change `avrdude` under `flash:` in the makefile to point to the location of avrdude on your machine (likely wherever your Arduino IDE is isntalled), or install avrdude through homebrew.

Change `PORT` in the makefile to the port of your Arduino running jtag2updi (will likely start with `/dev/cu.usbmodem`)

Open terminal and change directories to wherever you cloned/downloaded this project

```
cd firmware
make flash
```
