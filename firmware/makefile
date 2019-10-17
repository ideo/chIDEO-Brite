CC=avr-gcc
OC=avr-objcopy
ODIR=build

UC=attiny402
# set PORT as your Arduino usb port
# - likely to begin with /dev/cu.usbmodem on Mac
# - likely to begin with COM on Win
PORT=/dev/cu.usbmodem1441301
# location of avrdude.conf inside of jtag2updi directory
AVRCONFIG=~/Downloads/jtag2updi-master/avrdude.conf

.PHONY: clean flash directories fuses

$(ODIR)/%.o: %.c
	$(CC) -Os -mmcu=$(UC) -o $@ -c $<

all: directories clean $(ODIR)/brite_fw.o
	@echo "building..."
	$(CC) -mmcu=$(UC) -o $(ODIR)/brite_fw.elf $(ODIR)/brite_fw.o
	$(OC) -O ihex $(ODIR)/brite_fw.elf $(ODIR)/brite_fw.hex

clean:
	rm -f $(ODIR)/*

flash: all
	@echo "flashing..."
	avrdude -C $(AVRCONFIG) -c jtag2updi -p t402 -P $(PORT) -U flash:w:$(ODIR)/brite_fw.hex

fuses:
	# set EESAVE bit in SYSCFG0 (fuse5) to retain EEPROM during chip erase
	avrdude -C $(AVRCONFIG) -c jtag2updi -p t402 -P $(PORT) -U fuse5:w:0xF7:m -v

directories: $(ODIR)

# make build directory if it doesn't already exist
$(ODIR):
	mkdir -p $@