/*******************************************************************************
 * File:    arduino_shield_qwa309.c
 *
 * Purpose: Board descriptor for the Arduino Uno R3 header on the QWA309 base
 *          board (TESAIoT Dev Kit: PSoC Edge E84 AI SoM on QWA309).
 *
 *          This is the ONLY file in the shield stack that knows physical PSoC
 *          pins. Everything above it speaks ARDUINO_D9, not P15.2.
 *
 * Source:  Schematic sheet 17, QWA309_Arduino_Header.SchDoc, revision V3_P1
 *          dated 2026-03-06. Connectors J7 (10-pin IOH), J8 (8-pin POWER),
 *          J9 (8-pin IOL), J10 (6-pin ADC). The mapping was derived twice by
 *          independent methods -- reading the rendered sheet, and walking the
 *          HSIOM pin-function table against the net names -- and both agree.
 *
 * Data,    Board defects live here as capability bits rather than as #if
 * not      ladders in driver code. A driver asks for what it needs and is told
 * #ifdefs: no, once, with a reason. See arduino_shield.h.
 *
 * Contents:
 *          - the capability table and pin names: portable C, host-testable
 *          - the physical ops: PSoC PDL, compiled only for the target
 ******************************************************************************/

#include "arduino_shield.h"

/*******************************************************************************
 * What this board does and does not bring out
 *
 * Every digital pin passes through a TXB0106 auto-direction level shifter
 * (U7/U8/U9 on sheets 9/10/11), MCU side 1.8 V, header side 3.3 V. Those pins
 * carry ARDUINO_CAP_LEVEL_SHIFTED: no pull stiffer than about 50 kOhm may be
 * applied to them, and never a pull-up to 5 V -- the shifter's ESD diodes clamp
 * into +3.3V_BB, which is how an earlier incident took down the shared I2C bus.
 *
 * SDA and SCL are the exception: direct 3.3 V, no shifter in the path.
 *
 * A0-A3 are not routed at all. Neither are VIN, RESET, IOREF or AREF.
 *
 * D2 (P13.0) and D3 (P13.5) carry ARDUINO_CAP_ERRATUM: their port
 * configuration nibble does not latch an output drive. This is proven on
 * hardware, not inferred. P13.5 additionally has no TCPWM route in the HSIOM
 * table, so it could not do PWM even if it drove.
 ******************************************************************************/

#define DIG         (ARDUINO_CAP_ROUTED | ARDUINO_CAP_DIGITAL_IN | \
                     ARDUINO_CAP_DIGITAL_OUT | ARDUINO_CAP_LEVEL_SHIFTED)

static const uint16_t s_qwa309_caps[ARDUINO_PIN_COUNT] =
{
    [ARDUINO_D0]  = DIG | ARDUINO_CAP_UART,     /* P15.0  SCB9 UART RX        */
    [ARDUINO_D1]  = DIG | ARDUINO_CAP_UART,     /* P15.1  SCB9 UART TX        */
    [ARDUINO_D2]  = DIG | ARDUINO_CAP_ERRATUM,  /* P13.0  output does not latch */
    [ARDUINO_D3]  = DIG | ARDUINO_CAP_ERRATUM,  /* P13.5  no TCPWM, and same erratum */
    [ARDUINO_D4]  = DIG,                        /* P13.6                      */
    [ARDUINO_D5]  = DIG | ARDUINO_CAP_PWM,      /* P13.4  TCPWM line_compl 5  */
    [ARDUINO_D6]  = DIG | ARDUINO_CAP_PWM,      /* P13.3  TCPWM line 5        */
    [ARDUINO_D7]  = DIG,                        /* P13.7                      */
    [ARDUINO_D8]  = DIG | ARDUINO_CAP_PWM,      /* P15.3  net "ADC_3_PWM3-"   */
    [ARDUINO_D9]  = DIG | ARDUINO_CAP_PWM,      /* P15.2  net "ADC_2_PWM3+"   */
    [ARDUINO_D10] = DIG | ARDUINO_CAP_PWM | ARDUINO_CAP_SPI,  /* P9.0  SPI SS   */
    [ARDUINO_D11] = DIG | ARDUINO_CAP_PWM | ARDUINO_CAP_SPI,  /* P9.2  SPI MOSI */
    [ARDUINO_D12] = DIG | ARDUINO_CAP_SPI,      /* P9.1  SPI MISO             */
    [ARDUINO_D13] = DIG | ARDUINO_CAP_SPI,      /* P9.3  SPI SCK              */

    /* A0-A3 are brought to J10 pins 1-4 and stop there -- no MCU connection. */
    [ARDUINO_A0]  = 0U,
    [ARDUINO_A1]  = 0U,
    [ARDUINO_A2]  = 0U,
    [ARDUINO_A3]  = 0U,

    /* A4/A5 follow the Uno R3 convention of doubling as SDA/SCL, which is what
     * this board wires them to. Not level-shifted, and not usable as GPIO. */
    [ARDUINO_A4]  = ARDUINO_CAP_ROUTED | ARDUINO_CAP_I2C,   /* P17.1 SDA */
    [ARDUINO_A5]  = ARDUINO_CAP_ROUTED | ARDUINO_CAP_I2C,   /* P17.0 SCL */
};

/*
 * ADC is deliberately absent from every entry.
 *
 * The net names on P15.2/P15.3 read "ADC_2" and "ADC_3", and the J10 block is
 * annotated "ADC x4 *1V8 *mux", so an analogue path plausibly exists. Plausible
 * is not the same as verified, and claiming a capability we have not proven
 * would push the discovery of its absence out to a bench session. A driver that
 * asks for ADC gets ARDUINO_ERR_NO_CAP today; when someone measures it, this
 * table is the one line that changes.
 */

static const char * const s_qwa309_names[ARDUINO_PIN_COUNT] =
{
    [ARDUINO_D0]  = "P15.0", [ARDUINO_D1]  = "P15.1",
    [ARDUINO_D2]  = "P13.0", [ARDUINO_D3]  = "P13.5",
    [ARDUINO_D4]  = "P13.6", [ARDUINO_D5]  = "P13.4",
    [ARDUINO_D6]  = "P13.3", [ARDUINO_D7]  = "P13.7",
    [ARDUINO_D8]  = "P15.3", [ARDUINO_D9]  = "P15.2",
    [ARDUINO_D10] = "P9.0",  [ARDUINO_D11] = "P9.2",
    [ARDUINO_D12] = "P9.1",  [ARDUINO_D13] = "P9.3",
    [ARDUINO_A0]  = "n/c",   [ARDUINO_A1]  = "n/c",
    [ARDUINO_A2]  = "n/c",   [ARDUINO_A3]  = "n/c",
    [ARDUINO_A4]  = "P17.1", [ARDUINO_A5]  = "P17.0",
};

/*******************************************************************************
 * Physical access
 *
 * The ops vtable is supplied by the target-only HAL translation unit. Keeping
 * it external means this file -- the table, which is the part that is easy to
 * get wrong and easy to test -- builds and is verified on a host.
 ******************************************************************************/

extern const arduino_ops_t  arduino_qwa309_ops;
extern const arduino_i2c_t  arduino_qwa309_i2c;

const arduino_board_t arduino_board_qwa309 =
{
    .name          = "QWA309",
    .caps          = s_qwa309_caps,
    .pin_names     = s_qwa309_names,
    .ops           = &arduino_qwa309_ops,
    .i2c           = &arduino_qwa309_i2c,

    /* J8 pin 7 is IOREF and it is not connected, along with VIN, RESET and
     * AREF. A shield that selects its logic voltage from IOREF -- which the
     * Adafruit Motor Shield does by default since March 2024 -- cannot do so
     * here and must be strapped by hand. Exposing this lets a driver say so
     * instead of leaving the user with a device that simply never enumerates. */
    .ioref_present = false,

    .logic_mv      = 3300U,
};
