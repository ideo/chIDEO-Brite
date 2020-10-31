/**
 * == TODO List ==
 * x PWM working
 * x button working
 * x serial working
 * x eeprom working
 * x interrupts
 *   x button press
 *   x serial receive
 * - serial parsing
 *   x save state
 *   x load state
 *   - set palette
 *     - select which hardcoded palette to use
 *     - save current palette selection to EEPROM
 * - create palettes and hardcode them in
 * x power management
 *   x explore sleep modes (can only idle sleep since TCA won't run in standby =[ )
 *   - explore hw power (resistors, using one LED vs 2 LEDs)
 * x write eeprom after peiod of inactivity to save on lifetime writes
 *           
 * == For Fun ==
 * - pixel drawer
 * 	 - send an initial byte (something like 0xFF that is impossible with multiple command states set) that sets nodes to pixel draw mode
 *   - second command is count til pixel to be drawn
 *   - each time a node receives the count command, it subtracts one from the count and transmits that new value
 *   - if received count is zero, draw pixel (likely white)
 * - whack-a-peg game
 *   - send an initial byte (something like 0xFF that is impossible with multiple command states set) that sets nodes to whack-a-peg mode
 *   - pegs use timer to randomly illuminate peg
 * 	 - when player pushes a lit peg, it turns off and sends a command like 0x01 that lite brite controller can add up for a total score
 *   - send a final byte that sets all nodes back to normal mode
 * 
 * == helpful info ==
 * - Reset pin config https://www.microchip.com/webdoc/GUID-DDB0017E-84E3-4E77-AAE9-7AC4290E5E8B/index.html?GUID-A92C1757-40E2-4062-AB6D-2536D4C33FB7
 * - TCA appnote http://ww1.microchip.com/downloads/en/AppNotes/TB3217-Getting-Started-with-TCA-90003217A.pdf
 * - USART http://ww1.microchip.com/downloads/en/DeviceDoc/ATtiny202-402-DataSheet-DS40001969B.pdf
 * - http://www.fourwalledcubicle.com/AVRArticles.php
 *
 * == location of iotn402.h ==
 * 
 *  ‎⁨⁨Applications/⁨Arduino.app/Contents⁩/Java/hardware/tools⁩/avr⁩/avr⁩/include⁩/⁨avr⁩/iotn402.h
 * 
 * == byte structure ==
 * 
 *  xxx xxxxx (LSB)
 *  ||| |
 *  ||| Address (bits 0-4)
 *  ||Palette Command (bit 5)
 *  |Load Command (bit 6)
 *  Save Command (bit 7)
 * 
 *  examples
 *  0x39 = use palette 25
 *  0x4a = load pixel color from EEPROM address 10
 *  0x92 = save pixel color to EEPROM address 18
 * 
 * 	Only one of the command bits (bits 5-7) should be high, to signify the command (save, load, or palette select). The address bits are then used
 *  to identify the EEPROM address to save/load pixel color to/from, or which palette to use. Only one of the command bits high allows for error
 *  checking incase of the command bits gets flipped. An addition to this is 'fun mode' being all command bits high, since two command bit flips is
 *  fairly unlikely, and then using address bits to specify which fun mode to go into, and sending 0xFF ends fun mode.
 *  
 **/

#ifndef F_CPU
#define F_CPU 625000UL // 625kHz clock speed
#endif

#include <avr/io.h>
#include <util/delay.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

/* constants */
static const uint8_t FALSE = 0;
static const uint8_t TRUE  = 1;

static const uint8_t CMD_SAVE = 0x80;
static const uint8_t CMD_LOAD = 0x40;
static const uint8_t CMD_PALT = 0x20;

static const uint8_t EEPROM_TEMP_SAVE  = 0x00;
static const uint8_t EEPROM_SAVE_START = 0x10;

static const uint16_t WAIT_PERIOD = 900; // seconds (15 min)

/* pins */
static const uint8_t BTNPIN = 0;
static const uint8_t BTNMSK = 0x01; // mask for button pin
static const uint8_t BLUPIN = 1;
static const uint8_t REDPIN = 2;
static const uint8_t GRNPIN = 3;
static const uint8_t TXPIN  = 6;
static const uint8_t RXPIN  = 7;

/* global user variables */
// static uint16_t colors[][3] = {{0,0,0}, {50,0,0}, {50,50,0}, {0,50,0}, {0,50,50}, {0,0,50}, {50,0,50}};
static uint16_t colors[][3] = {{0,0,0}, {5,0,0}, {5,5,0}, {0,5,0}, {0,5,5}, {0,0,5}, {5,0,5}}; //GBR
// static uint16_t colors[][3] = {{0,0,1}, {0,0,2}, {0,0,3}, {0,0,4}, {0,0,5}, {0,0,6}, {0,0,7}, {0,0,8}, {0,0,9}};
uint8_t colorIndex;
uint8_t numColors;
uint8_t buttonPressed;
uint16_t secondsCount;


/**** peripheral initialization functions ****/

/**
 * Initialize and configure clock to 625kHz
 **/
void clock_init(void) {
	// set the main clock to internal 20MHz oscillator
	_PROTECTED_WRITE(CLKCTRL.MCLKCTRLA, CLKCTRL_CLKSEL_OSC20M_gc);

	// set the main clock prescaler divisor to 32
	// enable clock prescale
	_PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, CLKCTRL_PDIV_32X_gc | CLKCTRL_PEN_bm);
}

/**
 * Initialize and configure button as input with pull-up
 **/
void button_init(void) {
	// set button pin direction to input
	PORTA.DIRCLR = BTNPIN;

	// enable pull-up and interrupt on falling edge
	// PORTA.PIN0CTRL |= PORT_PULLUPEN_bm | PORT_ISC0_bm | PORT_ISC1_bm;
	PORTA.PIN0CTRL |= PORT_PULLUPEN_bm | PORT_ISC0_bm;
}

/**
 * Initialize and configure LED pins as outputs
 **/
void LEDs_init(void) {
	// set pin direction for LEDs to outputs
	PORTA.DIR |= (1 << BLUPIN) | (1 << REDPIN) | (1 << GRNPIN);
}

/**
 * Initialize and configure Timer/Counter A to control PWM for LEDs
 **/
void TCA0_init(void) {
	// set TCA0 to use original pins (this should be set already by default)
	PORTMUX.CTRLC &= ~PORTMUX_TCA00_bm;

	// enable compare 0,1,2 for PMW generation
	// set TCA0 to dual slope PWM, overflow on BOTTOM
	TCA0.SINGLE.CTRLB = TCA_SINGLE_CMP0EN_bm | TCA_SINGLE_CMP1EN_bm | TCA_SINGLE_CMP2EN_bm | TCA_SINGLE_WGMODE_DSBOTTOM_gc;

	// disable counting on Event input
	TCA0.SINGLE.EVCTRL &= ~(TCA_SINGLE_CNTEI_bm);

	// set period to ~305 Hz (plenty enough for eyes to not see)
	// TCA period = clock freq / (2 * TCA prescaler * desired PWM freq)
	TCA0.SINGLE.PERBUF = 0x40; // 64 steps (6 bit color)
	
	// start with compare buffers set to 0 (PWM off)
	TCA0.SINGLE.CMP0BUF = 0x000;
	TCA0.SINGLE.CMP1BUF = 0x000;
	TCA0.SINGLE.CMP2BUF = 0x000;

	// set clock divider to 16
	// enable TCA0
	TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV16_gc | TCA_SINGLE_ENABLE_bm;
}

void TCB0_init(void) {
	// set compare value
    TCB0.CCMP = 0x9896; // period of 1 sec

	// set timer mode to periodic interrupt mode
	TCB0.CTRLB = TCB_CNTMODE_INT_gc;

	// set clock to be the same as TCA0
	// enable TCB0
	TCB0.CTRLA = TCB_CLKSEL_CLKTCA_gc | TCB_ENABLE_bm;
    
	// interrupt not enabled until needed
}

/**
 * Initialize and configure USART
 **/
void USART_init(void) {
	// configure USART pin directions
    PORTA.DIR &= ~(1 << RXPIN);
    PORTA.DIR |= (1 << TXPIN);
    
	// set serial baudrate
	// from microchip's getting started with USART:
	// baud freq = (64 * clock freq) / (S * baud)
	// S = number of samples per bit = 16 (normal mode)
	// not really sure, but i think '+ 0.5' is for rounding up
	float baud = 9600;
	uint16_t baudFreq = (uint16_t)((float)((F_CPU << 2) / baud) + 0.5);
	USART0.BAUD = baudFreq;

	// enable interrupt on receive start (necessary for standy mode)
	USART0.CTRLA |= USART_RXCIE_bm;

	// enable TX and RX pins
    USART0.CTRLB |= USART_TXEN_bm | USART_RXEN_bm;
}

/**** functions ****/

void setColor(uint8_t color) {
	TCA0.SINGLE.CMP0BUF = colors[color][0];
	TCA0.SINGLE.CMP1BUF = colors[color][1];
	TCA0.SINGLE.CMP2BUF = colors[color][2];
}

void sendByte(uint8_t data) {
	// wait for data register to be empty
    while (!(USART0.STATUS & USART_DREIF_bm)) {
        ;
    }

	// load data byte to send
    USART0.TXDATAL = data;
}

void loadColor(uint8_t* eepromIndex) {
	// get color from eeprom index
	colorIndex = eeprom_read_byte(eepromIndex);

	if (colorIndex == 255) {
		// eeprom register got reset
		colorIndex = 0;
	}
	else {
		// wrap index if higher than palette count
		colorIndex = colorIndex % numColors;
	}
	
	// set the LEDs
	setColor(colorIndex);
}

uint8_t handleSerial(uint8_t data) {
	// split data into command and index chunks
	uint8_t command = data & 0xE0;
	uint8_t slot = data & 0x1F;

	// handle command
	if (command == CMD_SAVE) {
		// save color to save slot
		uint8_t* saveIndex = (uint8_t *)(EEPROM_SAVE_START + slot);
		eeprom_update_byte(saveIndex, colorIndex);
	}
	else if (command == CMD_LOAD) {
		// load color from save slot
		uint8_t* eepromIndex = (uint8_t *)(EEPROM_SAVE_START + slot);
		loadColor(eepromIndex);
	}
	else if (command == CMD_PALT) {
		// load palette
	}
	else {
		// oh no! there was an error
		return 1;
	}

	return 0;
}

void handleError(void) {
	// flash red and off to show error
	uint8_t flashCount = 5;
	while(flashCount) {
		setColor(5);
		_delay_ms(500);
		setColor(0);
		_delay_ms(500);
		flashCount--;
	}
}

void colorCycle(void) {
	uint16_t maxBright = 500;
	uint8_t steps = 5;

	for (int i=0; i<maxBright; i+=steps) {
		TCA0.SINGLE.CMP0BUF = i;
		TCA0.SINGLE.CMP1BUF = maxBright - i;
		_delay_ms(50);
	}
	for (int i=0; i<maxBright; i+=steps) {
		TCA0.SINGLE.CMP2BUF = i;
		TCA0.SINGLE.CMP0BUF = maxBright - i;
		_delay_ms(50);
	}
	for (int i=0; i<maxBright; i+=steps) {
		TCA0.SINGLE.CMP1BUF = i;
		TCA0.SINGLE.CMP2BUF = maxBright - i;
		_delay_ms(50);
	}
}

/**** interrupt functions ****/

/**
 * interrupt service routine for portA pin event
 **/
ISR(PORTA_PORT_vect, ISR_BLOCK) {
	if (PORTA.INTFLAGS && BTNMSK) {
		if ((~PORTA.IN & BTNMSK) && !buttonPressed) {
			// flag to ensure button press only counts once
			buttonPressed = TRUE;

			// increment palette to next color
			colorIndex++;
			if (colorIndex >= numColors) { colorIndex = 0; }

			// set color
			setColor(colorIndex);

			// debounce
			_delay_ms(50);

			// start timer for eeprom update
    		TCB0.INTCTRL &= ~TCB_CAPT_bm;
			secondsCount = 0;
    		TCB0.INTCTRL |= TCB_CAPT_bm;
		}
		else if ((PORTA.IN & BTNMSK) && buttonPressed) {
			// clear flag
			buttonPressed = FALSE;

			// debounce
			_delay_ms(50);
		}
		
		// clear the interrupt flag
		PORTA.INTFLAGS |= BTNMSK;
	}
}

/**
 * interrupt service routine for USART receive complete event
 **/
ISR(USART0_RXC_vect, ISR_BLOCK) {
	// check for receive to complete
	while (!(USART0.STATUS & USART_RXCIF_bm)) {
		;
	}
	
	// get the received byte
	uint8_t rxByte = USART0.RXDATAL;

	if(handleSerial(rxByte) == 0) {
		// relay byte
		sendByte(rxByte);
	}
	else {
		// on no! there was an error
		handleError();
		setColor(colorIndex);
	}
}

/**
 * interrupt service routine for TCB timer trigger
 * 
 * TCB timer is used to store the pixel value after WAIT_PERIOD
 * this prevents the EEPROM temp address from wearing out as fast due to less write cycles
 **/
ISR(TCB0_INT_vect) {
	secondsCount++;

	// check if it's been long enough to save current value to temp register
	if (secondsCount >= WAIT_PERIOD) {
		// write color selection to eeprom temp slot if different than previous
		eeprom_update_byte((uint8_t *)0, colorIndex);

		// clear count
		secondsCount = 0;
		
		// disable TCB0 interrupt until color changed again
    	TCB0.INTCTRL &= ~TCB_CAPT_bm;
	}
	
	TCB0.INTFLAGS = TCB_CAPT_bm; /* Clear the interrupt flag */
}

/**** main function ****/

int main (void){
	clock_init();
	TCA0_init();
	TCB0_init();
	LEDs_init();
	button_init();
	USART_init();

	// give USART receive complete highest interrupt priority
	CPUINT.LVL0PRI = USART0_RXC_vect_num;
	
	// quick delay while configurations take effect
	_delay_ms(10);
	
	// configure global variables
	numColors = sizeof(colors)/sizeof(colors[0]);
	buttonPressed = FALSE;
	secondsCount = 0;

	// recall and set last color
	loadColor(0);

	// enable interrupts
	sei();

	// configure sleep mode to idle when entered
	SLPCTRL.CTRLA |= SLPCTRL_SMODE_IDLE_gc;

	while(1){
		sleep_enable();
		sleep_cpu();
		; // everything is handled by interrupts :D
	}
}
