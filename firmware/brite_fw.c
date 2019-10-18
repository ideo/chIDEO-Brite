/**
 * == TODO List ==
 * x PWM working
 * x button working
 * x serial working
 * x eeprom working
 * x interrupts
 *   x button press
 *   x serial receive
 * 
 * - serial parsing
 *   x save state
 *   x load state
 *   - set palette
 * - power management
 * - write eeprom after peiod of inactivity to save on lifetime writes
 * 
 * == helpful info ==
 * - Reset pin config https://www.microchip.com/webdoc/GUID-DDB0017E-84E3-4E77-AAE9-7AC4290E5E8B/index.html?GUID-A92C1757-40E2-4062-AB6D-2536D4C33FB7
 * - TCA appnote http://ww1.microchip.com/downloads/en/AppNotes/TB3217-Getting-Started-with-TCA-90003217A.pdf
 * - USART http://ww1.microchip.com/downloads/en/DeviceDoc/ATtiny202-402-DataSheet-DS40001969B.pdf
 * - http://www.fourwalledcubicle.com/AVRArticles.php
 **/

#ifndef F_CPU
#define F_CPU 3333333UL // 3.333MHz clock speed
#endif

#include <avr/io.h>
#include <util/delay.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>

/* constants */
static const uint8_t false = 0;
static const uint8_t true  = 1;

static const uint8_t CMD_SAVE = 0x80;
static const uint8_t CMD_LOAD = 0x40;
static const uint8_t CMD_PALT = 0x20;

static const uint8_t EEPROM_TEMP_SAVE  = 0x00;
static const uint8_t EEPROM_SAVE_START = 0x10;

/* pins */
static const uint8_t BTNPIN = 0;
static const uint8_t BTNMSK = 0x01; // mask for button pin
static const uint8_t BLUPIN = 1;
static const uint8_t REDPIN = 2;
static const uint8_t GRNPIN = 3;
static const uint8_t TXPIN  = 6;
static const uint8_t RXPIN  = 7;

/* global user variables */
static uint16_t colors[][3] = {{0,0,0}, {50,0,0}, {50,50,0}, {0,50,0}, {0,50,50}, {0,0,50}, {50,0,50}};
uint8_t colorIndex;
uint8_t numColors;

uint8_t buttonPressed;

/**** peripheral initialization functions ****/

/**
 * Initialize and configure clock to 3.333MHz
 **/
void clock_init(void) {
	// set the main clock to internal 20Hz oscillator
	_PROTECTED_WRITE(CLKCTRL.MCLKCTRLA, CLKCTRL_CLKSEL_OSC20M_gc);

	// set the main clock prescaler divisor to 6
	// enable clock prescale
	_PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, CLKCTRL_PDIV_6X_gc | CLKCTRL_PEN_bm);
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

	// set period to 1000 Hz (using TCA prescale of 4)
	// TCA period = clock freq / (2 * TCA prescaler * desired PWM freq)
	TCA0.SINGLE.PERBUF = 0x01A0;
	
	// start with compare buffers set to 0 (PWM off)
	TCA0.SINGLE.CMP0BUF = 0x000;
	TCA0.SINGLE.CMP1BUF = 0x000;
	TCA0.SINGLE.CMP2BUF = 0x000;

	// set clock divider 4
	// enable TCA0
	TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV4_gc | TCA_SINGLE_ENABLE_bm;
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

	// enable interrupt on receive complete
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
	} else {
		// wrap index is higher than palette count
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
	} else if (command == CMD_LOAD) {
		// load color from save slot
		uint8_t* eepromIndex = (uint8_t *)(EEPROM_SAVE_START + slot);
		loadColor(eepromIndex);
	} else if (command == CMD_PALT) {
		// load palette
	} else {
		// oh no! there was an error
		return 1;
	}

	return 0;
}

void handleError(void) {
	// flash red and off to show error
	while(1) {
		setColor(5);
		_delay_ms(500);
		setColor(0);
		_delay_ms(500);
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
			buttonPressed = true;

			// increment palette to next color
			colorIndex++;
			if (colorIndex >= numColors) { colorIndex = 0; }

			// set color
			setColor(colorIndex);

			// debounce
			_delay_ms(50);

			// write color selection to eeprom if different than previous
			eeprom_update_byte((uint8_t *)0, colorIndex);
		} else if ((PORTA.IN & BTNMSK) && buttonPressed) {
			// clear flag
			buttonPressed = false;

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
	// check the receive complete status bit
	// this check might be unnecessary, but whatevs
	if (USART0.STATUS & USART_RXCIF_bm) {
		// get the received byte
		uint8_t rxByte = USART0.RXDATAL;

		if(handleSerial(rxByte)) {
			// on no! there was an error
			handleError();
		}

		// relay byte
		sendByte(rxByte);
	}
}

/**** main function ****/

int main (void){
	clock_init();
	TCA0_init();
	LEDs_init();
	button_init();
	USART_init();

	// give USART receive complete highest interrupt priority
	CPUINT.LVL0PRI = USART0_RXC_vect_num;
	
	// quick delay while configurations take effect
	_delay_ms(10);
	
	// set global variables
	numColors = sizeof(colors)/sizeof(colors[0]);
	buttonPressed = false;

	// recall and set last color
	loadColor(0);

	// enable interrupts
	sei();

	while(1){
		; // everything is handled by interrupts :D
	}
}
