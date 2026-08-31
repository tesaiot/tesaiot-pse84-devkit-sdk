/*******************************************************************************
 * File Name        : bsp_feature_flags.h
 *
 * Description      : BSP Feature Flags for PSoC Edge E84 projects.
 *                    Enables conditional compilation of sensor drivers,
 *                    MicroPython modules, and LVGL UI pages based on TARGET.
 *
 *                    Flags are set via -DBSP_HAS_XXX=1 in CFLAGS by the
 *                    build system (bsp_features.mk per TARGET).
 *
 * Supported targets:
 *   KIT_PSE84_AI            - PSoC Edge AI Dev Kit
 *
 * Usage in C:
 *   #include "bsp_feature_flags.h"
 *   #if BSP_HAS_DPS368
 *       dps368_init();
 *   #endif
 *
 *******************************************************************************/

#ifndef BSP_FEATURE_FLAGS_H
#define BSP_FEATURE_FLAGS_H

/*******************************************************************************
 * Common sensors (both boards)
 *******************************************************************************/
#ifndef BSP_HAS_BMI270
#define BSP_HAS_BMI270          0
#endif

#ifndef BSP_HAS_BMM350
#define BSP_HAS_BMM350          0
#endif

/*******************************************************************************
 * AI Dev Kit sensors (KIT_PSE84_AI)
 *******************************************************************************/
#ifndef BSP_HAS_DPS368
#define BSP_HAS_DPS368          0
#endif

#ifndef BSP_HAS_SHT40
#define BSP_HAS_SHT40           0
#endif

#ifndef BSP_HAS_RADAR
#define BSP_HAS_RADAR           0
#endif

/*******************************************************************************
 * Optional peripherals (requires base board or extension)
 *******************************************************************************/
#ifndef BSP_HAS_CAPSENSE
#define BSP_HAS_CAPSENSE        0
#endif

#ifndef BSP_HAS_POTENTIOMETER
#define BSP_HAS_POTENTIOMETER   0
#endif

#ifndef BSP_HAS_AUDIO_CODEC
#define BSP_HAS_AUDIO_CODEC     0
#endif

/*******************************************************************************
 * Audio (both boards have PDM mic, different configs)
 *******************************************************************************/
#ifndef BSP_HAS_PDM_MIC
#define BSP_HAS_PDM_MIC         0
#endif

/*******************************************************************************
 * QWA309 training/base board (CAN bus SN65HVD230 + 2 tactile buttons SW9/SW10)
 *******************************************************************************/
#ifndef BSP_HAS_QWA309_BASEBOARD
#define BSP_HAS_QWA309_BASEBOARD 0
#endif

/*******************************************************************************
 * Page Component Toggles (compile-time)
 *
 * Default: ALL pages enabled (preserves current behavior).
 * Set to 0 in bsp_features.mk to exclude a page from the build.
 * Core pages (Home, Dashboard, Playground) are always ON — no flag needed.
 *******************************************************************************/
#ifndef ENABLE_PAGE_MOTION
#define ENABLE_PAGE_MOTION          1
#endif

#ifndef ENABLE_PAGE_ENVIRON
#define ENABLE_PAGE_ENVIRON         1
#endif

#ifndef ENABLE_PAGE_CONTROLS
#define ENABLE_PAGE_CONTROLS        1
#endif

#ifndef ENABLE_PAGE_JOYSTICK
#define ENABLE_PAGE_JOYSTICK        1
#endif

#ifndef ENABLE_PAGE_SMART_CARD
#define ENABLE_PAGE_SMART_CARD      1
#endif

#ifndef ENABLE_PAGE_HSM
#define ENABLE_PAGE_HSM             1
#endif

#ifndef ENABLE_PAGE_BENTOCLAW
#define ENABLE_PAGE_BENTOCLAW       1
#endif

#ifndef ENABLE_PAGE_ANIMATION
#define ENABLE_PAGE_ANIMATION       1
#endif

#ifndef ENABLE_PAGE_FACE_ID
#define ENABLE_PAGE_FACE_ID         1
#endif

#ifndef ENABLE_PAGE_SMART_WATCH
#define ENABLE_PAGE_SMART_WATCH     1
#endif

#ifndef ENABLE_PAGE_SPECTRUM
#define ENABLE_PAGE_SPECTRUM        1
#endif

#ifndef ENABLE_PAGE_WIFI_CONNECT
#define ENABLE_PAGE_WIFI_CONNECT    1
#endif

#ifndef ENABLE_PAGE_GAME_SNAKE
#define ENABLE_PAGE_GAME_SNAKE      0
#endif

#ifndef ENABLE_PAGE_GAME_FLAPPY
#define ENABLE_PAGE_GAME_FLAPPY     0
#endif

#ifndef ENABLE_PAGE_GAME_PONG
#define ENABLE_PAGE_GAME_PONG       0
#endif

#ifndef ENABLE_PAGE_GAME_SHOOTER
#define ENABLE_PAGE_GAME_SHOOTER    0
#endif

/*******************************************************************************
 * Runtime feature toggles (safety gates)
 *******************************************************************************/
#ifndef TESAIOT_ENABLE_FACE_RUNTIME
#define TESAIOT_ENABLE_FACE_RUNTIME 0
#endif

#endif /* BSP_FEATURE_FLAGS_H */
