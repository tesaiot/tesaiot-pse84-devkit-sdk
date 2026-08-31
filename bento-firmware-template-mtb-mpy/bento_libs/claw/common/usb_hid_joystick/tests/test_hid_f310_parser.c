/*******************************************************************************
 * test_hid_f310_parser.c — Host-runnable unit tests for the L1 decoder.
 *
 * Build:  gcc -Wall -Iapi tests/test_hid_f310_parser.c src/hid_f310_parser.c \
 *             -o /tmp/test_hid_f310 && /tmp/test_hid_f310
 * Pass:   exit code 0 + "OK" line per case.
 ******************************************************************************/
#include "hid_f310_parser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int passed = 0, failed = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("  OK   %s\n", name); passed++; } \
    else      { printf("  FAIL %s\n", name); failed++; } \
} while (0)

static void test_null_inputs(void)
{
    printf("[test] null inputs\n");
    f310_decoded_t out;
    CHECK(!f310_parse(NULL, &out), "null raw rejected");
    f310_report_t raw = {0};
    CHECK(!f310_parse(&raw, NULL), "null out rejected");
}

static void test_neutral_report(void)
{
    printf("[test] neutral report (sticks centered, no buttons, hat neutral)\n");
    f310_report_t raw = {
        .left_x = 0x80, .left_y = 0x7F,
        .right_x = 0x80, .right_y = 0x7F,
        .buttons1 = F310_HAT_NEUTRAL,
        .buttons2 = 0,
        .mode = 0, .status = 0,
    };
    f310_decoded_t d;
    CHECK(f310_parse(&raw, &d), "parse returns true");
    CHECK(d.lx == 0 && d.ly == 0, "left stick at zero");
    CHECK(d.rx == 0 && d.ry == 0, "right stick at zero");
    CHECK(!d.a && !d.b && !d.x && !d.y, "face buttons clear");
    CHECK(!d.lb && !d.rb && !d.lt && !d.rt, "shoulders clear");
    CHECK(!d.back && !d.start && !d.l3 && !d.r3, "system buttons clear");
    CHECK(d.dpad_x == 0 && d.dpad_y == 0, "dpad neutral");
}

static void test_left_stick_full_right(void)
{
    printf("[test] left stick pushed fully right\n");
    f310_report_t raw = {.left_x = 0xFF, .left_y = 0x7F, .right_x = 0x80,
                          .right_y = 0x7F, .buttons1 = F310_HAT_NEUTRAL};
    f310_decoded_t d;
    f310_parse(&raw, &d);
    CHECK(d.lx == 127, "lx = +127 at full right");
    CHECK(d.ly == 0,   "ly stays neutral");
}

static void test_left_stick_full_up(void)
{
    printf("[test] left stick pushed fully up (Y-axis inverted, up = positive)\n");
    f310_report_t raw = {.left_x = 0x80, .left_y = 0x00, .right_x = 0x80,
                          .right_y = 0x7F, .buttons1 = F310_HAT_NEUTRAL};
    f310_decoded_t d;
    f310_parse(&raw, &d);
    CHECK(d.lx == 0,   "lx stays neutral");
    CHECK(d.ly == 127, "ly = +127 at full up (Y inverted)");
}

static void test_face_buttons(void)
{
    printf("[test] face button A only (byte 4 bit 5)\n");
    f310_report_t raw = {.left_x = 0x80, .left_y = 0x7F, .right_x = 0x80,
                          .right_y = 0x7F,
                          .buttons1 = F310_BTN_A | F310_HAT_NEUTRAL};
    f310_decoded_t d;
    f310_parse(&raw, &d);
    CHECK(d.a && !d.b && !d.x && !d.y, "only A pressed");
}

static void test_shoulder_LB_plus_RT(void)
{
    printf("[test] LB and RT simultaneously (byte 5 bits 0 + 3)\n");
    f310_report_t raw = {.left_x = 0x80, .left_y = 0x7F, .right_x = 0x80,
                          .right_y = 0x7F,
                          .buttons1 = F310_HAT_NEUTRAL,
                          .buttons2 = F310_BTN_LB | F310_BTN_RT};
    f310_decoded_t d;
    f310_parse(&raw, &d);
    CHECK(d.lb && !d.rb && !d.lt && d.rt, "LB + RT only");
}

static void test_hat_directions(void)
{
    printf("[test] hat all 8 directions + neutral\n");
    struct { uint8_t hat; int8_t dx; int8_t dy; const char *name; } cases[] = {
        {F310_HAT_UP,         0,  1, "UP"},
        {F310_HAT_UP_RIGHT,   1,  1, "UP_RIGHT"},
        {F310_HAT_RIGHT,      1,  0, "RIGHT"},
        {F310_HAT_DOWN_RIGHT, 1, -1, "DOWN_RIGHT"},
        {F310_HAT_DOWN,       0, -1, "DOWN"},
        {F310_HAT_DOWN_LEFT, -1, -1, "DOWN_LEFT"},
        {F310_HAT_LEFT,      -1,  0, "LEFT"},
        {F310_HAT_UP_LEFT,   -1,  1, "UP_LEFT"},
        {F310_HAT_NEUTRAL,    0,  0, "NEUTRAL"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        f310_report_t raw = {.left_x = 0x80, .left_y = 0x7F, .right_x = 0x80,
                              .right_y = 0x7F, .buttons1 = cases[i].hat};
        f310_decoded_t d;
        f310_parse(&raw, &d);
        char name[32];
        snprintf(name, sizeof(name), "hat %s -> (%+d, %+d)",
                 cases[i].name, cases[i].dx, cases[i].dy);
        CHECK(d.dpad_x == cases[i].dx && d.dpad_y == cases[i].dy, name);
    }
}

static void test_deadzone(void)
{
    printf("[test] deadzone math\n");
    CHECK(f310_deadzone(0, 8) == 0,   "zero stays zero");
    CHECK(f310_deadzone(5, 8) == 0,   "5 within +-8 deadzone -> 0");
    CHECK(f310_deadzone(-5, 8) == 0,  "-5 within +-8 deadzone -> 0");
    CHECK(f310_deadzone(9, 8) == 9,   "9 outside +-8 passes through");
    CHECK(f310_deadzone(-9, 8) == -9, "-9 outside +-8 passes through");
    CHECK(f310_deadzone(127, 8) == 127, "extreme value passes through");
    CHECK(f310_deadzone(5, -8) == 0,  "negative threshold treated as abs");
}

int main(void)
{
    test_null_inputs();
    test_neutral_report();
    test_left_stick_full_right();
    test_left_stick_full_up();
    test_face_buttons();
    test_shoulder_LB_plus_RT();
    test_hat_directions();
    test_deadzone();
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
