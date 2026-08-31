/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2022-2024 Infineon Technologies AG
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* MICROPY_HW_BOARD_NAME comes from the board's mpconfigboard.h (and is
 * overridden by a -D on the Eva build). help.c had no includes and printed a
 * hardcoded "KIT_PSE84_AI", so an Eva learner's very first command answered
 * with the wrong board. */
#include "py/mpconfig.h"

const char psoc_edge_help_text[] =
    "BENTO : : Make Anything.\n"
    "PSoC Edge MicroPython by BENTO & TESAIoT\n"
    "\n"
    "Docs  : https://ide.tesaiot.com/\n"
    "Board : " MICROPY_HW_BOARD_NAME "\n"
    "Core  : Arm Cortex-M33 (Non-Secure) + Cortex-M55 (Display/AI)\n"
    "By    : Wiroon Sriborrirux (BDH)\n"
    "\n"
    "Built-in modules:\n"
    "  ui             -- widgets on the board screen (via CM55)\n"
    "  lcd            -- text into the console drawer\n"
    "  gpio           -- LEDs and buttons\n"
    "  sensors        -- IMU, compass, CapSense, potentiometer\n"
    "  dsp            -- filters and tilt/compass maths\n"
    "  wifi           -- WiFi scan, connect, status\n"
    "  mqtt           -- MQTT publish/subscribe (plaintext 1883)\n"
    "  edge_ai        -- run the on-board models\n"
    "  optiga         -- OPTIGA Trust M secure element\n"
    "  machine        -- low-level hardware (Pin, I2C, PDM_PCM, RTC)\n"
    "  psoc_edge      -- QSPI flash, board-specific APIs\n"
    "\n"
    "  help('modules') lists every module this build actually has.\n"
    "\n"
    "Control commands:\n"
    "  CTRL-C -- interrupt a running program\n"
    "  CTRL-D -- soft reset\n"
    "  CTRL-E -- paste mode\n"
    "\n"
    "For help on an object : help(obj)\n"
    "List available modules : help('modules')\n"
;
