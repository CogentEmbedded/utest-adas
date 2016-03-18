/*******************************************************************************
 * utest-display.h
 *
 * Display support for unit-test application
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

#ifndef UTEST_DISPLAY_H
#define UTEST_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif
    
    #include <stdint.h>
    #include <sys/types.h>
#ifdef ENABLE_OBJDET
    #include <CL/cl.h>
#endif 
   #include <cairo.h>

    #include "utest-event.h"

/*******************************************************************************
 * Forward declarations
 ******************************************************************************/

typedef struct display_data     display_data_t;
typedef struct window_info      window_info_t;
typedef struct window_data      window_data_t;
typedef struct widget_info      widget_info_t;
typedef struct widget_data      widget_data_t;
typedef struct texture_data     texture_data_t;
typedef struct vbo_data         vbo_data_t;
typedef struct texture_platform texture_platform_t;

    
/*******************************************************************************
 * Window support
 ******************************************************************************/

/* ...window configuration data */
struct window_info
{
    /* ...window title */
    const char         *title;

    /* ...fullscreen mode */
    int                fullscreen;

    /* ...dimensions */
    uint32_t            width, height;

    /* ...output device id */
    uint32_t            output;

    /* ...window transformation */
    uint32_t            transform;

    /* ...context initialization function */
    int               (*init)(display_data_t *, window_data_t *, void *);
    
    /* ...resize hook */
    void              (*resize)(display_data_t *, void *);
    
    /* ...drawing completion callback */
    void              (*redraw)(display_data_t *, void *);
    
    void              (*init_bv)(display_data_t *, void *);

    /* ...custom context destructor */
    void              (*destroy)(window_data_t *, void *);
};

/*******************************************************************************
 * External textures support
 ******************************************************************************/

/* ...external texture data */
struct texture_data
{
    /* ...drawable EGL pixmap */
    texture_platform_t *pdata;
    
    /* ...GL texture index (in shared display EGL context) */
    uint              tex;

    /* ...buffer data pointer (per-plane; up to 3 planes) */
    void               *data[3];

    /* ...buffer plane size */
    uint32_t                 size[3];
};

    /* ...texture cropping data */
    typedef float     texture_crop_t[6 * 2];

    /* ...texture viewport data */
    typedef float     texture_view_t[6 * 2];
#ifdef ENABLE_OBJDET
    /* ...CL configuration data */
    typedef struct cl_data
    {
        /* ...current CL platform */
        cl_platform_id          platform_id;

        /* ...selected CL device */
        cl_device_id            device_id;

        /* ...global computing context */
        cl_context              ctx;

    }   cl_data_t;
#endif
    /* ...connect to a display */
    extern display_data_t * display_create(void);

    /* ...cairo device accessor */
    extern cairo_device_t  * __display_cairo_device(display_data_t *display);
#ifdef ENABLE_OBJDET
    /* ...get current CL configuration data */
    extern cl_data_t * display_cl_data(display_data_t *display);
#endif
    /* ...window creation/destruction */
    extern window_data_t * window_create(display_data_t *display, window_info_t *info, widget_info_t *info2, void *data);
    extern void window_destroy(window_data_t *window);

    /* ...schedule window redrawal */
    extern void window_schedule_redraw(window_data_t *window);
    extern void window_draw(window_data_t *window);

    /* ...bv reinitialize */
    void window_reinit_bv(window_data_t *window);
    
    /* ...window getters... */
    extern int window_get_width(window_data_t *window);
    extern int window_get_height(window_data_t *window);
    extern widget_data_t *window_get_widget(window_data_t *window);
    extern const window_info_t *window_get_info(window_data_t *window);
    extern cairo_t * window_get_cairo(window_data_t *window);
    extern void window_put_cairo(window_data_t *window, cairo_t *cr);
    extern cairo_device_t * window_get_cairo_device(window_data_t *window);
    extern cairo_matrix_t * window_get_cmatrix(window_data_t *window);
   
//    window->fps_acc = 0, window->fps_ts = 0;
//    extern window_set_
    
    /* ...auxiliary helpers */
    extern void window_frame_rate_reset(window_data_t *window);
    extern float window_frame_rate_update(window_data_t *window);

    inline int __check_surface(cairo_surface_t *cs);
    
    /* ...external textures handling */
    extern texture_data_t * texture_create(int w, int h, void **pb, int format);
    extern void texture_destroy(texture_data_t *texture);
    extern void texture_draw(texture_data_t *texture, texture_crop_t *crop, texture_view_t *view, float alpha);
#ifdef ENABLE_OBJDET
    extern cl_mem texture_map(texture_data_t *texture, cl_mem_flags flags);
    extern void texture_unmap(cl_mem buf);
#endif
    /* ...texture viewport/cropping setting */
    extern void texture_set_view(texture_view_t *vcoord, float x0, float y0, float x1, float y1);
    extern void texture_set_crop(texture_crop_t *tcoord, float x0, float y0, float x1, float y1);
    extern void texture_transform_view(texture_view_t *vcoord, u32 transform);
    extern void texture_set_view_scale(texture_view_t *vcoord, int x, int y, int w, int h, int W, int H, int width, int height);

    /* ...helper that scales texture on window with current transformation */
    extern void texture_scale_to_window(texture_view_t *vcoord, window_data_t *window, int w, int h, cairo_matrix_t *m);

/*******************************************************************************
 * Generic widgets support
 ******************************************************************************/



/* ...widget descriptor data */
typedef struct widget_info
{
    /* ...coordinates within parent window/widget */
    int                 left, top, width, height;

    /* ...initialization function */
    int               (*init)(widget_data_t *widget, void *cdata);

    /* ...redraw hook */
    void              (*draw)(widget_data_t *widget, void *cdata, cairo_t *cr);

    /* ...input event processing */
    widget_data_t *   (*event)(widget_data_t *widget, void *cdata, widget_event_t *event);

    /* ...deinitialization function? - need that? */
    void              (*destroy)(widget_data_t *widget, void *cdata);

}   widget_info_t;

/* ...widget creation/destruction */
extern widget_data_t * widget_create(window_data_t *window, widget_info_t *info, void *cdata);
extern void widget_destroy(widget_data_t *widget);

/* Platform-dependent declaration */
int __widget_init(widget_data_t *widget, window_data_t *window, int W, int H, widget_info_t *info, void *cdata);

/* ...widget rendering */
extern void widget_render(widget_data_t *widget, cairo_t *cr, float alpha);
extern void widget_update(widget_data_t *widget, int flush);
extern void widget_schedule_redraw(widget_data_t *widget);
extern cairo_device_t * widget_get_cairo_device(widget_data_t *widget);

/* ...input event processing */
extern widget_data_t * widget_input_event(widget_data_t *widget, widget_event_t *event);
extern widget_data_t * widget_get_parent(widget_data_t *widget);

/* ...helpers */
extern int widget_get_left(widget_data_t *widget);
extern int widget_get_top(widget_data_t *widget);
extern int widget_get_width(widget_data_t *widget);
extern int widget_get_height(widget_data_t *widget);  
extern void window_get_viewport(window_data_t *window, int *w, int *h);
extern void window_translate_coordinates(window_data_t *window, int x, int y, int *X, int *Y);

#ifdef __cplusplus
}
#endif

#endif /* UTEST_DISPLAY_H */

