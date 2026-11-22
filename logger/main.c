// Fallskynjari - low-power fall detector and temperature/humidity logger
// PIC16LF1559, runs from 2 x AA.
//
// Hardware
//   I2C bus: SDA = RB4 (pin 13), SCL = RB6 (pin 11), 100 kHz
//     AHT20     0x38        temperature / humidity
//     SSD1306   0x3C        0.96" OLED (optional, off most of the time)
//     24AA1025  0x50/0x54   128 KB EEPROM log
//     ADXL345   0x53/0x1D   accelerometer, INT1 -> RA2/INT (pin 17)
//   LED on RC0 (pin 16)
//   Serial console 115200 8N1: TX = RB7 (pin 10), RX = RB5 (pin 12)
//     press Enter to wake it; s status, d dump CSV, E erase, q quit
//
// Operation
//   * The PIC sleeps. The watchdog wakes it about once a second to keep time;
//     its period is calibrated against the 16 MHz HFINTOSC (+-2 %).
//   * Every 180 s: measure temperature/humidity, write a log record.
//   * ADXL345 runs at 400 Hz, FIFO in stream mode, free-fall interrupt on
//     INT1. On a fall the PIC wakes, walks through the buffered and live
//     samples, counts free-fall samples until impact and computes
//     h = g * t^2 / 2. One log record per fall.
//   * OLED is on for 15 s after power-up and 10 s after each fall.
//
// Log: 16-byte records from EEPROM address 0, 8192 records maximum.
//   byte 0      type: 0x01 boot, 0x02 environment, 0x03 fall, 0xFF empty
//   bytes 1-3   time in seconds since boot (little-endian)
//   boot        4-5 LFINTOSC Hz, 6 PCON reset flags
//   environment 4-5 temperature x10 (int16), 6-7 humidity x10 (uint16),
//               0x7FFF / 0xFFFF = sensor error
//   fall        4-5 free-fall samples (2.5 ms each), 6-7 height mm,
//               8-9 impact peak g x100
//   byte 15     checksum = sum of bytes 0-14
//
// Boot error codes (LED: N flashes, pause, repeat):
//   1 SCL low, 2 SDA low, 3 I2C timeout, 5 ADXL345 missing, 6 EEPROM missing

#pragma config FOSC = INTOSC, WDTE = SWDTEN, PWRTE = ON, MCLRE = ON, CP = OFF
#pragma config BOREN = NSLEEP, CLKOUTEN = OFF
#pragma config WRT = OFF, STVREN = ON, BORV = LO, LPBOR = OFF, LVP = ON

#include <xc.h>
#include <stdint.h>
#include <string.h>

#define _XTAL_FREQ 16000000UL

#include "font5x7.h"

// ---------------------------------------------------------------- settings
#define ENV_PERIOD_MS     180000UL  // temperature/humidity interval
#define WDT_PRESCALE      32768UL   // WDTPS = 01010 -> 32768 LFINTOSC cycles
#define FF_LSB            102       // free fall below 0.4 g (256 LSB/g)
#define LAND_LSB          384       // impact above 1.5 g
#define MIN_FF_SAMPLES    20        // 50 ms at 400 Hz (= ADXL TIME_FF)
#define MAX_FALL_SAMPLES  800       // give up after 2 s
#define PEAK_SAMPLES      40        // look for the impact peak for 100 ms
#define DISPLAY_ALWAYS_ON 1         // 1 while testing on PICkit power, 0 on battery
#define BOOT_SCREEN_S     15
#define FALL_SCREEN_S     10
#define REC_SIZE          16
#define REC_COUNT         8192U

#define FF2   ((uint32_t)FF_LSB * FF_LSB)
#define LAND2 ((uint32_t)LAND_LSB * LAND_LSB)

static uint8_t booted;

// ---------------------------------------------------------------- LED / errors
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
    if (booted) RESET();            // glitch while running: restart and resume
    SSP1CON1 = 0;
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
        if (PIR2bits.BCL1IF || ++t == 20000) fail(3);
    }
    PIR1bits.SSP1IF = 0;
}

static void i2c_init(void)
{
    SSP1STAT = 0x80;
    SSP1ADD  = (_XTAL_FREQ / (4UL * 100000UL)) - 1;   // 100 kHz
    SSP1CON1 = 0x28;                // I2C master
    SSP1CON2 = 0x00;
    PIR1bits.SSP1IF = 0;
    PIR2bits.BCL1IF = 0;
}

static void i2c_start(void)        { SSP1CON2bits.SEN  = 1; i2c_wait(); }
static void i2c_restart(void)      { SSP1CON2bits.RSEN = 1; i2c_wait(); }
static void i2c_stop(void)         { SSP1CON2bits.PEN  = 1; i2c_wait(); }
static void i2c_write(uint8_t b)   { SSP1BUF = b;           i2c_wait(); }

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

static uint8_t i2c_probe(uint8_t addr)
{
    i2c_start();
    i2c_write((uint8_t)(addr << 1));
    uint8_t ack = !SSP1CON2bits.ACKSTAT;
    i2c_stop();
    return ack;
}

// Register-style access used by ADXL345.
static void reg_write(uint8_t dev, uint8_t reg, uint8_t val)
{
    i2c_start();
    i2c_write((uint8_t)(dev << 1));
    i2c_write(reg);
    i2c_write(val);
    i2c_stop();
}

static void reg_read(uint8_t dev, uint8_t reg, uint8_t *buf, uint8_t n)
{
    i2c_start();
    i2c_write((uint8_t)(dev << 1));
    i2c_write(reg);
    i2c_restart();
    i2c_write((uint8_t)((dev << 1) | 1));
    for (uint8_t i = 0; i < n; i++)
        buf[i] = i2c_read(i < n - 1);
    i2c_stop();
}

// ---------------------------------------------------------------- ADXL345
static uint8_t adxl_addr;

static uint8_t adxl_reg(uint8_t reg)
{
    uint8_t v;
    reg_read(adxl_addr, reg, &v, 1);
    return v;
}

static uint8_t adxl_init(void)
{
    if      (i2c_probe(0x53)) adxl_addr = 0x53;
    else if (i2c_probe(0x1D)) adxl_addr = 0x1D;
    else return 0;
    if (adxl_reg(0x00) != 0xE5) return 0;           // DEVID

    reg_write(adxl_addr, 0x2D, 0x00);   // POWER_CTL: standby while configuring
    reg_write(adxl_addr, 0x31, 0x0B);   // DATA_FORMAT: full res, +-16 g, INT active high
    reg_write(adxl_addr, 0x2C, 0x0C);   // BW_RATE: 400 Hz
    reg_write(adxl_addr, 0x28, 0x07);   // THRESH_FF: 7 x 62.5 mg = 0.44 g
    reg_write(adxl_addr, 0x29, MIN_FF_SAMPLES / 2);   // TIME_FF: 10 x 5 ms = 50 ms
    reg_write(adxl_addr, 0x2F, 0x00);   // INT_MAP: everything on INT1
    reg_write(adxl_addr, 0x38, 0x80);   // FIFO_CTL: stream mode (keeps last 32)
    reg_write(adxl_addr, 0x2E, 0x04);   // INT_ENABLE: free fall
    reg_write(adxl_addr, 0x2D, 0x08);   // POWER_CTL: measure
    (void)adxl_reg(0x30);               // clear pending interrupts
    return 1;
}

static uint32_t adxl_mag2(void)
{
    uint8_t d[6];
    reg_read(adxl_addr, 0x32, d, 6);
    uint32_t m = 0;
    for (uint8_t i = 0; i < 3; i++) {
        int32_t v = (int16_t)((uint16_t)d[2*i] | ((uint16_t)d[2*i + 1] << 8));
        m += (uint32_t)(v * v);
    }
    return m;
}

static uint16_t isqrt32(uint32_t v)
{
    uint32_t r = 0, b = 1UL << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else            { r >>= 1; }
        b >>= 2;
    }
    return (uint16_t)r;
}

typedef struct {
    uint16_t n;         // free-fall samples (2.5 ms each)
    uint16_t h_mm;
    uint16_t peak_cg;   // impact peak, g x 100
    uint16_t seen;      // samples read in total (for timekeeping)
} fall_t;

// Called after the free-fall interrupt. Walks through the FIFO history and
// live samples: counts consecutive free-fall samples, then the samples until
// impact. Returns 1 if a complete fall (release -> impact) was measured.
static uint8_t fall_measure(fall_t *f)
{
    uint16_t run = 0, post = 0, idle = 0;
    uint32_t peak = 0;
    uint8_t landed = 0;

    f->seen = 0;
    while (f->seen < MAX_FALL_SAMPLES + PEAK_SAMPLES) {
        uint8_t k = adxl_reg(0x39) & 0x3F;          // FIFO entries
        if (k == 0) {
            if (++idle > 5000) break;               // sensor stopped?
            continue;
        }
        idle = 0;
        while (k--) {
            uint32_t m = adxl_mag2();
            f->seen++;
            if (!landed) {
                if (run < MIN_FF_SAMPLES) {
                    run = (m < FF2) ? run + 1 : 0;  // waiting for a real fall
                } else if (m < LAND2) {
                    run++;                          // still in the air
                    if (run > MAX_FALL_SAMPLES) return 0;
                } else {
                    landed = 1;                     // impact
                    peak = m;
                }
            } else {
                if (m > peak) peak = m;
                if (++post >= PEAK_SAMPLES) goto done;
            }
        }
    }
done:
    if (!landed) return 0;
    f->n = run;
    f->h_mm = (uint16_t)(((uint32_t)run * run * 981UL) / 32000UL);   // g t^2 / 2
    f->peak_cg = (uint16_t)(((uint32_t)isqrt32(peak) * 100UL) / 256UL);
    return 1;
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

static uint8_t aht20_init(void)
{
    if (!i2c_probe(AHT20_ADDR)) return 0;
    if (!(aht20_status() & 0x08)) {                 // not calibrated
        i2c_start();
        i2c_write(AHT20_ADDR << 1);
        i2c_write(0xBE); i2c_write(0x08); i2c_write(0x00);
        i2c_stop();
        __delay_ms(10);
    }
    return 1;
}

static uint8_t aht20_read(int16_t *temp10, uint16_t *rh10)
{
    if (!i2c_probe(AHT20_ADDR)) return 0;
    i2c_start();
    i2c_write(AHT20_ADDR << 1);
    i2c_write(0xAC); i2c_write(0x33); i2c_write(0x00);
    i2c_stop();

    __delay_ms(80);
    for (uint8_t tries = 0; (aht20_status() & 0x80) && tries < 10; tries++)
        __delay_ms(10);

    uint8_t d[6];
    i2c_start();
    i2c_write((AHT20_ADDR << 1) | 1);
    for (uint8_t i = 0; i < 6; i++)
        d[i] = i2c_read(i < 5);
    i2c_stop();
    if (d[0] & 0x80) return 0;

    uint32_t h = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
    uint32_t t = ((uint32_t)(d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
    *rh10   = (uint16_t)((h * 1000UL) >> 20);
    *temp10 = (int16_t)((t * 2000UL) >> 20) - 500;
    return 1;
}

// ---------------------------------------------------------------- 24AA1025
#define EE_ADDR_LO 0x50
#define EE_ADDR_HI 0x54

static uint8_t ee_ctrl(uint32_t addr)
{
    return (addr & 0x10000UL) ? EE_ADDR_HI : EE_ADDR_LO;
}

static void ee_wait_ready(uint8_t ctrl)
{
    for (uint8_t i = 0; i < 100; i++) {             // up to ~10 ms
        if (i2c_probe(ctrl)) return;
        __delay_us(100);
    }
}

// n bytes, must not cross a 128-byte page.
static void ee_write(uint32_t addr, const uint8_t *data, uint8_t n)
{
    uint8_t ctrl = ee_ctrl(addr);
    i2c_start();
    i2c_write((uint8_t)(ctrl << 1));
    i2c_write((uint8_t)(addr >> 8));
    i2c_write((uint8_t)addr);
    for (uint8_t i = 0; i < n; i++)
        i2c_write(data[i]);
    i2c_stop();
    ee_wait_ready(ctrl);
}

static void ee_read(uint32_t addr, uint8_t *data, uint8_t n)
{
    uint8_t ctrl = ee_ctrl(addr);
    i2c_start();
    i2c_write((uint8_t)(ctrl << 1));
    i2c_write((uint8_t)(addr >> 8));
    i2c_write((uint8_t)addr);
    i2c_restart();
    i2c_write((uint8_t)((ctrl << 1) | 1));
    for (uint8_t i = 0; i < n; i++)
        data[i] = i2c_read(i < n - 1);
    i2c_stop();
}

// ---------------------------------------------------------------- log
static uint16_t log_next;           // index of the next free record
static uint8_t  rec[REC_SIZE];

static uint8_t rec_type(uint16_t i)
{
    uint8_t t;
    ee_read((uint32_t)i * REC_SIZE, &t, 1);
    return t;
}

static void log_init(void)
{
    // The earlier EEPROM self-test left data in the last 16 bytes of each
    // 64 KB half (records 4095 and 8191). Blank them if they aren't records.
    static const uint16_t old_test[2] = { 4095, 8191 };
    for (uint8_t i = 0; i < 2; i++) {
        uint8_t t = rec_type(old_test[i]);
        if (t != 0xFF && (t < 1 || t > 3)) {
            memset(rec, 0xFF, REC_SIZE);
            ee_write((uint32_t)old_test[i] * REC_SIZE, rec, REC_SIZE);
        }
    }
    // Records are written in order from 0: binary search for the first empty.
    uint16_t lo = 0, hi = REC_COUNT;
    while (lo < hi) {
        uint16_t mid = (uint16_t)((lo + hi) / 2);
        if (rec_type(mid) == 0xFF) hi = mid;
        else                       lo = mid + 1;
    }
    log_next = lo;
}

static void rec_put16(uint8_t i, uint16_t v)
{
    rec[i] = (uint8_t)v;
    rec[i + 1] = (uint8_t)(v >> 8);
}

static void log_commit(uint8_t type, uint32_t t_s)
{
    if (log_next >= REC_COUNT) return;              // full
    rec[0] = type;
    rec[1] = (uint8_t)t_s;
    rec[2] = (uint8_t)(t_s >> 8);
    rec[3] = (uint8_t)(t_s >> 16);
    uint8_t sum = 0;
    for (uint8_t i = 0; i < REC_SIZE - 1; i++) sum += rec[i];
    rec[REC_SIZE - 1] = sum;
    ee_write((uint32_t)log_next * REC_SIZE, rec, REC_SIZE);
    log_next++;
}

// ---------------------------------------------------------------- SSD1306
static uint8_t oled_addr, oled_ok, oled_on;

static void oled_cmd(uint8_t c)
{
    i2c_start();
    i2c_write((uint8_t)(oled_addr << 1));
    i2c_write(0x00);
    i2c_write(c);
    i2c_stop();
}

static void oled_goto(uint8_t col, uint8_t page)
{
    oled_cmd(0x21); oled_cmd(col);  oled_cmd(127);
    oled_cmd(0x22); oled_cmd(page); oled_cmd(7);
}

static void oled_init(void)
{
    static const uint8_t init_seq[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14,
        0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1,
        0xDB, 0x40, 0xA4, 0xA6, 0xAF
    };
    for (uint8_t i = 0; i < sizeof init_seq; i++)
        oled_cmd(init_seq[i]);
    oled_on = 1;
}

static void oled_power(uint8_t on)
{
    if (!oled_ok || on == oled_on) return;
    if (on) { oled_cmd(0x8D); oled_cmd(0x14); oled_cmd(0xAF); }   // pump on, display on
    else    { oled_cmd(0xAE); oled_cmd(0x8D); oled_cmd(0x10); }   // display off, pump off
    oled_on = on;
}

// Write a full 21-character row (text padded with spaces).
static void oled_line(uint8_t row, const char *s)
{
    if (!oled_ok) return;
    oled_goto(0, row);
    i2c_start();
    i2c_write((uint8_t)(oled_addr << 1));
    i2c_write(0x40);
    for (uint8_t col = 0; col < 21; col++) {
        char c = *s ? *s++ : ' ';
        if (c < 0x20 || c > 0x7E) c = '?';
        for (uint8_t k = 0; k < 5; k++)
            i2c_write(font5x7[c - 0x20][k]);
        i2c_write(0x00);
    }
    i2c_write(0x00); i2c_write(0x00);   // columns 126-127
    i2c_stop();
}

// Append v / 10^dec as text to buf, e.g. (-1234, 1) -> "-123.4".
static void cat_fixed(char *buf, int32_t v, uint8_t dec)
{
    char tmp[12];
    uint8_t i = 0;
    uint8_t neg = v < 0;
    uint32_t u = neg ? (uint32_t)(-v) : (uint32_t)v;
    do {
        tmp[i++] = (char)('0' + u % 10);
        u /= 10;
        if (dec && i == dec) tmp[i++] = '.';
    } while (u || (dec && i < dec + 2));
    size_t n = strlen(buf);
    if (neg) buf[n++] = '-';
    while (i) buf[n++] = tmp[--i];
    buf[n] = '\0';
}

static void cat_hex(char *buf, uint8_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = strlen(buf);
    buf[n] = hex[v >> 4];
    buf[n + 1] = hex[v & 0x0F];
    buf[n + 2] = '\0';
}

// Boot error screen: what failed, every address that answers on the bus,
// and the ADXL345 DEVID register if something answers at its addresses.
static void show_fail(uint8_t code)
{
    char s[22];
    if (!oled_ok) return;
    strcpy(s, "BOOT ERROR "); cat_fixed(s, code, 0);
    oled_line(0, s);
    oled_line(1, code == 5 ? "ADXL345 not found" :
                 code == 6 ? "EEPROM not found" : "");
    oled_line(2, "I2C devices found:");

    uint8_t row = 3;
    s[0] = '\0';
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (!i2c_probe(a)) continue;
        strcat(s, "0x"); cat_hex(s, a); strcat(s, " ");
        if (strlen(s) >= 20) { oled_line(row++, s); s[0] = '\0'; }
        if (row > 5) break;
    }
    oled_line(row, s[0] ? s : (row == 3 ? "(none)" : ""));

    strcpy(s, "DEVID 53:");
    if (i2c_probe(0x53)) { uint8_t id; reg_read(0x53, 0x00, &id, 1); cat_hex(s, id); }
    else strcat(s, "--");
    strcat(s, " 1D:");
    if (i2c_probe(0x1D)) { uint8_t id; reg_read(0x1D, 0x00, &id, 1); cat_hex(s, id); }
    else strcat(s, "--");
    oled_line(7, s);
}

// ---------------------------------------------------------------- timekeeping
static uint32_t now_ms;             // time since boot
static uint16_t tick_ms = 1057;     // one watchdog period, calibrated
static uint16_t lf_hz = 31000;      // measured LFINTOSC frequency

// While awake, Timer1 counts LFINTOSC / 8 so time spent awake (display
// updates, sensor reads, fall handling) is added to the clock too.
static void awake_restart(void)
{
    T1CON = 0xF0;                   // TMR1CS = 11: LFINTOSC, 1:8, stopped
    TMR1H = 0;
    TMR1L = 0;
    T1CONbits.TMR1ON = 1;
}

static void awake_flush(void)
{
    T1CONbits.TMR1ON = 0;
    uint16_t c = ((uint16_t)TMR1H << 8) | TMR1L;
    now_ms += ((uint32_t)c * 8000UL) / lf_hz;
    TMR1H = 0;
    TMR1L = 0;
}

// Count LFINTOSC cycles (Timer1) during 200 ms of HFINTOSC time.
static void calibrate(void)
{
    awake_flush();
    T1CON = 0xC0;                   // TMR1CS = 11: LFINTOSC, 1:1, stopped
    TMR1H = 0;
    TMR1L = 0;
    T1CONbits.TMR1ON = 1;
    __delay_ms(200);
    T1CONbits.TMR1ON = 0;
    uint32_t hz = (((uint16_t)TMR1H << 8) | TMR1L) * 5UL;
    if (hz < 20000 || hz > 45000) hz = 31000;       // implausible -> nominal
    lf_hz = (uint16_t)hz;
    tick_ms = (uint16_t)((WDT_PRESCALE * 1000UL + hz / 2) / hz);
    now_ms += 200;
    awake_restart();
}

// ---------------------------------------------------------------- screens
static uint8_t aht_ok;
static int16_t last_t10 = 0x7FFF;
static uint16_t last_rh10;
static uint16_t falls;

static void show_status(void)
{
    char s[22];
    oled_line(0, "Fallskynjari");
    if (last_t10 == 0x7FFF) {
        oled_line(1, "AHT20: error");
    } else {
        strcpy(s, "T:"); cat_fixed(s, last_t10, 1);
        strcat(s, "C  H:"); cat_fixed(s, (int32_t)last_rh10, 1); strcat(s, "%");
        oled_line(1, s);
    }
    strcpy(s, "Log: "); cat_fixed(s, log_next, 0); strcat(s, " / 8192");
    oled_line(2, s);
    strcpy(s, "LFINTOSC: "); cat_fixed(s, lf_hz, 0); strcat(s, " Hz");
    oled_line(3, s);
    strcpy(s, "Tick: "); cat_fixed(s, tick_ms, 0); strcat(s, " ms");
    oled_line(4, s);
    oled_line(5, "ADXL345: 400 Hz, FF");
    oled_line(6, "");
    oled_line(7, DISPLAY_ALWAYS_ON ? "Display: always on" : "Display off in 15 s");
}

static void show_uptime(void)
{
    char s[22];
    strcpy(s, "Up: "); cat_fixed(s, now_ms / 1000, 0); strcat(s, " s");
    oled_line(6, s);
}

static void show_fall(const fall_t *f)
{
    char s[22];
    strcpy(s, "FALL #"); cat_fixed(s, falls, 0);
    oled_line(0, s);
    oled_line(1, "");
    strcpy(s, "t    = "); cat_fixed(s, ((uint32_t)f->n * 5) / 2, 3); strcat(s, " s");
    oled_line(2, s);
    strcpy(s, "h    = "); cat_fixed(s, f->h_mm / 10, 2); strcat(s, " m");
    oled_line(3, s);
    strcpy(s, "peak = "); cat_fixed(s, f->peak_cg, 2); strcat(s, " g");
    oled_line(4, s);
    strcpy(s, "n    = "); cat_fixed(s, f->n, 0); strcat(s, " samples");
    oled_line(5, s);
    oled_line(6, "");
    strcpy(s, "Log: "); cat_fixed(s, log_next, 0); strcat(s, " / 8192");
    oled_line(7, s);
}

// ---------------------------------------------------------------- tasks
static void env_sample(void)
{
    int16_t t;
    uint16_t rh;
    if (!aht_ok) aht_ok = aht20_init();
    if (!(aht_ok && aht20_read(&t, &rh))) {
        aht_ok = 0;
        t = 0x7FFF;
        rh = 0xFFFF;
    }
    last_t10 = t;
    last_rh10 = rh;
    memset(rec, 0, REC_SIZE);
    rec_put16(4, (uint16_t)t);
    rec_put16(6, rh);
    log_commit(0x02, now_ms / 1000);
}

static void fall_event(void)
{
    fall_t f;
    uint8_t ok = fall_measure(&f);
    now_ms += tick_ms / 2;          // woke part-way through a watchdog period
    (void)adxl_reg(0x30);           // clear the interrupt
    if (!ok) return;

    falls++;
    memset(rec, 0, REC_SIZE);
    rec_put16(4, f.n);
    rec_put16(6, f.h_mm);
    rec_put16(8, f.peak_cg);
    log_commit(0x03, now_ms / 1000);

    LATCbits.LATC0 = 1;
    oled_power(1);
    show_fall(&f);
    LATCbits.LATC0 = 0;
}

// ---------------------------------------------------------------- UART console
// EUSART 115200 8N1: TX = RB7 (pin 10) -> CP2102 RXD,
//                    RX = RB5 (pin 12) <- CP2102 TXD (through 1 kOhm).
// A falling edge on RX wakes the PIC (interrupt-on-change). It then takes
// single-key commands until 30 s pass without input.
#define CONSOLE_TIMEOUT_MS 30000U

static void uart_init_pins(void)
{
    ANSELBbits.ANSB5 = 0;
    ANSELBbits.ANSB7 = 0;
    TRISBbits.TRISB5 = 1;           // RX
    LATBbits.LATB7 = 1;             // TX idles high while the UART is off
    TRISBbits.TRISB7 = 0;
    WPUA = 0x00;                    // weak pull-up only on RX, so a missing
    WPUB = 0x20;                    // adapter can't wake the PIC by noise
    OPTION_REGbits.nWPUEN = 0;
    IOCBNbits.IOCBN5 = 1;           // wake on RX falling edge
    IOCBFbits.IOCBF5 = 0;
    INTCONbits.IOCIE = 1;
}

static void uart_on(void)
{
    BAUDCONbits.BRG16 = 1;
    TXSTAbits.BRGH = 1;
    SPBRGH = 0;
    SPBRGL = 34;                    // 16 MHz / (4 x 35) = 114286 baud (-0.8 %)
    TXSTAbits.TXEN = 1;
    RCSTAbits.SPEN = 1;
    RCSTAbits.CREN = 1;
}

static void uart_off(void)
{
    while (!TXSTAbits.TRMT) { }
    RCSTAbits.CREN = 0;
    TXSTAbits.TXEN = 0;
    RCSTAbits.SPEN = 0;
}

static void uart_putc(char c)
{
    while (!PIR1bits.TXIF) { }
    TXREG = (uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

static void uart_line(const char *s)
{
    uart_puts(s);
    uart_puts("\r\n");
}

// Wait up to ms for a key; returns 0 on timeout.
static char uart_getc_timeout(uint16_t ms)
{
    while (ms--) {
        if (RCSTAbits.OERR) { RCSTAbits.CREN = 0; RCSTAbits.CREN = 1; }
        if (PIR1bits.RCIF) return (char)RCREG;
        __delay_ms(1);
    }
    return 0;
}

static void keep_time(void)         // call at least every 15 s while awake
{
    awake_flush();
    awake_restart();
}

static void cmd_help(void)
{
    uart_line("Commands:");
    uart_line("  s  status");
    uart_line("  d  dump log as CSV");
    uart_line("  E  erase log (asks to confirm)");
    uart_line("  q  quit and go back to sleep");
}

static void cmd_status(void)
{
    char s[40];
    keep_time();
    strcpy(s, "records:     "); cat_fixed(s, log_next, 0); strcat(s, " / 8192");
    uart_line(s);
    strcpy(s, "uptime_s:    "); cat_fixed(s, now_ms / 1000, 0);
    uart_line(s);
    strcpy(s, "lfintosc_hz: "); cat_fixed(s, lf_hz, 0);
    uart_line(s);
    strcpy(s, "tick_ms:     "); cat_fixed(s, tick_ms, 0);
    uart_line(s);
    if (last_t10 == 0x7FFF) {
        uart_line("aht20:       error");
    } else {
        strcpy(s, "temp_C:      "); cat_fixed(s, last_t10, 1); uart_line(s);
        strcpy(s, "rh_pct:      "); cat_fixed(s, (int32_t)last_rh10, 1); uart_line(s);
    }
    strcpy(s, "falls:       "); cat_fixed(s, falls, 0);
    uart_line(s);
}

static void cmd_dump(void)
{
    char s[80];
    uart_line("index,time_s,type,temp_C,rh_pct,fall_time_s,fall_height_m,"
              "impact_peak_g,samples,note");
    for (uint16_t i = 0; i < log_next; i++) {
        ee_read((uint32_t)i * REC_SIZE, rec, REC_SIZE);
        uint8_t sum = 0;
        for (uint8_t k = 0; k < REC_SIZE - 1; k++) sum += rec[k];
        uint32_t t = rec[1] | ((uint32_t)rec[2] << 8) | ((uint32_t)rec[3] << 16);
        uint16_t a = rec[4] | ((uint16_t)rec[5] << 8);
        uint16_t b = rec[6] | ((uint16_t)rec[7] << 8);
        uint16_t c = rec[8] | ((uint16_t)rec[9] << 8);

        s[0] = '\0';
        cat_fixed(s, i, 0); strcat(s, ",");
        cat_fixed(s, (int32_t)t, 0); strcat(s, ",");
        switch (rec[0]) {
        case 0x01:
            strcat(s, "boot,,,,,,,lf_hz=");
            cat_fixed(s, a, 0);
            if (rec[6] == 0xEE) strcat(s, " log erased");
            break;
        case 0x02:
            strcat(s, "env,");
            if ((int16_t)a == 0x7FFF) {
                strcat(s, ",,,,,,sensor error");
            } else {
                cat_fixed(s, (int16_t)a, 1); strcat(s, ",");
                cat_fixed(s, b, 1); strcat(s, ",,,,,");
            }
            break;
        case 0x03:
            strcat(s, "fall,,,");
            cat_fixed(s, ((uint32_t)a * 5) / 2, 3); strcat(s, ",");   // s
            cat_fixed(s, b, 3); strcat(s, ",");                        // m
            cat_fixed(s, c, 2); strcat(s, ",");                        // g
            cat_fixed(s, a, 0); strcat(s, ",");
            break;
        default:
            strcat(s, "unknown,,,,,,,");
            break;
        }
        if (sum != rec[REC_SIZE - 1]) strcat(s, " bad checksum");
        uart_line(s);
        if ((i & 31) == 31) keep_time();
    }
    strcpy(s, "# end, ");
    cat_fixed(s, log_next, 0);
    strcat(s, " records");
    uart_line(s);
}

static void cmd_erase(void)
{
    uart_puts("Erase the whole log? Press y to confirm: ");
    char c = uart_getc_timeout(10000);
    uart_line("");
    if (c != 'y') { uart_line("Cancelled."); return; }

    uint16_t pages = (uint16_t)((log_next + 7) / 8);   // 8 records per page
    for (uint16_t p = 0; p < pages; p++) {
        uint32_t addr = (uint32_t)p * 128;
        uint8_t ctrl = ee_ctrl(addr);
        i2c_start();
        i2c_write((uint8_t)(ctrl << 1));
        i2c_write((uint8_t)(addr >> 8));
        i2c_write((uint8_t)addr);
        for (uint8_t k = 0; k < 128; k++) i2c_write(0xFF);
        i2c_stop();
        ee_wait_ready(ctrl);
        if ((p & 31) == 31) { uart_putc('.'); keep_time(); }
    }
    log_next = 0;
    memset(rec, 0, REC_SIZE);
    rec_put16(4, lf_hz);
    rec[6] = 0xEE;                  // marks "log erased" in the boot record
    log_commit(0x01, now_ms / 1000);
    uart_line("");
    uart_line("Log erased.");
}

static void console(void)
{
    uint16_t idle = 0, ms = 0;
    uart_on();
    __delay_ms(2);
    while (PIR1bits.RCIF) (void)RCREG;      // drop the wake-up character
    uart_line("");
    uart_line("Fallskynjari console - h for help");
    uart_puts("> ");

    while (idle < CONSOLE_TIMEOUT_MS) {
        char c = uart_getc_timeout(1);
        if (PORTAbits.RA2) fall_event();    // a fall still gets measured
        if (++ms >= 1000) { ms = 0; keep_time(); }
        if (!c) { idle++; continue; }
        idle = 0;
        if (c == '\r' || c == '\n') { uart_puts("\r\n> "); continue; }
        uart_putc(c);
        uart_line("");
        if (c == 'q') break;
        switch (c) {
        case 'h': case '?': cmd_help();   break;
        case 's':           cmd_status(); break;
        case 'd':           cmd_dump();   break;
        case 'E':           cmd_erase();  break;
        default:            uart_line("Unknown command, h for help"); break;
        }
        uart_puts("> ");
    }
    uart_line("");
    uart_line("Sleeping. Press Enter to wake the console.");
    uart_off();
    IOCBFbits.IOCBF5 = 0;
}

// ---------------------------------------------------------------- main
void main(void)
{
    OSCCON = 0b01111010;            // 16 MHz HFINTOSC
    WDTCON = 0b01010 << 1;          // WDT period 1:32768 (~1 s), still off

    ANSELCbits.ANSC0 = 0;           // LED
    TRISCbits.TRISC0 = 0;
    LATCbits.LATC0 = 0;

    ANSELAbits.ANSA2 = 0;           // ADXL345 INT1 on RA2/INT
    TRISAbits.TRISA2 = 1;

    led_flash(3, 80);
    __delay_ms(100);

    // I2C lines must idle high before the bus is enabled.
    ANSELBbits.ANSB4 = 0;
    ANSELBbits.ANSB6 = 0;
    TRISBbits.TRISB4 = 1;
    TRISBbits.TRISB6 = 1;
    __delay_ms(1);
    if (!PORTBbits.RB6) fail(1);
    if (!PORTBbits.RB4) fail(2);
    i2c_init();

    if      (i2c_probe(0x3C)) { oled_addr = 0x3C; oled_ok = 1; }
    else if (i2c_probe(0x3D)) { oled_addr = 0x3D; oled_ok = 1; }
    if (oled_ok) oled_init();

    aht_ok = aht20_init();
    if (!adxl_init()) { show_fail(5); fail(5); }
    if (!i2c_probe(EE_ADDR_LO) || !i2c_probe(EE_ADDR_HI)) { show_fail(6); fail(6); }

    calibrate();
    log_init();
    memset(rec, 0, REC_SIZE);
    rec_put16(4, lf_hz);
    rec[6] = PCON;
    log_commit(0x01, 0);
    PCON = 0x1F;                    // re-arm reset-cause flags for next boot

    booted = 1;
    now_ms = 0;
    env_sample();
    uint32_t next_env = ENV_PERIOD_MS;

    show_status();
    uint8_t screen_ticks = DISPLAY_ALWAYS_ON ? 0 : BOOT_SCREEN_S;

    OPTION_REGbits.INTEDG = 1;      // ADXL INT1 is active high
    INTCONbits.INTE = 1;            // wake on INT (GIE off: no ISR)

    uart_init_pins();               // serial console wakes on RX
    uart_on();
    {
        char s[40];
        uart_line("");
        strcpy(s, "Fallskynjari boot, records: "); cat_fixed(s, log_next, 0);
        uart_line(s);
        uart_line("Press Enter for the console.");
    }
    uart_off();

    awake_restart();
    while (1) {
        awake_flush();
        INTCONbits.INTF = 0;
        if (!PORTAbits.RA2) {
            WDTCONbits.SWDTEN = 1;
            SLEEP();
            NOP();
            WDTCONbits.SWDTEN = 0;
        }
        awake_restart();

        if (IOCBFbits.IOCBF5) {             // key pressed on the serial port
            now_ms += tick_ms / 2;
            console();
            if (DISPLAY_ALWAYS_ON) show_status();
        } else if (INTCONbits.INTF || PORTAbits.RA2) {
            fall_event();
            screen_ticks = FALL_SCREEN_S;
        } else {
            now_ms += tick_ms;
            if (screen_ticks && --screen_ticks == 0) {
                if (DISPLAY_ALWAYS_ON) show_status();
                else                   oled_power(0);
            }
            if (DISPLAY_ALWAYS_ON && screen_ticks == 0)
                show_uptime();
        }

        if ((int32_t)(now_ms - next_env) >= 0) {
            env_sample();
            calibrate();            // LFINTOSC drifts with temperature
            next_env += ENV_PERIOD_MS;
            if (DISPLAY_ALWAYS_ON && screen_ticks == 0)
                show_status();
            LATCbits.LATC0 = 1;     // short "alive" blink
            __delay_ms(20);
            LATCbits.LATC0 = 0;
        }
    }
}
