/*******************************************************************************
 * utest-sv.h
 *
 * Surround-view unit-test common definitions
 *
 * Copyright (c) 2015-2016 Cogent Embedded Inc. ALL RIGHTS RESERVED.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *******************************************************************************/

#ifndef __UTEST_SV_H
#define __UTEST_SV_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "utest-common.h"
#include "utest-display.h"
#include "utest-camera.h"
#include "svlib.h"

/*******************************************************************************
 * Forward declarations
 ******************************************************************************/

typedef struct sview_data   sview_data_t;

/*******************************************************************************
 * Local types definitions
 ******************************************************************************/

typedef struct track_desc
{
    /* ...track list item */
    track_list_t        list;

    /* ...track type (0 - offline, 1 - network, 2 - VIN) */
    int                 type;

    /* ...private track-type-specfic data */
    void               *priv;

    /* ...textual description of the track */
    char               *info;
    
    /* ...filename for offline playback - video/CAN data */
    char               *file;
    
    /* ...filename containing point-cloud data */
    char               *cloud;

    /* ...offset between timestamps of video/cloud data */
    u64                 offset;
    
    /* ...set of camera addresses */
    u8                  mac[CAMERAS_NUMBER][6];

}   track_desc_t;

/*******************************************************************************
 * Cameras mapping
 ******************************************************************************/

#define CAMERA_RIGHT                    0
#define CAMERA_LEFT                     1
#define CAMERA_FRONT                    2
#define CAMERA_REAR                     3

/* ...mapping of cameras into texture indices (the order if left/right/front/rear) */
static inline int camera_id(int i)
{
    return (i < 2 ? i ^ 1 : i);
}

static inline int camera_idx(int id)
{
    return (id < 2 ? id ^ 1 : id);
}

/*******************************************************************************
 * Track setup
 ******************************************************************************/

/* ...prepare a runtime to start track playing */
extern int sview_track_start(sview_data_t *sv, track_desc_t *track, int start);

/*******************************************************************************
 * Global configuration options
 ******************************************************************************/

/* ...output devices for main / auxiliary windows */
extern int __output_main, __output_aux;
    
/*******************************************************************************
 * Public module API
 ******************************************************************************/

/* ...opaque type declaration */
typedef struct sview_data sview_data_t;

/* ...surround view application data initialization */
extern sview_data_t * sview_init(display_data_t *display, sview_cfg_t *cfg);

/* ...surround view application data initialization */
extern int sview_camera_init(sview_data_t *sv, camera_init_func_t camera_init);

/* ...main application thread */
extern void * sview_thread(void *arg);

/* ...end-of-stream indication */
extern void sview_eos(sview_data_t *sv);

/* ...ethernet packet reception callback */
extern void sview_packet_receive(sview_data_t *sv, int id, u8 *pdu, u16 len, u64 ts);

/* ...CAN message reception callback */
extern void sview_can_message_receive(sview_data_t *sv, u32 can_id, u8 *msg, u8 dlc, u64 ts);

/*******************************************************************************
 * GUI commands processing
 ******************************************************************************/

/* ...enable spherical projection */
extern void sview_sphere_enable(sview_data_t *sv, int enable);

/* ...auto-adjustment */
extern void sview_auto_adjust(sview_data_t *sv);

/* ...auto-adjustment */
extern void sview_set_view(sview_data_t *sv, int view);

/* ...switch to next track */
extern void sview_next_track(sview_data_t *sv);

/* ...switch to previous track */
extern void sview_prev_track(sview_data_t *sv);

/* ...restart current track */
extern void sview_restart_track(sview_data_t *sv);

/* ...enable surround-view scene showing */
extern void sview_scene_enable(sview_data_t *sb, int enable);

/* ...enable lidar data processing */
extern void sview_lidar_enable(sview_data_t *sv, int enable);

/* ...get lidar enable status */
extern int sview_lidar_enabled(sview_data_t *sv);

/* ...enable debugging output */
extern void sview_debug_enable(sview_data_t *sv, int enable);

/* ...enable debugging output */
extern int sview_debug_enabled(sview_data_t *sv);

/* ...close application */
extern void sview_exit(sview_data_t *sv);

/*******************************************************************************
 * Graphical user interface
 ******************************************************************************/

/* ...GUI initialization function */
extern widget_data_t * gui_create(window_data_t *window, sview_data_t *sv);

/* ...draw GUI layer */
extern void gui_redraw(widget_data_t *widget, cairo_t *cr);

/* ...update GUI configuration */
extern void gui_config_update(widget_data_t *widget);

#endif  /* __UTEST_SV_H */
