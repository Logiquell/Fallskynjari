// PIC16LF1559 blink test: LED on RC0 (pin 16) toggles every 500 ms.
// Internal oscillator at 16 MHz.

#pragma config FOSC = INTOSC    // Internal oscillator
#pragma config WDTE = OFF       // Watchdog off
#pragma config PWRTE = ON       // Power-up timer on
#pragma config MCLRE = ON       // MCLR pin is reset (pull-up to VDD)
#pragma config CP = OFF         // No code protection
#pragma config BOREN = ON       // Brown-out reset on
#pragma config CLKOUTEN = OFF   // CLKOUT pin is normal I/O
#pragma config WRT = OFF        // Flash write protection off
#pragma config STVREN = ON      // Stack overflow causes reset
#pragma config BORV = LO        // Low brown-out voltage (suits 3.3 V)
#pragma config LPBOR = OFF      // Low-power BOR off
#pragma config LVP = ON         // Low-voltage programming on (default)

#include <xc.h>

#define _XTAL_FREQ 16000000UL

void main(void)
{
    OSCCON = 0b01111010;        // IRCF = 1111 (16 MHz HF), SCS = 1x (internal)

    ANSELCbits.ANSC0 = 0;       // RC0 digital
    TRISCbits.TRISC0 = 0;       // RC0 output
    LATCbits.LATC0 = 0;

    while (1) {
        LATCbits.LATC0 ^= 1;
        __delay_ms(500);
    }
}
