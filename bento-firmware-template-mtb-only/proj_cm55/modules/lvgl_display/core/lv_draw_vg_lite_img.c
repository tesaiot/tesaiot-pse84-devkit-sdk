/*******************************************************************************
 * Upstream provenance and licence — LVGL
 *
 * This file is derived from LVGL v9.5.0, upstream file:
 *     src/draw/vg_lite/lv_draw_vg_lite_img.c
 *     https://github.com/lvgl/lvgl/tree/v9.5.0
 *
 * LVGL is distributed under the MIT licence, which requires its copyright
 * notice and permission notice to be retained in all copies and substantial
 * portions of the software. That notice is reproduced below in full.
 *
 * The Infineon notice that follows this block covers Infineon's modifications
 * to this file. Both notices stand together: neither supersedes the other, and
 * the Infineon terms do not narrow the MIT grant over the LVGL-authored code.
 *
 * ---------------------------------------------------------------------------
 * MIT licence
 * Copyright (c) 2025 LVGL Kft
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 * ---------------------------------------------------------------------------
 *
 * The quotation marks above are ASCII; the upstream file uses typographic
 * quotes. The byte-exact upstream text is shipped as ../LICENSE-LVGL.txt
 ******************************************************************************/
/*******************************************************************************
* File Name        : lv_draw_vg_lite_img.c
*
* Description      : VG-Lite GPU image drawing implementation for LVGL.
*
* Related Document : See README.md
*
*******************************************************************************
 * (c) 2025, Infineon Technologies AG, or an affiliate of Infineon
 * Technologies AG. All rights reserved.
 * This software, associated documentation and materials ("Software") is
 * owned by Infineon Technologies AG or one of its affiliates ("Infineon")
 * and is protected by and subject to worldwide patent protection, worldwide
 * copyright laws, and international treaty provisions. Therefore, you may use
 * this Software only as provided in the license agreement accompanying the
 * software package from which you obtained this Software. If no license
 * agreement applies, then any use, reproduction, modification, translation, or
 * compilation of this Software is prohibited without the express written
 * permission of Infineon.
 *
 * Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
 * IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
 * THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
 * SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
 * Infineon reserves the right to make changes to the Software without notice.
 * You are responsible for properly designing, programming, and testing the
 * functionality and safety of your intended application of the Software, as
 * well as complying with any legal requirements related to its use. Infineon
 * does not guarantee that the Software will be free from intrusion, data theft
 * or loss, or other breaches ("Security Breaches"), and Infineon shall have
 * no liability arising out of any Security Breaches. Unless otherwise
 * explicitly approved by Infineon, the Software may not be used in any
 * application where a failure of the Product or any consequences of the use
 * thereof can reasonably be expected to result in personal injury.
**********************/

/*********************
 *      INCLUDES
 *********************/

#include "lv_draw_vg_lite.h"

#if LV_USE_DRAW_VG_LITE

#include "lv_draw_vg_lite_type.h"
#include "lv_vg_lite_decoder.h"
#include "lv_vg_lite_path.h"
#include "lv_vg_lite_pending.h"
#include "lv_vg_lite_utils.h"
#include "../../misc/lv_area_private.h"
#include "../lv_image_decoder_private.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static inline bool matrix_has_transform(const vg_lite_matrix_t * matrix);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void lv_draw_vg_lite_img(lv_draw_task_t * t, const lv_draw_image_dsc_t * dsc,
                         const lv_area_t * coords, bool no_cache)
{
    lv_draw_vg_lite_unit_t * u = (lv_draw_vg_lite_unit_t *)t->draw_unit;

    lv_area_t clip_area;
    if(!lv_area_intersect(&clip_area, &t->_real_area, &t->clip_area)) {
        /*Fully clipped, nothing to do*/
        return;
    }

    LV_PROFILER_DRAW_BEGIN;

    vg_lite_buffer_t src_buf;
    lv_image_decoder_dsc_t decoder_dsc;

    /* if not support blend normal, premultiply alpha */
    bool premultiply = !lv_vg_lite_support_blend_normal();
    if(!lv_vg_lite_buffer_open_image(&src_buf, &decoder_dsc, dsc->src, no_cache, premultiply)) {
        LV_PROFILER_DRAW_END;
        return;
    }

    vg_lite_color_t color = lv_vg_lite_image_recolor(&src_buf, dsc);

    /* convert the blend mode to vg-lite blend mode, considering the premultiplied alpha */
    bool has_pre_mul = lv_draw_buf_has_flag(decoder_dsc.decoded, LV_IMAGE_FLAGS_PREMULTIPLIED)
                       || (decoder_dsc.decoded->header.cf == LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED);
    vg_lite_blend_t blend = lv_vg_lite_blend_mode(dsc->blend_mode, has_pre_mul);

    /* original image matrix */
    vg_lite_matrix_t image_matrix;
    vg_lite_identity(&image_matrix);
    lv_vg_lite_image_matrix(&image_matrix, coords->x1, coords->y1, dsc);

    /* image drawing matrix */
    vg_lite_matrix_t matrix = u->global_matrix;
    lv_vg_lite_matrix_multiply(&matrix, &image_matrix);

    const bool has_transform = matrix_has_transform(&matrix);
    const vg_lite_filter_t filter = has_transform ?  VG_LITE_FILTER_BI_LINEAR : VG_LITE_FILTER_POINT;

    /* Use coords as the fallback image width and height */
    const uint32_t img_w = dsc->header.w ? dsc->header.w : lv_area_get_width(coords);
    const uint32_t img_h = dsc->header.h ? dsc->header.h : lv_area_get_height(coords);

    if(dsc->colorkey) {
        lv_vg_lite_set_color_key(dsc->colorkey);
    }

    /* If clipping is not required, blit directly */
    if(lv_area_is_in(&t->_real_area, &t->clip_area, false) && dsc->clip_radius <= 0 && !dsc->tile) {
        /* rect is used to crop the pixel-aligned padding area */
        vg_lite_rectangle_t rect = {
            .x = 0,
            .y = 0,
            .width = img_w,
            .height = img_h,
        };

        lv_vg_lite_blit_rect(
            &u->target_buffer,
            &src_buf,
            &rect,
            &matrix,
            blend,
            color,
            filter);

        lv_vg_lite_pending_add(u->image_dsc_pending, &decoder_dsc);
        LV_PROFILER_DRAW_END;
        return;
    }


    lv_vg_lite_path_t * path = lv_vg_lite_path_get(u, VG_LITE_FP32);

    if(dsc->tile) {
        /* When the image is tiled, use coords as the tile area and create a path around it */
        lv_vg_lite_path_append_rect(
            path,
            coords->x1, coords->y1,
            lv_area_get_width(coords), lv_area_get_height(coords),
            dsc->clip_radius);
    }
    else if(has_transform || dsc->clip_radius) {
        /**
         * When the image is transformed or rounded, create a path around
         * the image and follow the image_matrix for coordinate transformation
         */
        lv_vg_lite_path_set_transform(path, &image_matrix);

        /* Each point will be transformed accordingly. */
        lv_vg_lite_path_append_rect(
            path,
            dsc->image_area.x1 - coords->x1, dsc->image_area.y1 - coords->y1,
            lv_area_get_width(&dsc->image_area), lv_area_get_height(&dsc->image_area),
            dsc->clip_radius);
    }
    else {
        /* append normal rect to the path */
        lv_vg_lite_path_append_rect(
            path,
            clip_area.x1, clip_area.y1,
            lv_area_get_width(&clip_area), lv_area_get_height(&clip_area),
            0);
    }

    lv_vg_lite_path_set_bounding_box_area(path, &clip_area);
    lv_vg_lite_path_end(path);

    vg_lite_matrix_t path_matrix = u->global_matrix;

    if(dsc->tile) {
        lv_area_t tile_area;
        if(lv_area_get_width(&dsc->image_area) >= 0) {
            tile_area = dsc->image_area;
        }
        else {
            tile_area = *coords;
        }
        lv_area_set_width(&tile_area, img_w);
        lv_area_set_height(&tile_area, img_h);

        /**
         * vg_lite_tvg does not support VG_LITE_PATTERN_REPEAT,
         * use looping texture for simulation.
         */
#if LV_USE_VG_LITE_THORVG
        const int32_t tile_x_start = tile_area.x1;
        while(tile_area.y1 <= coords->y2) {
            while(tile_area.x1 <= coords->x2) {
#endif
                vg_lite_matrix_t tile_matrix;
                vg_lite_identity(&tile_matrix);
                vg_lite_translate(tile_area.x1 - coords->x1, tile_area.y1 - coords->y1, &tile_matrix);

                vg_lite_matrix_t pattern_matrix = matrix;
                lv_vg_lite_matrix_multiply(&pattern_matrix, &tile_matrix);

                lv_area_t clipped_img_area;
                if(!LV_USE_VG_LITE_THORVG || lv_area_intersect(&clipped_img_area, &tile_area, coords)) {
                    lv_vg_lite_draw_pattern(
                        &u->target_buffer,
                        lv_vg_lite_path_get_path(path),
                        VG_LITE_FILL_EVEN_ODD,
                        &path_matrix,
                        &src_buf,
                        &pattern_matrix,
                        blend,
                        VG_LITE_PATTERN_REPEAT,
                        0,
                        color,
                        filter);
                }

#if LV_USE_VG_LITE_THORVG
                tile_area.x1 += img_w;
                tile_area.x2 += img_w;
            }

            tile_area.y1 += img_h;
            tile_area.y2 += img_h;
            tile_area.x1 = tile_x_start;
            tile_area.x2 = tile_x_start + img_w - 1;
        }
#endif
    }
    else {
        lv_vg_lite_draw_pattern(
            &u->target_buffer,
            lv_vg_lite_path_get_path(path),
            VG_LITE_FILL_EVEN_ODD,
            &path_matrix,
            &src_buf,
            &matrix,
            blend,
            VG_LITE_PATTERN_COLOR,
            0,
            color,
            filter);
    }

    if(dsc->colorkey) {
        lv_vg_lite_set_color_key(NULL);
    }

    lv_vg_lite_path_drop(u, path);

    lv_vg_lite_pending_add(u->image_dsc_pending, &decoder_dsc);
    LV_PROFILER_DRAW_END;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static inline bool matrix_has_transform(const vg_lite_matrix_t * matrix)
{
    /**
     * When the rotation angle is 0 or 180 degrees,
     * it is considered that there is no transformation.
     */
    return !((matrix->m[0][0] == 1.0f || matrix->m[0][0] == -1.0f) &&
             matrix->m[0][1] == 0.0f &&
             matrix->m[1][0] == 0.0f &&
             (matrix->m[1][1] == 1.0f || matrix->m[1][1] == -1.0f) &&
             matrix->m[2][0] == 0.0f &&
             matrix->m[2][1] == 0.0f &&
             matrix->m[2][2] == 1.0f);
}

#endif /*LV_USE_DRAW_VG_LITE*/

/* [] END OF FILE */
