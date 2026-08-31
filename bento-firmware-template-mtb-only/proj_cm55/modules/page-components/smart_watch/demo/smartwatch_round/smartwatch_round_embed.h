#ifndef SMARTWATCH_ROUND_EMBED_H
#define SMARTWATCH_ROUND_EMBED_H

#include "ipc_sensorhub.h"
#include "lvgl.h"

void smartwatch_round_embed_mount(lv_obj_t *host);
void smartwatch_round_embed_tick(sensorhub_snapshot_t *snap);
void smartwatch_round_embed_unmount(void);
void smartwatch_round_embed_swipe(lv_dir_t dir);

#endif /* SMARTWATCH_ROUND_EMBED_H */
