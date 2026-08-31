/*
 * Embedded host bridge for SquareLine smartwatch UI.
 * Keeps original UI code intact while allowing it to run inside a page frame.
 */

#ifndef UI_EMBED_PORT_H
#define UI_EMBED_PORT_H

#include "lvgl.h"
#include <stdbool.h>

void     ui_embed_port_set_parent(lv_obj_t *parent);
void     ui_embed_port_reset(void);
bool     ui_embed_port_is_enabled(void);
lv_obj_t *ui_embed_port_create_root(void);
void     ui_embed_port_show(lv_obj_t *root);
lv_obj_t *ui_embed_port_get_active_root(void);

#endif /* UI_EMBED_PORT_H */
