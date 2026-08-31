# page_edge_ai.h

Edge AI hub — one page for ALL compiled-in DEEPCRAFT models. Left: model list (tap to activate). Right: live class scores, inference latency and engine state. ONE page for every model is a hard requirement, not a style choice: PM_MAX_PAGES is 20 and this project already registers 18 pages, so per-model pages would silently fail to register (page_manager.c returns on overflow) and produce dead cards. Compiled only when BENTO_HAS_EDGE_AI=1.

## Functions (exported by the archive)

### `page_edge_ai_create`

```c
lv_obj_t *page_edge_ai_create(void);
```

_No description in the header._

### `page_edge_ai_destroy`

```c
void page_edge_ai_destroy(void);
```

_No description in the header._

### `page_edge_ai_render`

```c
void page_edge_ai_render(sensorhub_snapshot_t *snap);
```

_No description in the header._

## Constants

| Name | Value |
|---|---|
| `PAGE_EDGE_AI_H` | `#include` |
