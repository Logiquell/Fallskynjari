# Hardware

Breadboard prototype. Items marked **[confirm]** were planned or recommended
during development but haven't been checked against the actual board.

## Block diagram

```
                      +3.0 V (PICkit 5 now, 2 x AA later)
                         |
   +---------------------+----------------------------------------+
   |                     |              |            |            |
 PIC16LF1559           AHT20         ADXL345      24AA1025     SSD1306
   |  RB4/SDA ----------+--------------+------------+------------+  (I2C, 100 kHz)
   |  RB6/SCL ----------+--------------+------------+------------+
   |  RA2/INT <---------------------- INT1
   |  RC0 ----[330R]---|>|--- GND   (LED)
   |  RB7/TX ---------------------> CP2102 RXD
   |  RB5/RX <----[1k]------------- CP2102 TXD
   |  RA3/MCLR, RA0/ICSPDAT, RA1/ICSPCLK <---> PICkit 5
```

## PIC16LF1559 pin assignment (20-pin DIP)

Pin functions come from the Microchip device pack
(`PIC12-16F1xxx_DFP 1.7.242`, `PIC16LF1559.PIC`).

| Pin | Port | Function in this design | Connected to |
|---|---|---|---|
| 1 | VDD | Supply | +3.0 V, 100 nF to VSS **[confirm]** |
| 4 | RA3 | MCLR / VPP | PICkit 5 pin 1, 10 kΩ to VDD **[confirm value]** |
| 10 | RB7 | EUSART TX | CP2102 RXD *(not wired yet)* |
| 11 | RB6 | MSSP SCL | I²C SCL (all modules) |
| 12 | RB5 | EUSART RX, weak pull-up, wake-up on falling edge | CP2102 TXD via 1 kΩ *(not wired yet)* |
| 13 | RB4 | MSSP SDA | I²C SDA (all modules) |
| 16 | RC0 | Status LED | LED + 330 Ω to GND **[confirm value]** |
| 17 | RA2 | External interrupt INT (rising edge) | ADXL345 INT1 |
| 18 | RA1 | ICSPCLK | PICkit 5 pin 5 (PGC) |
| 19 | RA0 | ICSPDAT | PICkit 5 pin 4 (PGD) |
| 20 | VSS | Ground | GND |

Pins not listed are unused and left in their reset state (analog inputs).

**SDA pin note:** the MSSP's SDA line can only be on RB4 or RA3, selected by
the `SDSEL` bit in `APFCON`. RA3 is MCLR, so RB4 is the only usable choice.
RB5 (pin 12) next to it is the UART RX pin, not SDA. Mixing these up was one of
the wiring faults during bring-up.

## ICSP (PICkit 5)

| PICkit 5 pin | Signal | PIC pin |
|---|---|---|
| 1 (▶) | MCLR/VPP | 4 |
| 2 | VDD | 1 |
| 3 | GND | 20 |
| 4 | PGD | 19 |
| 5 | PGC | 18 |
| 6–8 | not used | — |

`ipecmd -W3.3` asks the PICkit for 3.3 V. It reports **3.00 V** delivered.

## I²C bus

| Device | 7-bit address | Notes |
|---|---|---|
| AHT20 | 0x38 | Fixed |
| SSD1306 OLED | 0x3C | Firmware also tries 0x3D |
| 24AA1025 | 0x50, 0x54 | Two addresses: lower and upper 64 KB block |
| ADXL345 | 0x53 or 0x1D | 0x53 with SDO low, 0x1D with SDO high; firmware tries both. **[confirm which one the board uses]** |

- **Speed:** 100 kHz (MSSP, `SSP1ADD = 39` at 16 MHz). 100 kHz is chosen
  because the 24AA1025 is only rated for 400 kHz above 2.5 V, and the battery
  voltage drops below that toward end of life.
- **Pull-ups:** only the resistors already on the OLED/AHT20/ADXL345 modules;
  no external ones were added. **[confirm resistor values on the modules]**
- **Address clash:** with A1 = A0 = 1 the 24AA1025 would also answer at 0x53,
  the same as the ADXL345. A0 and A1 are tied to GND to avoid this.

## Module wiring

### 24AA1025 EEPROM (8-pin)

| Pin | Name | Connection | Reason |
|---|---|---|---|
| 1 | A0 | GND | Address bit |
| 2 | A1 | GND | Address bit |
| 3 | A2 | **VCC** | Not an address input on this part; the datasheet requires it at VCC |
| 4 | VSS | GND | |
| 5 | SDA | RB4 (pin 13) | |
| 6 | SCL | RB6 (pin 11) | |
| 7 | WP | GND | Write protect off |
| 8 | VCC | +3.0 V | 100 nF to VSS **[confirm]** |

### ADXL345 (DFRobot SEN0032)

| Board pin | Connection | Note |
|---|---|---|
| VCC | +3.0 V | **[confirm]** whether the board has an on-board regulator, and its dropout voltage |
| GND | GND | |
| CS | VCC | Must be high for I²C; low or floating selects SPI |
| SDA | RB4 (pin 13) | |
| SCL | RB6 (pin 11) | |
| INT1 | RA2 (pin 17) | Push-pull, active high (`DATA_FORMAT.INT_INVERT = 0`) |
| SDO | **[confirm]** | Selects address 0x53 / 0x1D |

### AHT20 and SSD1306 OLED

Four-pin modules: VCC to +3.0 V, GND, SDA to pin 13, SCL to pin 11.

### CP2102 USB-serial adapter *(planned, not wired yet)*

| CP2102 | PIC | Note |
|---|---|---|
| TXD | pin 12 (RB5/RX) through 1 kΩ | Limits back-feeding of the PIC through its input protection diode when the board is unpowered |
| RXD | pin 10 (RB7/TX) | |
| GND | GND | |
| 3V3 / 5V | not connected | The board has its own supply |

## Power

### Supply voltage

2 × AA alkaline gives about 3.2 V when new and falls toward about 2.0 V at end
of life. No regulator is used; every part runs straight from the battery.
Operating ranges from the datasheets:

| Part | Supply range |
|---|---|
| PIC16LF1559 | 1.8–3.6 V |
| ADXL345 | 2.0–3.6 V |
| 24AA1025 | 1.7–5.5 V (400 kHz only above 2.5 V) |
| AHT20 | about 2.0–5.5 V **[confirm against the module datasheet]** |
| SSD1306 module | Usually rated 3.3–5 V because of its on-board regulator and charge pump; may stop working as the battery runs down. Logging keeps working without the display |

The brown-out reset is set to the low level (`BORV = LO`) and switched off in
sleep (`BOREN = NSLEEP`) to save current.

### Current budget (estimate, not measured)

These are **datasheet typical values and rough estimates**. They still have to
be checked with a multimeter.

| Part | State | Approx. current |
|---|---|---|
| PIC16LF1559 | Sleep with WDT | < 1 µA |
| ADXL345 | Measuring at 400 Hz | ~140 µA |
| AHT20 | Idle between measurements | < 1 µA |
| 24AA1025 | Standby | ≤ 5 µA |
| SSD1306 OLED | Display on | ~10–20 mA (depends on pixels lit) |
| Module power LEDs / regulators | Always | **unknown; check the modules for power LEDs** |

With the display off, the average current should be well under 1 mA. To last
48 hours on about 2000 mAh, the average must stay below roughly 40 mA. Measuring
the real figure is a planned test.
