/* sdk-example: core=cm33 variant=both group=io
 * id:      cm33/io/04_gpio_led_button
 * title:   Drive the board LEDs and read the user button
 * teaches: the PDL calls behind gpio.led()/gpio.button(), the drive modes that
 *          make them work, and how to debounce a button without blocking
 * apis:    Cy_GPIO_Pin_FastInit, Cy_GPIO_Set, Cy_GPIO_Clr, Cy_GPIO_Inv,
 *          Cy_GPIO_Write, Cy_GPIO_Read, CYBSP_USER_LED1_PORT, CYBSP_USER_BTN1_PORT
 * entry:   example_io_gpio_led_button
 */
/*******************************************************************************
 * io/04 — plain GPIO: five LEDs out, one button in.
 *
 * There is no BENTO wrapper for this and there does not need to be one. The
 * MicroPython `gpio` module (modgpio.c) is a thin shell over exactly the calls
 * below; this file is what it does, in C, so you can lift it into a project
 * that has no VM.
 *
 * THE PINS, ON THIS BSP
 * ---------------------
 * From bsps/TARGET_KIT_PSE84_AI/config/GeneratedSource/cycfg_pins.h:
 *
 *   CYBSP_USER_LED1   P10.x    a plain user LED
 *   CYBSP_USER_LED2            a plain user LED
 *   CYBSP_USER_LED3   P20.6    RGB red     ) one three-colour part,
 *   CYBSP_USER_LED4   P20.5    RGB blue    ) three separate pins
 *   CYBSP_USER_LED5   P20.4    RGB green   )
 *   CYBSP_USER_BTN1 = CYBSP_SW1, silkscreened SW2 on both kits
 *
 * LED3/4/5 exist only where the BSP defines them, which is why every use is
 * behind `#ifdef CYBSP_USER_LED3_PORT`. Do not count LEDs at compile time from
 * memory of a board — count them from the macros, the way led_table[] in
 * modgpio.c does.
 *
 * The button DEFINE is called BTN1 and the silkscreen says SW2. That is not a
 * mistake in either place: CYBSP_USER_BTN1 aliases CYBSP_SW1, which is the
 * define's name, not the button's. Print the silkscreen name to a user.
 *
 * POLARITY COMES FROM THE BSP, NOT FROM YOU
 * -----------------------------------------
 * cybsp_types.h states it: CYBSP_LED_STATE_ON = 1, CYBSP_LED_STATE_OFF = 0,
 * CYBSP_BTN_PRESSED = 0, CYBSP_BTN_OFF = 1. Use those names. Writing a literal
 * 1 works here and silently inverts on the next board whose LEDs sink instead
 * of source, and that bug looks like "the firmware is dead" because the LEDs
 * are on when they should be off.
 *
 * DRIVE MODES ARE THE WHOLE JOB
 * -----------------------------
 * Cy_GPIO_Pin_FastInit(port, pin, driveMode, outVal, hsiom):
 *
 *   LED     CY_GPIO_DM_STRONG   push-pull, initial value 0 so the pin does not
 *                               flash on the way up
 *   Button  CY_GPIO_DM_PULLUP   initial value 1 — for a pull-up drive mode the
 *                               out-value IS what holds the pull, so passing 0
 *                               gives you a pin that reads 0 forever and a
 *                               button that appears permanently pressed
 *
 *   HSIOM_SEL_GPIO takes the pin back from whatever peripheral the BSP or a
 *   previous owner had it muxed to. On the RGB pins that matters: the
 *   brightness path in modgpio.c muxes them to TCPWM0 lines, and a pin still
 *   muxed to a stopped counter does not respond to Cy_GPIO_Set at all.
 *
 * FOUR WAYS TO WRITE A PIN
 * ------------------------
 *   Cy_GPIO_Set / Cy_GPIO_Clr   one register write, no read — use these
 *   Cy_GPIO_Inv                 toggle, also one write
 *   Cy_GPIO_Write(p, n, v)      when the value is a variable
 *
 * All four are atomic per pin on this part, so no critical section is needed
 * around them even with another task driving a different pin of the same port.
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT DO
 * ---------------------------------------
 * It does not send IPC_CMD_GPIO_LED_STATE to CM55. modgpio.c does, so the
 * on-screen LED widgets follow the Python API; a C caller that wants the same
 * mirroring sends that message itself. Leaving it out here keeps the example
 * to one subject.
 *
 *     make build ENABLE_PAGE_EXAMPLES=1 SDK_EXAMPLE_CM33=cm33/io/04_gpio_led_button
 *******************************************************************************/

#include "../sdk_examples_cm33.h"

#include "cy_pdl.h"
#include "cybsp_types.h"
#include "cycfg_pins.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>

/* One row per LED the BSP actually defines. Built the same way modgpio.c
 * builds led_table[], and for the same reason: the count is a property of the
 * board, discovered from the macros. */
typedef struct {
    GPIO_PRT_Type *port;
    uint32_t       pin;
    const char    *name;
} led_def_t;

static const led_def_t s_leds[] = {
    { CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_NUM, "LED1" },
    { CYBSP_USER_LED2_PORT, CYBSP_USER_LED2_NUM, "LED2" },
#ifdef CYBSP_USER_LED3_PORT
    { CYBSP_USER_LED3_PORT, CYBSP_USER_LED3_NUM, "RGB_RED" },
#endif
#ifdef CYBSP_USER_LED4_PORT
    { CYBSP_USER_LED4_PORT, CYBSP_USER_LED4_NUM, "RGB_BLUE" },
#endif
#ifdef CYBSP_USER_LED5_PORT
    { CYBSP_USER_LED5_PORT, CYBSP_USER_LED5_NUM, "RGB_GREEN" },
#endif
};
#define LED_COUNT   ((unsigned)(sizeof(s_leds) / sizeof(s_leds[0])))

#define BTN_POLL_MS      (10U)
#define BTN_WATCH_MS     (5000U)

/* Consecutive agreeing polls before a level change is believed. Three polls at
 * 10 ms is 30 ms — longer than the few milliseconds a tactile dome bounces
 * for, short enough that nobody feels it. Debounce in TIME, not by reading the
 * pin twice in a row: two reads 200 ns apart are two samples of the same
 * bounce. */
#define BTN_DEBOUNCE_POLLS  (3U)

static void leds_init(void)
{
    for (unsigned i = 0U; i < LED_COUNT; i++) {
        Cy_GPIO_Pin_FastInit(s_leds[i].port, s_leds[i].pin,
                             CY_GPIO_DM_STRONG,
                             CYBSP_LED_STATE_OFF,   /* dark from the first cycle */
                             HSIOM_SEL_GPIO);       /* take the pin back from TCPWM */
    }
}

static void button_init(void)
{
    /* outVal = CYBSP_BTN_OFF (1) is what energises the pull-up. */
    Cy_GPIO_Pin_FastInit(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_NUM,
                         CY_GPIO_DM_PULLUP, CYBSP_BTN_OFF, HSIOM_SEL_GPIO);
}

static bool button_is_pressed(void)
{
    return (Cy_GPIO_Read(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_NUM)
            == CYBSP_BTN_PRESSED);
}

int example_io_gpio_led_button(void)
{
    leds_init();
    button_init();

    printf("[io/04] %u LED(s) on this BSP:", LED_COUNT);
    for (unsigned i = 0U; i < LED_COUNT; i++) {
        printf(" %s", s_leds[i].name);
    }
    printf("\r\n");

    /* --- 1. Set / Clr, one LED at a time ---------------------------------- */
    for (unsigned i = 0U; i < LED_COUNT; i++) {
        Cy_GPIO_Set(s_leds[i].port, s_leds[i].pin);
        vTaskDelay(pdMS_TO_TICKS(120));
        Cy_GPIO_Clr(s_leds[i].port, s_leds[i].pin);
    }

    /* --- 2. Inv, to blink without tracking state -------------------------- */
    for (unsigned n = 0U; n < 6U; n++) {
        Cy_GPIO_Inv(s_leds[0].port, s_leds[0].pin);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    /* Six inversions is an even number, so LED1 is back where it started —
     * which is the trap with Inv: it has no idea what "off" means. End a
     * sequence with an explicit write, never with a lucky parity. */
    Cy_GPIO_Write(s_leds[0].port, s_leds[0].pin, CYBSP_LED_STATE_OFF);

    /* --- 3. Read a pin back ------------------------------------------------
     * Cy_GPIO_Read on an output returns the pin's actual level, so this is a
     * genuine read-back and not an echo of a shadow variable. It is what
     * gpio.led(n).value() reports. */
    Cy_GPIO_Write(s_leds[0].port, s_leds[0].pin, CYBSP_LED_STATE_ON);
    const uint32_t lit = Cy_GPIO_Read(s_leds[0].port, s_leds[0].pin);
    Cy_GPIO_Write(s_leds[0].port, s_leds[0].pin, CYBSP_LED_STATE_OFF);
    const uint32_t dark = Cy_GPIO_Read(s_leds[0].port, s_leds[0].pin);
    printf("[io/04] %s read-back: on=%u off=%u (expect %u and %u)\r\n",
           s_leds[0].name, (unsigned)lit, (unsigned)dark,
           (unsigned)CYBSP_LED_STATE_ON, (unsigned)CYBSP_LED_STATE_OFF);

    if (lit != CYBSP_LED_STATE_ON || dark != CYBSP_LED_STATE_OFF) {
        /* The pin did not follow. Almost always the HSIOM: something else owns
         * the mux. Say so instead of carrying on as if the LED worked. */
        printf("[io/04] the pin did not follow its own write — another\r\n");
        printf("        peripheral still owns the HSIOM mux for it.\r\n");
        return SDK_EX_REFUSED;
    }

    /* --- 4. The button, debounced, mirrored onto LED2 ---------------------- */
    printf("[io/04] press SW2 (the CYBSP_USER_BTN1 define) for %u s — "
           "%s follows it\r\n",
           (unsigned)(BTN_WATCH_MS / 1000U),
           s_leds[(LED_COUNT > 1U) ? 1U : 0U].name);

    const led_def_t *mirror = &s_leds[(LED_COUNT > 1U) ? 1U : 0U];

    bool     stable  = button_is_pressed();   /* believed state              */
    bool     cand    = stable;                /* level being counted in      */
    unsigned agree   = 0U;
    unsigned presses = 0U;

    for (unsigned t = 0U; t < (BTN_WATCH_MS / BTN_POLL_MS); t++) {
        const bool now = button_is_pressed();

        if (now == cand) {
            if (agree < BTN_DEBOUNCE_POLLS) {
                agree++;
            }
        } else {
            cand  = now;
            agree = 1U;
        }

        if (agree >= BTN_DEBOUNCE_POLLS && cand != stable) {
            stable = cand;
            Cy_GPIO_Write(mirror->port, mirror->pin,
                          stable ? CYBSP_LED_STATE_ON : CYBSP_LED_STATE_OFF);
            if (stable) {
                presses++;
                printf("[io/04] SW2 down\r\n");
            } else {
                printf("[io/04] SW2 up\r\n");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }

    /* --- 5. Leave the board as we found it -------------------------------- */
    for (unsigned i = 0U; i < LED_COUNT; i++) {
        Cy_GPIO_Write(s_leds[i].port, s_leds[i].pin, CYBSP_LED_STATE_OFF);
    }

    printf("[io/04] done: %u debounced press(es), all LEDs left off\r\n", presses);
    return SDK_EX_OK;
}
