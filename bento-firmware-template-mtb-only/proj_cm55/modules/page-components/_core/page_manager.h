/*******************************************************************************
 * File Name: page_manager.h
 *
 * Description: Page-based navigation manager for TESAIoT SensorHub UI.
 *              Replaces the TabView with Smart Home-style card menu + pages.
 *              Create-on-demand, destroy-on-leave for minimal LVGL heap usage.
 *
 *              Navigation: Home card grid -> tap card -> full page -> Back.
 *              Uses lv_screen_load_anim() with auto_del for clean transitions.
 *
 *******************************************************************************/

#ifndef PAGE_MANAGER_H
#define PAGE_MANAGER_H

#include "lvgl.h"
#include "ipc_sensorhub.h"
#include "ipc_communication.h"
#include "bsp_feature_flags.h"

#define PM_MAX_PAGES       (24U)  /* == PAGE_ID_COUNT — ids are now explicit and
                                  * never re-numbered, so the table is sized
                                  * for every page the enum names, compiled in
                                  * or not. A slot is a pointer; absence costs
                                  * nothing that matters. */
#define PM_NAV_STACK_DEPTH 12

/* Transition animation duration (ms). Must be >= 10 to avoid LVGL #4212 */
#define PM_ANIM_TIME_MS    300

/*******************************************************************************
 * Page IDs — conditionally compiled via ENABLE_PAGE_* flags.
 * Core pages (Home, Dashboard, Playground) are always present.
 *******************************************************************************/
typedef enum {
    //! [j7_page_id_enum_abi]
    /* ...context: top of the page_id_t enum ... */
    /* EXPLICIT VALUES, NEVER GUARDED — this ordering is ABI.
     *
     * The prebuilt archives in lib/ bake page ids as immediates
     * (lib/ipc_core/PROVENANCE.txt records PAGE_ID_PLAYGROUND = 7). These
     * entries used to be wrapped in #if ENABLE_PAGE_* — so flipping any menu
     * flag renumbered every page after it, and the archives then compared the
     * wrong page at runtime while linking clean. page_id_ordinal_assert.c
     * caught exactly that when the Motor menu was turned off, 2026-08-28.
     *
     * An enum value costs nothing when the page is compiled out, so every
     * page owns its number forever. Values 0-13 are the layout the archives
     * were built against (recovered from the build's own DWARF); pages that
     * were not in that build park stably from 14 up. Add new pages at the
     * end with the next free number. NEVER renumber, NEVER re-guard. */
     //! [j7_page_id_enum_abi]
    PAGE_ID_HOME                = 0,
    PAGE_ID_DASHBOARD           = 1,
    PAGE_ID_MOTION              = 2,
    PAGE_ID_ENVIRON             = 3,
    PAGE_ID_GPIO_RGB            = 4,
    PAGE_ID_MOTOR_CTRL          = 5,
    PAGE_ID_JOYSTICK            = 6,
    PAGE_ID_PLAYGROUND          = 7,   /* baked into lib/ipc_core — see PROVENANCE.txt */
    PAGE_ID_SMART_WATCH         = 8,
    PAGE_ID_WIFI_CONNECT        = 9,
    PAGE_ID_HSM                 = 10,
    PAGE_ID_BENTOCLAW           = 11,
    PAGE_ID_ANIMATION           = 12,
    PAGE_ID_EDGE_AI             = 13,
    /* Not part of the archive-time build; parked on stable numbers. */
    PAGE_ID_CONTROLS            = 14,
    PAGE_ID_FACE_IDENTIFICATION = 15,
    PAGE_ID_SPECTRUM_ANALYZER   = 16,
    PAGE_ID_SMART_CARD          = 17,
    PAGE_ID_BENTO_BUDDY         = 18,
    PAGE_ID_TESAIOT_CONNECT     = 19,
    PAGE_ID_GAME_SNAKE          = 20,
    PAGE_ID_GAME_FLAPPY         = 21,
    PAGE_ID_GAME_PONG           = 22,
    PAGE_ID_GAME_SHOOTER        = 23,

    PAGE_ID_COUNT               = 24
} page_id_t;

/* Fail the build instead of silently dropping a page, which would ship a card
 * that renders and then does nothing when tapped. */
_Static_assert(PAGE_ID_COUNT <= PM_MAX_PAGES,
               "page_id_t exceeds PM_MAX_PAGES — raise PM_MAX_PAGES or drop a page");

/*******************************************************************************
 * Page Definition — function pointers for lifecycle management.
 *******************************************************************************/
//! [j7_page_def_callbacks]
typedef struct {
    const char *name;           /* Display name (e.g., "Dashboard") */
    const char *subtitle;       /* Brief description for home card */
    uint32_t    accent_color;   /* Card border + title color (hex) */
    lv_obj_t *(*create_cb)(void);                         /* Build screen */
    void      (*render_cb)(sensorhub_snapshot_t *snap);   /* Update data */
    void      (*destroy_cb)(void);                        /* Pre-destroy cleanup */
    bool        cacheable;      /* If true, screen survives nav-away (not destroyed) */
} page_def_t;
//! [j7_page_def_callbacks]

/*******************************************************************************
 * Page Manager State
 *******************************************************************************/
typedef struct {
    page_def_t  pages[PM_MAX_PAGES];
    uint8_t     page_count;
    page_id_t   current_page;
    page_id_t   nav_stack[PM_NAV_STACK_DEPTH];
    int8_t      nav_top;        /* -1 = empty stack */
    lv_obj_t   *status_lbl;     /* IDE Connected label (re-created per page) */
    lv_obj_t   *time_lbl;      /* Time display (re-created per page) */
    lv_obj_t   *wifi_lbl;      /* WiFi icon (re-created per page) */
    bool        animating;      /* Guard: true during screen transition */
    lv_obj_t   *cached_screens[PM_MAX_PAGES];  /* Screen cache for cacheable pages */
} page_manager_t;

/*******************************************************************************
 * Public API
 *******************************************************************************/

/** Initialize page manager (zeroes state, sets nav_top = -1). */
void pm_init(page_manager_t *pm);

/** Register a page definition. */
void pm_register(page_manager_t *pm, page_id_t id, const page_def_t *def);

/** Navigate forward to a page (push current to stack, slide left). */
void pm_navigate(page_manager_t *pm, page_id_t target);

/** Navigate back to previous page (pop stack, slide right). */
void pm_back(page_manager_t *pm);

/** Dispatch render to current page's render_cb. Skips if animating. */
void pm_render(page_manager_t *pm, sensorhub_snapshot_t *snap);

/** Get the currently active page ID. */
page_id_t pm_current(const page_manager_t *pm);

/** Check if a navigation animation is in progress. */
bool pm_is_animating(const page_manager_t *pm);

/*******************************************************************************
 * Helper: Create common page elements (re-used by all pages)
 *******************************************************************************/

/**
 * Create status bar on a screen (top 32px).
 * Re-created per page since auto_del destroys the old one.
 *
 * @param screen        Screen object (lv_obj_create(NULL) root).
 * @param out_status    Output: right-aligned status label (IDE Connected).
 * @return Status bar container.
 */
lv_obj_t *pm_create_status_bar(lv_obj_t *screen, lv_obj_t **out_status);

/**
 * Create a Back button (top-left, below status bar).
 *
 * @param screen  Screen object.
 * @param pm      Page manager (for back navigation callback).
 * @return Button object.
 */
lv_obj_t *pm_create_back_button(lv_obj_t *screen, page_manager_t *pm);

/**
 * Create full page header: status bar + back button + title.
 * Returns the content area object (positioned below header).
 *
 * @param screen       Screen object.
 * @param pm           Page manager.
 * @param title        Page title text.
 * @param title_color  Title text color (hex).
 * @return Content area container (below header, full remaining height).
 */
lv_obj_t *pm_create_page_with_header(lv_obj_t *screen, page_manager_t *pm,
                                      const char *title, uint32_t title_color);

/*******************************************************************************
 * Singleton Access (for external callers like IPC handlers)
 *******************************************************************************/

/** Get the global page manager instance (set by sensorhub_ui_init). */
page_manager_t *pm_get_instance(void);

/** Set the global page manager instance (called by sensorhub_ui_init). */
void pm_set_instance(page_manager_t *pm);

/* Shared IPC message buffer placed in .cy_sharedmem.
 * Reused by multiple UI pages to avoid exhausting the 4KB allocatable shared
 * window needed by IPC pipe payload pointers. */
extern ipc_msg_t g_pm_ipc_msg_shared;

#endif /* PAGE_MANAGER_H */
