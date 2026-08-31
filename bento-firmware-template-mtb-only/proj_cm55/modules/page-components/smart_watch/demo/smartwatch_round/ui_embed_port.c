#include "ui_embed_port.h"

static lv_obj_t *s_embed_parent = NULL;
static lv_obj_t *s_active_root = NULL;

static void ui_embed_port_prepare_interaction_recursive(lv_obj_t *obj)
{
    if (obj == NULL) return;

    lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; ++i) {
        ui_embed_port_prepare_interaction_recursive(lv_obj_get_child(obj, i));
    }
}

void ui_embed_port_set_parent(lv_obj_t *parent)
{
    s_embed_parent = parent;
    s_active_root = NULL;
}

void ui_embed_port_reset(void)
{
    s_embed_parent = NULL;
    s_active_root = NULL;
}

bool ui_embed_port_is_enabled(void)
{
    return (s_embed_parent != NULL);
}

lv_obj_t *ui_embed_port_create_root(void)
{
    if (s_embed_parent == NULL) {
        return lv_obj_create(NULL);
    }

    lv_obj_t *root = lv_obj_create(s_embed_parent);
    int32_t w = lv_obj_get_width(s_embed_parent);
    int32_t h = lv_obj_get_height(s_embed_parent);
    if (w <= 0) w = 466;
    if (h <= 0) h = 466;
    lv_obj_set_size(root, w, h);
    lv_obj_center(root);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    return root;
}

void ui_embed_port_show(lv_obj_t *root)
{
    if (root == NULL) return;

    if (s_embed_parent == NULL) {
        lv_screen_load_anim(root, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
        return;
    }

    if (s_active_root && s_active_root != root) {
        lv_obj_add_flag(s_active_root, LV_OBJ_FLAG_HIDDEN);
    }

    ui_embed_port_prepare_interaction_recursive(root);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root);
    s_active_root = root;
}

lv_obj_t *ui_embed_port_get_active_root(void)
{
    return s_active_root;
}
