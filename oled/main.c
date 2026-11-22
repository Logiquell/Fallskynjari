// PIC16LF1559 + 0.96" SSD1306 128x64 I2C OLED + AHT20 temp/humidity sensor
//   SDA = RB4 (pin 13), SCL = RB6 (pin 11), OLED 0x3C, AHT20 0x38 (same bus)
//   LED on RC0 (pin 16) still blinks once per second.
// Internal oscillator at 16 MHz, I2C at 100 kHz.
// No framebuffer (chip has only 512 bytes RAM): text is written straight
// to the display, 8 pages x 21 characters with a 5x7 font.

#pragma config FOSC = INTOSC, WDTE = OFF, PWRTE = ON, MCLRE = ON, CP = OFF
#pragma config BOREN = ON, CLKOUTEN = OFF
#pragma config WRT = OFF, STVREN = ON, BORV = LO, LPBOR = OFF, LVP = ON

#include <xc.h>
#include <stdint.h>

#define _XTAL_FREQ 16000000UL
#define OLED_ADDR  0x3C

// ---------------------------------------------------------------- 5x7 font
// ASCII 0x20..0x7E, 5 column bytes per character, LSB = top row.
static const uint8_t font5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x5F,0x00,0x00}, // !
    {0x00,0x07,0x00,0x07,0x00}, // "
    {0x14,0x7F,0x14,0x7F,0x14}, // #
    {0x24,0x2A,0x7F,0x2A,0x12}, // $
    {0x23,0x13,0x08,0x64,0x62}, // %
    {0x36,0x49,0x55,0x22,0x50}, // &
    {0x00,0x05,0x03,0x00,0x00}, // '
    {0x00,0x1C,0x22,0x41,0x00}, // (
    {0x00,0x41,0x22,0x1C,0x00}, // )
    {0x14,0x08,0x3E,0x08,0x14}, // *
    {0x08,0x08,0x3E,0x08,0x08}, // +
    {0x00,0x50,0x30,0x00,0x00}, // ,
    {0x08,0x08,0x08,0x08,0x08}, // -
    {0x00,0x60,0x60,0x00,0x00}, // .
    {0x20,0x10,0x08,0x04,0x02}, // /
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    {0x00,0x36,0x36,0x00,0x00}, // :
    {0x00,0x56,0x36,0x00,0x00}, // ;
    {0x08,0x14,0x22,0x41,0x00}, // <
    {0x14,0x14,0x14,0x14,0x14}, // =
    {0x00,0x41,0x22,0x14,0x08}, // >
    {0x02,0x01,0x51,0x09,0x06}, // ?
    {0x32,0x49,0x79,0x41,0x3E}, // @
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
    {0x00,0x7F,0x41,0x41,0x00}, // [
    {0x02,0x04,0x08,0x10,0x20}, // backslash
    {0x00,0x41,0x41,0x7F,0x00}, // ]
    {0x04,0x02,0x01,0x02,0x04}, // ^
    {0x40,0x40,0x40,0x40,0x40}, // _
    {0x00,0x01,0x02,0x04,0x00}, // `
    {0x20,0x54,0x54,0x54,0x78}, // a
    {0x7F,0x48,0x44,0x44,0x38}, // b
    {0x38,0x44,0x44,0x44,0x20}, // c
    {0x38,0x44,0x44,0x48,0x7F}, // d
    {0x38,0x54,0x54,0x54,0x18}, // e
    {0x08,0x7E,0x09,0x01,0x02}, // f
    {0x0C,0x52,0x52,0x52,0x3E}, // g
    {0x7F,0x08,0x04,0x04,0x78}, // h
    {0x00,0x44,0x7D,0x40,0x00}, // i
    {0x20,0x40,0x44,0x3D,0x00}, // j
    {0x7F,0x10,0x28,0x44,0x00}, // k
    {0x00,0x41,0x7F,0x40,0x00}, // l
    {0x7C,0x04,0x18,0x04,0x78}, // m
    {0x7C,0x08,0x04,0x04,0x78}, // n
    {0x38,0x44,0x44,0x44,0x38}, // o
    {0x7C,0x14,0x14,0x14,0x08}, // p
    {0x08,0x14,0x14,0x18,0x7C}, // q
    {0x7C,0x08,0x04,0x04,0x08}, // r
    {0x48,0x54,0x54,0x54,0x20}, // s
    {0x04,0x3F,0x44,0x40,0x20}, // t
    {0x3C,0x40,0x40,0x20,0x7C}, // u
    {0x1C,0x20,0x40,0x20,0x1C}, // v
    {0x3C,0x40,0x30,0x40,0x3C}, // w
    {0x44,0x28,0x10,0x28,0x44}, // x
    {0x0C,0x50,0x50,0x50,0x3C}, // y
    {0x44,0x64,0x54,0x4C,0x44}, // z
    {0x00,0x08,0x36,0x41,0x00}, // {
    {0x00,0x00,0x7F,0x00,0x00}, // |
    {0x00,0x41,0x36,0x08,0x00}, // }
    {0x10,0x08,0x08,0x10,0x08}, // ~
};

// ---------------------------------------------------------------- LED / errors
// Error codes, shown forever as N short flashes then a pause:
//   1 = SCL (pin 11) is held low  -> missing pull-up, short, or wiring
//   2 = SDA (pin 13) is held low  -> missing pull-up, short, or wiring
//   3 = I2C timeout / bus collision during transfer
//   4 = no OLED answered at 0x3C or 0x3D
static void led_flash(uint8_t n, uint16_t on_ms)
{
    while (n--) {
        LATCbits.LATC0 = 1;
        for (uint16_t i = 0; i < on_ms; i++) __delay_ms(1);
        LATCbits.LATC0 = 0;
        for (uint16_t i = 0; i < on_ms; i++) __delay_ms(1);
    }
}

static void fail(uint8_t code)
{
    SSP1CON1 = 0;                   // release the bus
    while (1) {
        led_flash(code, 150);
        __delay_ms(1500);
    }
}

// ---------------------------------------------------------------- I2C (MSSP)
static void i2c_wait(void)
{
    uint16_t t = 0;
    while (!PIR1bits.SSP1IF) {
        if (PIR2bits.BCL1IF || ++t == 20000) fail(3);   // ~ tens of ms
    }
    PIR1bits.SSP1IF = 0;
}

static void i2c_init(void)
{
    ANSELBbits.ANSB4 = 0;           // RB4 (SDA) digital
    ANSELBbits.ANSB6 = 0;           // RB6 (SCL) digital
    TRISBbits.TRISB4 = 1;           // MSSP drives the pins itself
    TRISBbits.TRISB6 = 1;
    SSP1STAT = 0x80;                // slew rate control off (100 kHz)
    SSP1ADD  = (_XTAL_FREQ / (4UL * 100000UL)) - 1;   // = 39 -> 100 kHz
    SSP1CON1 = 0x28;                // SSPEN = 1, I2C master mode
    SSP1CON2 = 0x00;
    PIR1bits.SSP1IF = 0;
    PIR2bits.BCL1IF = 0;
}

static void i2c_start(void)        { SSP1CON2bits.SEN = 1; i2c_wait(); }
static void i2c_stop(void)         { SSP1CON2bits.PEN = 1; i2c_wait(); }
static void i2c_write(uint8_t b)   { SSP1BUF = b;          i2c_wait(); }

// Returns 1 if a device ACKs its address.
static uint8_t i2c_probe(uint8_t addr)
{
    i2c_start();
    i2c_write(addr << 1);
    uint8_t ack = !SSP1CON2bits.ACKSTAT;
    i2c_stop();
    return ack;
}

// Read one byte; ack = 1 to ask for more, 0 for the last byte.
static uint8_t i2c_read(uint8_t ack)
{
    SSP1CON2bits.RCEN = 1;
    i2c_wait();
    uint8_t b = SSP1BUF;
    SSP1CON2bits.ACKDT = ack ? 0 : 1;
    SSP1CON2bits.ACKEN = 1;
    i2c_wait();
    return b;
}

static void i2c_restart(void)      { SSP1CON2bits.RSEN = 1; i2c_wait(); }

// ---------------------------------------------------------------- ADXL345
// DFRobot SEN0032. Address 0x53 when SDO is low, 0x1D when SDO is high.
static uint8_t adxl_addr = 0;

static void adxl_write(uint8_t reg, uint8_t val)
{
    i2c_start();
    i2c_write(adxl_addr << 1);
    i2c_write(reg);
    i2c_write(val);
    i2c_stop();
}

static void adxl_read(uint8_t reg, uint8_t *buf, uint8_t n)
{
    i2c_start();
    i2c_write(adxl_addr << 1);
    i2c_write(reg);
    i2c_restart();
    i2c_write((adxl_addr << 1) | 1);
    for (uint8_t i = 0; i < n; i++)
        buf[i] = i2c_read(i < n - 1);
    i2c_stop();
}

// Returns 1 if an ADXL345 (DEVID 0xE5) was found and started.
static uint8_t adxl_init(void)
{
    uint8_t id;
    if      (i2c_probe(0x53)) adxl_addr = 0x53;
    else if (i2c_probe(0x1D)) adxl_addr = 0x1D;
    else return 0;
    adxl_read(0x00, &id, 1);
    if (id != 0xE5) return 0;
    adxl_write(0x2D, 0x00);     // POWER_CTL: standby while configuring
    adxl_write(0x31, 0x0B);     // DATA_FORMAT: full resolution, +-16 g
    adxl_write(0x2C, 0x0A);     // BW_RATE: 100 Hz
    adxl_write(0x2D, 0x08);     // POWER_CTL: measure
    return 1;
}

// Reads X/Y/Z in hundredths of g (3.9 mg per LSB in full resolution).
static void adxl_xyz(int16_t *g100)
{
    uint8_t d[6];
    adxl_read(0x32, d, 6);
    for (uint8_t i = 0; i < 3; i++) {
        int16_t raw = (int16_t)((uint16_t)d[2*i] | ((uint16_t)d[2*i + 1] << 8));
        g100[i] = (int16_t)(((int32_t)raw * 39) / 100);
    }
}

// Format hundredths as a right-aligned "-1.23" style string (6 chars).
static void hundredths_to_str(int16_t v, char *buf)
{
    uint8_t neg = v < 0;
    uint16_t u = neg ? (uint16_t)(-v) : (uint16_t)v;
    buf[6] = '\0';
    buf[5] = (char)('0' + u % 10); u /= 10;
    buf[4] = (char)('0' + u % 10); u /= 10;
    buf[3] = '.';
    int8_t i = 2;
    do { buf[i--] = (char)('0' + u % 10); u /= 10; } while (u && i >= 0);
    if (neg && i >= 0) buf[i--] = '-';
    while (i >= 0) buf[i--] = ' ';
}

// ---------------------------------------------------------------- 24AA1025
// 128 KB I2C EEPROM, A0 = A1 = GND, A2 = VCC.
// Lower 64 KB at 0x50, upper 64 KB at 0x54. 128-byte pages, 5 ms write.
#define EE_ADDR_LO 0x50
#define EE_ADDR_HI 0x54

static uint8_t ee_ctrl(uint32_t addr)
{
    return (addr & 0x10000UL) ? EE_ADDR_HI : EE_ADDR_LO;
}

// Wait for the internal write cycle to finish (ACK polling, max ~10 ms).
static uint8_t ee_wait_ready(uint8_t ctrl)
{
    for (uint8_t i = 0; i < 100; i++) {
        if (i2c_probe(ctrl)) return 1;
        __delay_us(100);
    }
    return 0;
}

// Write n bytes (must not cross a 128-byte page boundary).
static uint8_t ee_write(uint32_t addr, const uint8_t *data, uint8_t n)
{
    uint8_t ctrl = ee_ctrl(addr);
    i2c_start();
    i2c_write(ctrl << 1);
    if (SSP1CON2bits.ACKSTAT) { i2c_stop(); return 0; }
    i2c_write((uint8_t)(addr >> 8));
    i2c_write((uint8_t)addr);
    for (uint8_t i = 0; i < n; i++)
        i2c_write(data[i]);
    i2c_stop();
    return ee_wait_ready(ctrl);
}

static uint8_t ee_read(uint32_t addr, uint8_t *data, uint8_t n)
{
    uint8_t ctrl = ee_ctrl(addr);
    i2c_start();
    i2c_write(ctrl << 1);
    if (SSP1CON2bits.ACKSTAT) { i2c_stop(); return 0; }
    i2c_write((uint8_t)(addr >> 8));
    i2c_write((uint8_t)addr);
    i2c_restart();
    i2c_write((ctrl << 1) | 1);
    for (uint8_t i = 0; i < n; i++)
        data[i] = i2c_read(i < n - 1);
    i2c_stop();
    return 1;
}

// Self-test: write a pattern and its inverse to the last 16 bytes of each
// 64 KB half, read back and compare. Returns a message for the display.
static const char *ee_selftest(void)
{
    static const uint32_t test_addr[2] = { 0x0FFF0UL, 0x1FFF0UL };
    uint8_t w[16], r[16];

    if (!i2c_probe(EE_ADDR_LO)) return "EEPROM: 0x50 missing ";
    if (!i2c_probe(EE_ADDR_HI)) return "EEPROM: 0x54 missing ";

    for (uint8_t half = 0; half < 2; half++) {
        for (uint8_t pass = 0; pass < 2; pass++) {
            for (uint8_t i = 0; i < 16; i++) {
                w[i] = (uint8_t)(0xA5 ^ (i * 17) ^ (half << 4));
                if (pass) w[i] = (uint8_t)~w[i];
            }
            if (!ee_write(test_addr[half], w, 16))
                return half ? "EEPROM: hi write err " : "EEPROM: lo write err ";
            if (!ee_read(test_addr[half], r, 16))
                return half ? "EEPROM: hi read err  " : "EEPROM: lo read err  ";
            for (uint8_t i = 0; i < 16; i++)
                if (r[i] != w[i])
                    return half ? "EEPROM: hi mismatch  " : "EEPROM: lo mismatch  ";
        }
    }
    return "EEPROM: OK (128 KB)  ";
}

// ---------------------------------------------------------------- AHT20
#define AHT20_ADDR 0x38

static uint8_t aht20_status(void)
{
    i2c_start();
    i2c_write((AHT20_ADDR << 1) | 1);
    uint8_t s = i2c_read(0);
    i2c_stop();
    return s;
}

// Returns 1 if the sensor answers (and is calibrated).
static uint8_t aht20_init(void)
{
    if (!i2c_probe(AHT20_ADDR)) return 0;
    if (!(aht20_status() & 0x08)) {         // not calibrated -> init command
        i2c_start();
        i2c_write(AHT20_ADDR << 1);
        i2c_write(0xBE); i2c_write(0x08); i2c_write(0x00);
        i2c_stop();
        __delay_ms(10);
    }
    return 1;
}

// Measures temperature (tenths of degC) and humidity (tenths of %RH).
// Returns 1 on success.
static uint8_t aht20_read(int16_t *temp10, uint16_t *rh10)
{
    i2c_start();
    if (!(i2c_write(AHT20_ADDR << 1), !SSP1CON2bits.ACKSTAT)) { i2c_stop(); return 0; }
    i2c_write(0xAC); i2c_write(0x33); i2c_write(0x00);   // trigger
    i2c_stop();

    __delay_ms(80);
    for (uint8_t tries = 0; (aht20_status() & 0x80) && tries < 10; tries++)
        __delay_ms(10);                     // still busy

    uint8_t d[6];
    i2c_start();
    i2c_write((AHT20_ADDR << 1) | 1);
    for (uint8_t i = 0; i < 6; i++)
        d[i] = i2c_read(i < 5);
    i2c_stop();
    if (d[0] & 0x80) return 0;              // still busy -> give up this round

    uint32_t h = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
    uint32_t t = ((uint32_t)(d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
    *rh10   = (uint16_t)((h * 1000UL) >> 20);
    *temp10 = (int16_t)((t * 2000UL) >> 20) - 500;
    return 1;
}

// Format tenths as a right-aligned "-12.3" style string (6 chars).
static void tenths_to_str(int16_t v, char *buf)
{
    uint8_t neg = v < 0;
    uint16_t u = neg ? (uint16_t)(-v) : (uint16_t)v;
    buf[6] = '\0';
    buf[5] = (char)('0' + u % 10); u /= 10;
    buf[4] = '.';
    int8_t i = 3;
    do { buf[i--] = (char)('0' + u % 10); u /= 10; } while (u && i >= 0);
    if (neg && i >= 0) buf[i--] = '-';
    while (i >= 0) buf[i--] = ' ';
}

// ---------------------------------------------------------------- SSD1306
static uint8_t oled_addr = OLED_ADDR;

static void oled_cmd(uint8_t c)
{
    i2c_start();
    i2c_write(oled_addr << 1);
    i2c_write(0x00);                // control byte: command
    i2c_write(c);
    i2c_stop();
}

// Set the drawing window to columns col..127, pages page..7.
static void oled_goto(uint8_t col, uint8_t page)
{
    oled_cmd(0x21); oled_cmd(col);  oled_cmd(127);
    oled_cmd(0x22); oled_cmd(page); oled_cmd(7);
}

static void oled_init(void)
{
    static const uint8_t init_seq[] = {
        0xAE,           // display off
        0xD5, 0x80,     // clock divide
        0xA8, 0x3F,     // multiplex 64
        0xD3, 0x00,     // display offset 0
        0x40,           // start line 0
        0x8D, 0x14,     // charge pump on
        0x20, 0x00,     // horizontal addressing mode
        0xA1,           // segment remap (flip X)
        0xC8,           // COM scan descending (flip Y)
        0xDA, 0x12,     // COM pins config for 128x64
        0x81, 0xCF,     // contrast
        0xD9, 0xF1,     // pre-charge
        0xDB, 0x40,     // VCOMH level
        0xA4,           // display follows RAM
        0xA6,           // normal (not inverted)
        0xAF            // display on
    };
    for (uint8_t i = 0; i < sizeof init_seq; i++)
        oled_cmd(init_seq[i]);
}

static void oled_clear(void)
{
    oled_goto(0, 0);
    i2c_start();
    i2c_write(oled_addr << 1);
    i2c_write(0x40);                // control byte: data stream
    for (uint16_t i = 0; i < 1024; i++)
        i2c_write(0x00);
    i2c_stop();
}

// Print text at character column cx (0..20) and text row/page (0..7).
static void oled_print(uint8_t cx, uint8_t page, const char *s)
{
    oled_goto(cx * 6, page);
    i2c_start();
    i2c_write(oled_addr << 1);
    i2c_write(0x40);
    while (*s) {
        char c = *s++;
        if (c < 0x20 || c > 0x7E) c = '?';
        for (uint8_t k = 0; k < 5; k++)
            i2c_write(font5x7[c - 0x20][k]);
        i2c_write(0x00);            // 1-pixel gap between characters
    }
    i2c_stop();
}

// Convert n to a right-aligned 5-digit string.
static void u16_to_str(uint16_t n, char *buf)
{
    for (int8_t i = 4; i >= 0; i--) {
        buf[i] = (char)('0' + n % 10);
        n /= 10;
    }
    buf[5] = '\0';
    for (uint8_t i = 0; i < 4 && buf[i] == '0'; i++)
        buf[i] = ' ';
}

// ---------------------------------------------------------------- main
void main(void)
{
    OSCCON = 0b01111010;            // 16 MHz internal oscillator

    ANSELCbits.ANSC0 = 0;           // LED on RC0
    TRISCbits.TRISC0 = 0;
    LATCbits.LATC0 = 0;

    led_flash(3, 80);               // 3 quick flashes = chip is running
    __delay_ms(100);                // let the OLED power up

    // Before enabling I2C, both lines must idle high via the pull-ups.
    ANSELBbits.ANSB4 = 0;
    ANSELBbits.ANSB6 = 0;
    TRISBbits.TRISB4 = 1;
    TRISBbits.TRISB6 = 1;
    __delay_ms(1);
    if (!PORTBbits.RB6) fail(1);
    if (!PORTBbits.RB4) fail(2);

    i2c_init();
    if (!i2c_probe(0x3C)) {
        if (i2c_probe(0x3D)) oled_addr = 0x3D;
        else fail(4);
    }
    oled_init();
    oled_clear();

    // Layout (21 chars x 8 rows):
    //  0 Fallskynjari
    //  1 T:  22.8C  H:  41.5%
    //  2 EEPROM self-test result
    //  3 X:   0.01 g
    //  4 Y:  -0.02 g
    //  5 Z:   1.00 g
    //  6 (status / errors)
    //  7 Seconds:
    oled_print(0, 0, "Fallskynjari");
    oled_print(0, 3, "X:");
    oled_print(0, 4, "Y:");
    oled_print(0, 5, "Z:");
    oled_print(0, 7, "Seconds:");

    __delay_ms(40);                         // AHT20 needs 40 ms after power-up
    uint8_t aht_ok  = aht20_init();
    uint8_t adxl_ok = adxl_init();

    oled_print(0, 2, "EEPROM: testing...   ");
    oled_print(0, 2, ee_selftest());

    uint16_t seconds = 0;
    uint8_t tick = 0;                       // 5 ticks of ~200 ms = 1 s
    char buf[7];

    while (1) {
        // ---- accelerometer, every tick (~5 Hz on screen)
        if (!adxl_ok) adxl_ok = adxl_init();
        if (adxl_ok) {
            int16_t g[3];
            adxl_xyz(g);
            for (uint8_t i = 0; i < 3; i++) {
                hundredths_to_str(g[i], buf);
                oled_print(3, (uint8_t)(3 + i), buf);
                oled_print(9, (uint8_t)(3 + i), " g");
            }
            oled_print(0, 6, "                     ");
        } else {
            oled_print(0, 6, "ADXL345: not found!  ");
        }

        // ---- temperature / humidity and counter, once per second
        if (tick == 0) {
            int16_t t10;
            uint16_t rh10;
            if (!aht_ok) aht_ok = aht20_init();
            if (aht_ok && aht20_read(&t10, &rh10)) {
                oled_print(0, 1, "T:");
                tenths_to_str(t10, buf);
                oled_print(2, 1, buf); oled_print(8, 1, "C  H:");
                tenths_to_str((int16_t)rh10, buf);
                oled_print(13, 1, buf); oled_print(19, 1, "% ");
            } else {
                aht_ok = 0;
                oled_print(0, 1, "AHT20: not found!    ");
            }
            u16_to_str(seconds, buf);
            oled_print(9, 7, buf);
            LATCbits.LATC0 = 1;             // LED on for the first tick
            seconds++;
        } else {
            LATCbits.LATC0 = 0;
            __delay_ms(100);                // ticks without the AHT20 wait
        }
        if (++tick == 5) tick = 0;
        __delay_ms(50);
    }
}
