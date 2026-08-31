# tesaiot_display.h

## Functions (exported by the archive)

### `tesaiot_display_init`

```c
BaseType_t tesaiot_display_init(void);
```

_No description in the header._

### `tesaiot_display_task`

```c
void tesaiot_display_task(void *arg);
```

_No description in the header._

## Structs

### `tesaiot_display_diag_t`

```c
typedef struct {
  uint32_t last_dc_irq_status;
  uint32_t dc_disp0_count;
  uint32_t dc_underflow_count;
  uint32_t dc_bus_error_count;
  uint32_t gpu_recovery_count;
  uint32_t flush_start_count;
  uint32_t flush_ready_count;
  uint32_t flush_timeout_count;
  uint32_t gfx_task_stack_min_words;} tesaiot_display_diag_t;
```

## Constants

| Name | Value |
|---|---|
| `TESAIOT_DISPLAY_H` | `#include` |
| `GPU_INT_PRIORITY` | `(3U)` |
| `DC_INT_PRIORITY` | `(3U)` |
| `I2C_CONTROLLER_IRQ_PRIORITY` | `(2UL)` |
| `APP_BUFFER_COUNT` | `(2U)` |
| `DEFAULT_GPU_CMD_BUFFER_SIZE` | `((64U)` |
| `GPU_TESSELLATION_BUFFER_SIZE` | `((MY_DISP_VER_RES)` |
| `VGLITE_HEAP_SIZE` | `\` |
| `GPU_MEM_BASE` | `(0x0U)` |
| `VG_PARAMS_POS` | `(0UL)` |
| `GFX_TASK_NAME` | `("TESAIoT` |
| `GFX_TASK_STACK_SIZE` | `(configMINIMAL_STACK_SIZE` |
| `GFX_TASK_PRIORITY` | `(configMAX_PRIORITIES` |
