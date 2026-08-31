/*******************************************************************************
 * File Name: modlcd.c
 *
 * Description: MicroPython 'lcd' module for CM33_NS.
 *              Sends text to CM55 LVGL display via IPC Pipe.
 *              BARE-METAL: No FreeRTOS. Direct Cy_IPC_Pipe_SendMessage().
 *
 *******************************************************************************/

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "ipc_communication.h"
#include <string.h>

/* IPC message buffer in shared memory (accessible by both cores + IPC HW) */
CY_SECTION_SHAREDMEM static ipc_msg_t lcd_ipc_msg;

/* Flag: IPC pipe initialized */
static bool lcd_ipc_initialized = false;
static bool lcd_ipc_ever_succeeded = false;  /* Suppress error until CM55 ready */

/* IPC send retry config (bare-metal busy-wait) */
#define LCD_IPC_SEND_RETRIES    (50)
#define LCD_IPC_RETRY_DELAY_US  (100)
#define LCD_IPC_FLAG_MARKUP     (1UL << 31)
#define LCD_IPC_VALUE_LEN_MASK  (0x7FFFFFFFUL)

/*******************************************************************************
 * lcd_ipc_init() - One-time IPC pipe initialization on CM33 side.
 * Called lazily on first lcd.print() call.
 *******************************************************************************/
static void lcd_ipc_init(void) {
    if (lcd_ipc_initialized) {
        return;
    }
    cm33_ipc_communication_setup();
    Cy_SysLib_Delay(50); /* Let CM55 IPC settle */
    lcd_ipc_initialized = true;
}

/*******************************************************************************
 * lcd_ipc_send_cmd() - Send LCD IPC command to CM55 via IPC Pipe.
 * Returns true on success.
 *******************************************************************************/
static bool lcd_ipc_send_cmd(uint32_t cmd, const char *text, size_t len, uint32_t value) {
    cy_en_ipc_pipe_status_t status;
    int retries = LCD_IPC_SEND_RETRIES;

    /* Prepare message in shared memory */
    memset(&lcd_ipc_msg, 0, sizeof(lcd_ipc_msg));
    lcd_ipc_msg.client_id = CM55_IPC_PIPE_CLIENT_ID;       /* Target client on CM55 */
    lcd_ipc_msg.intr_mask = CY_IPC_CYPIPE_INTR_MASK_EP1;   /* Release mask */
    lcd_ipc_msg.cmd = cmd;
    lcd_ipc_msg.value = value;

    if (text != NULL && len > 0) {
        /* Copy text, truncate to 127 chars + null */
        size_t copy_len = (len >= IPC_DATA_MAX_LEN) ? (IPC_DATA_MAX_LEN - 1) : len;
        memcpy(lcd_ipc_msg.data, text, copy_len);
        lcd_ipc_msg.data[copy_len] = '\0';

        /* Keep flags in high bits, but always send effective length */
        if (cmd == IPC_CMD_LCD_PRINT || cmd == IPC_CMD_LCD_THEME) {
            uint32_t flags = lcd_ipc_msg.value & ~LCD_IPC_VALUE_LEN_MASK;
            lcd_ipc_msg.value = flags | (uint32_t)copy_len;
        }
    }

    /* Send with busy-wait retry (bare-metal, no RTOS) */
    do {
        status = Cy_IPC_Pipe_SendMessage(
            CM55_IPC_PIPE_EP_ADDR,   /* to: CM55 endpoint */
            CM33_IPC_PIPE_EP_ADDR,   /* from: CM33 endpoint */
            (void *)&lcd_ipc_msg,
            NULL                      /* No release callback */
        );
        if (CY_IPC_PIPE_SUCCESS == status) {
            lcd_ipc_ever_succeeded = true;
            return true;
        }
        Cy_SysLib_DelayUs(LCD_IPC_RETRY_DELAY_US);
    } while (--retries > 0);

    return false;
}

/*******************************************************************************
 * Shared implementation:
 *   lcd.print(*args, sep=' ', end='\n', markup=True)
 *   lcd.console(*args, sep=' ', end='\n', markup=True)
 *******************************************************************************/
static mp_obj_t lcd_console_impl(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    lcd_ipc_init();

    enum { ARG_sep, ARG_end, ARG_markup };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_sep, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_QSTR(MP_QSTR__space_)} },
        { MP_QSTR_end, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_QSTR(MP_QSTR__0x0a_)} },
        { MP_QSTR_markup, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = true} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(0, NULL, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    size_t sep_len, end_len;
    const char *sep_data = mp_obj_str_get_data(args[ARG_sep].u_obj, &sep_len);
    const char *end_data = mp_obj_str_get_data(args[ARG_end].u_obj, &end_len);

    vstr_t vstr;
    mp_print_t print;
    vstr_init_print(&vstr, 32, &print);

    for (size_t i = 0; i < n_args; i++) {
        if (i > 0) {
            mp_print_strn(&print, sep_data, sep_len, 0, 0, 0);
        }
        mp_obj_print_helper(&print, pos_args[i], PRINT_STR);
    }
    mp_print_strn(&print, end_data, end_len, 0, 0, 0);

    uint32_t flags = args[ARG_markup].u_bool ? LCD_IPC_FLAG_MARKUP : 0;
    bool ok = lcd_ipc_send_cmd(IPC_CMD_LCD_PRINT, vstr.buf, vstr.len, flags | (uint32_t)vstr.len);
    vstr_clear(&vstr);

    (void)ok;  /* Suppress silently — CM55 may not be ready yet */

    return mp_const_none;
}

/*******************************************************************************
 * MicroPython: lcd.print(*args, sep=' ', end='\n', markup=True)
 *******************************************************************************/
static mp_obj_t lcd_print(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    return lcd_console_impl(n_args, pos_args, kw_args);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(lcd_print_obj, 0, lcd_print);

/*******************************************************************************
 * MicroPython: lcd.console(*args, sep=' ', end='\n', markup=True)
 *******************************************************************************/
static mp_obj_t lcd_console(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    return lcd_console_impl(n_args, pos_args, kw_args);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(lcd_console_obj, 0, lcd_console);

/*******************************************************************************
 * MicroPython: lcd.clear()
 *******************************************************************************/
static mp_obj_t lcd_clear(void) {
    lcd_ipc_init();
    lcd_ipc_send_cmd(IPC_CMD_LCD_CLEAR, NULL, 0, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(lcd_clear_obj, lcd_clear);

/*******************************************************************************
 * MicroPython: lcd.theme(name)
 *******************************************************************************/
static mp_obj_t lcd_theme(mp_obj_t name_obj) {
    lcd_ipc_init();

    size_t len;
    const char *name = mp_obj_str_get_data(name_obj, &len);
    (void)lcd_ipc_send_cmd(IPC_CMD_LCD_THEME, name, len, (uint32_t)len);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(lcd_theme_obj, lcd_theme);

/*******************************************************************************
 * Module Definition
 *******************************************************************************/
static const mp_rom_map_elem_t lcd_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_lcd) },
    { MP_ROM_QSTR(MP_QSTR_print),    MP_ROM_PTR(&lcd_print_obj) },
    { MP_ROM_QSTR(MP_QSTR_console),  MP_ROM_PTR(&lcd_console_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear),    MP_ROM_PTR(&lcd_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_theme),    MP_ROM_PTR(&lcd_theme_obj) },
};
static MP_DEFINE_CONST_DICT(lcd_module_globals, lcd_module_globals_table);

const mp_obj_module_t mp_module_lcd = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&lcd_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_lcd, mp_module_lcd);
