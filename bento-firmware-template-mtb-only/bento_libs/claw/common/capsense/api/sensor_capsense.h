/*******************************************************************************
 * sensor_capsense.h — Backward-compatibility umbrella header.
 *
 * Pre-Phase-2 BENTO code includes this header and expects to find the L1
 * data struct (capsense_data_t), L1 constants (CAPSENSE_I2C_ADDR etc.) and
 * the L2 driver API (capsense_init/read/...). Phase 2 split these into
 * separate headers (capsense_protocol.h, capsense_decode.h, capsense_i2c.h).
 *
 * This umbrella keeps `#include "sensor_capsense.h"` working without
 * source-level changes in the 49+ existing consumers (MicroPython binding,
 * UI page, IPC handler).
 *
 * NEW code should include the granular L1 / L2 headers directly.
 ******************************************************************************/
#ifndef SENSOR_CAPSENSE_H
#define SENSOR_CAPSENSE_H

#include "capsense_protocol.h"   /* L1 constants + capsense_frame_t */
#include "capsense_decode.h"     /* L1 capsense_data_t + decoder API */
#include "capsense_i2c.h"        /* L2 driver API (init/read/...) */

/* Legacy alias — historical name kept for compatibility. */
#define CAPSENSE_DATA_SIZE  CAPSENSE_FRAME_BYTES

#endif /* SENSOR_CAPSENSE_H */
