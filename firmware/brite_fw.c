/**
 * == TODO List ==
 * x PWM working
 * x button working
 * x serial working
 * x eeprom working
 * 
 * - interrupts?
 * - serial parsing
 * - state saving/recalling
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

static uint8_t false = 0;
static uint8_t true  = 1;

/* pins */
static uint8_t BTNPIN = 0;
static uint8_t BTNMSK = 0x01; // mask for button pin
static uint8_t BLUPIN = 1;
static uint8_t REDPIN = 2;
static uint8_t GRNPIN = 3;
static uint8_t TXPIN  = 6;
static uint8_t RXPIN  = 7;

static uint16_t colors[][3] = {{0,0,0}, {50,0,0}, {50,50,0}, {0,50,0}, {0,50,50}, {0,0,50}, {50,0,50}};

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
	PORTA.PIN0CTRL |= PORT_PULLUPEN_bm | PORT_ISC0_bm | PORT_ISC1_bm;
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

	// enable TX and RX pins
    USART0.CTRLB |= USART_TXEN_bm | USART_RXEN_bm; 
}

void setColor(uint8_t color) {
	TCA0.SINGLE.CMP0BUF = colors[color][0];
	TCA0.SINGLE.CMP1BUF = colors[color][1];
	TCA0.SINGLE.CMP2BUF = colors[color][2];
}

void sendByte(uint8_t b)
{
    while (!(USART0.STATUS & USART_DREIF_bm))
    {
        ;
    }        
    USART0.TXDATAL = b;
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

int main (void){
	clock_init();
	TCA0_init();
	LEDs_init();
	button_init();
	USART_init();

	// quick delay while configurations set
	_delay_ms(10);
	
	uint8_t color = 0;
	uint8_t numColors = sizeof(colors)/sizeof(colors[0]);
	uint8_t buttonPressed = false;

	// recall and set last color
	color = eeprom_read_byte(0);
	if (color > numColors) { color = 0; }
	setColor(color);

	while(1){
		if ((~PORTA.IN & BTNMSK) && !buttonPressed) {
			// flag to ensure button press only counts once
			buttonPressed = true;

			// increment palette to next color
			color++;
			if (color >= numColors) {
				color = 0;
			}

			// set color
			setColor(color);

			// write color selection to eeprom if different than previous
			eeprom_update_byte(0, color);

			_delay_ms(50); // debounce
		} else if ((PORTA.IN & BTNMSK) && buttonPressed) {
			buttonPressed = false;

			_delay_ms(50); // debounce
		}
		
		if (USART0.STATUS & USART_RXCIF_bm) {
			uint8_t rxByte = USART0.RXDATAL;
			sendByte(rxByte + 1);
		}
	}
}
