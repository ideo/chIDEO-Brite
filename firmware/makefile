CC=avr-gcc
OC=avr-objcopy
ODIR=build

UC=attiny402
PORT=/dev/cu.usbmodem142301

AVRCONFIG=~/Downloads/jtag2updi-master/avrdude.conf

.PHONY: clean flash directories

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

directories: $(ODIR)

$(ODIR):
	mkdir -p $@