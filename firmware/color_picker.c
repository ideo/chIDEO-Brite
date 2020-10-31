/**
 * Color Picker is a tool for visualizing pixel colors
 * it's pretty crudely put together
 * make sure an ftdi tx is connected to the rxin for the pixel
 * (also helps if you power the the board from the ftdi converter)
 * i use coolterm to communicate with the ftdi and send hex strings
 * you can either send a string of three hex values (ie. 10 20 50) to set the colors BGR
 * or you can send one hex value at a time and it'll set blue, then green, then red
 * 
**/

#ifndef F_CPU
#define F_CPU 3333333UL // 3.333MHz clock speed
#endif

#include <avr/io.h>
#include <util/delay.h>

static const uint8_t false = 0;
static uint8_t true  = 1;

/* pins */
static uint8_t BLUPIN = 1;
static uint8_t REDPIN = 2;
static uint8_t GRNPIN = 3;
static uint8_t TXPIN  = 6;
static uint8_t RXPIN  = 7;

static uint16_t colors[][3] = {{0,0,0}, {50,0,0}, {50,50,0}, {0,50,0}, {0,50,50}, {0,0,50}, {50,0,50}};
uint8_t colorIndex;
uint8_t numColors;

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
	// USART0.CTRLA |= USART_RXCIE_bm;

	// enable TX and RX pins
    USART0.CTRLB |= USART_TXEN_bm | USART_RXEN_bm; 
}

/**** functions ****/

void sendByte(uint8_t data) {
	// wait for data register to be empty
    while (!(USART0.STATUS & USART_DREIF_bm)) {
        ;
    }

	// load data byte to send
    USART0.TXDATAL = data;
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

/**** main function ****/

int main (void){
	clock_init();
	TCA0_init();
	LEDs_init();
	// button_init();
	USART_init();

	// quick delay while configurations take effect
	_delay_ms(10);

	uint8_t count = 0;

	while(1){
        if (USART0.STATUS & USART_RXCIF_bm) {
            // get the received byte
			uint8_t rxByte = USART0.RXDATAL;
            // uint8_t blueByte = USART0.RXDATAL;
            // uint8_t greenByte = USART0.RXDATAL;
            // uint8_t redByte = USART0.RXDATAL;
	
			if (count == 0) {
				TCA0.SINGLE.CMP0BUF = rxByte; // blue
			} else if (count == 1) {
				TCA0.SINGLE.CMP1BUF = rxByte; // green
			} else if (count == 2) {
				TCA0.SINGLE.CMP2BUF = rxByte; // red
			}

			count = (count + 1) % 3;
        }
	}
}