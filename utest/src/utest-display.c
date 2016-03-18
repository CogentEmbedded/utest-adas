/*******************************************************************************
 * utest-display.c
 *
 * Display support for unit-test application
 *
 * Copyright (c) 2014-2016 Cogent Embedded Inc. ALL RIGHTS RESERVED.
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

#define MODULE_TAG                      DISPLAY

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "utest.h"
#include "utest-common.h"
#include "utest-display.h"
#include "utest-event.h"

#include <cairo-gl.h>

#include <math.h>

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(EVENT, 1);
TRACE_TAG(DEBUG, 1);

/*******************************************************************************
 * Local typedefs
 ******************************************************************************/

/* ...widget data structure */
struct widget_data
{
    /* ...reference to owning window */
    window_data_t              *window;

    /* ...reference to parent widget */
    widget_data_t              *parent;

    /* ...node in a widgets tree ordered somehow */
//    rb_node_t                   node;
//    
//    /* ...nested widgets group */
//    rb_tree_t                   nested;
    
    /* ...pointer to the user-provided widget info */
    widget_info_t              *info;

    /* ...widget client data */
    void                       *cdata;

    /* ...cairo surface associated with this widget */
    cairo_surface_t            *cs;

    /* ...actual widget dimensions */
    int                         left, top, width, height;

    /* ...surface update request */
    int                         dirty;
};

/*******************************************************************************
 * Basic widgets support
 ******************************************************************************/

/* ...create widget */
widget_data_t * widget_create(window_data_t *window, widget_info_t *info, void *cdata) {
    int w = window_get_widget(window)->width;
    int h = window_get_widget(window)->height;
    
    widget_data_t *widget;

    /* ...allocate data handle */
    CHK_ERR(widget = malloc(sizeof (*widget)), (errno = ENOMEM, NULL));

    /* ...initialize widget data */
    if (__widget_init(widget, window, w, h, info, cdata) < 0) {
        TRACE(ERROR, _x("widget initialization error: %m"));
        goto error;
    }
    
    return widget;

error:
    /* ...destroy widget data */
    free(widget);

    return NULL;
}

/* ...widget destructor */
void widget_destroy(widget_data_t *widget) {
    widget_info_t *info = widget->info;

    /* ...invoke custom destructor function as needed */
    (info && info->destroy ? info->destroy(widget, widget->cdata) : 0);

    /* ...destroy cairo surface */
    cairo_surface_destroy(widget->cs);

    /* ...release data handle */
    free(widget);

    TRACE(INIT, _b("widget[%p] destroyed"), widget);
}

/* ...render widget content into given target context */
void widget_render(widget_data_t *widget, cairo_t *cr, float alpha) {
    widget_info_t *info = widget->info;

    /* ...update widget content as needed */
    widget_update(widget, 0);

    /* ...output widget content in current drawing context */
    cairo_save(cr);
    cairo_set_source_surface(cr, widget->cs, info->left, info->top);
    cairo_paint_with_alpha(cr, alpha);
    cairo_restore(cr);
}

/* ...update widget content */
void widget_update(widget_data_t *widget, int flush) {
    cairo_t *cr;

    /* ...do nothing if update is not required */
    TRACE(DEBUG, _b("widget state %d"), widget->dirty);
    if (!widget->dirty) return;

    /* ...clear dirty flag in advance */
    widget->dirty = 0;

    /* ...get curface drawing context */
    cr = cairo_create(widget->cs);

    /* ...update widget content */
    widget->info->draw(widget, widget->cdata, cr);
    
    /* ...make sure context is sane */
    if (TRACE_CFG(DEBUG) && cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        TRACE(ERROR, _x("widget[%p]: bad context: '%s'"), widget, cairo_status_to_string(cairo_status(cr)));
    }

    /* ...destroy context */
    cairo_destroy(cr);

    /* ...force widget surface update */
    (0 && flush ? cairo_surface_flush(widget->cs) : 0);
}

/* ...schedule widget redrawing */
void widget_schedule_redraw(widget_data_t *widget) {
    /* ...mark widget is dirty */
    widget->dirty = 1;

    /* ...schedule redrawing of the parent window */
    window_schedule_redraw(widget->window);
}

/* ...input event processing */
widget_data_t * widget_input_event(widget_data_t *widget, widget_event_t *event) {
    widget_info_t *info = widget->info;

    return (info && info->event ? info->event(widget, widget->cdata, event) : NULL);
}

/* ...return current widget width */
int widget_get_width(widget_data_t *widget) {
    return widget->width;
}

/* ...return current widget height */
int widget_get_height(widget_data_t *widget) {
    return widget->height;
}

/* ...return left point */
int widget_get_left(widget_data_t *widget) {
    return widget->left;
}

/* ...return top point */
int widget_get_top(widget_data_t *widget) {
    return widget->top;
}

/* ...get cairo device associated with widget */
cairo_device_t * widget_get_cairo_device(widget_data_t *widget) {
    return window_get_cairo_device(widget->window);
}

/* ...get parent window root widget */
widget_data_t * widget_get_parent(widget_data_t *widget) {
    return window_get_widget(widget->window);
}

/*******************************************************************************
 * Window API
 ******************************************************************************/

/* ...transformation matrix processing */
static inline void window_set_transform_matrix(window_data_t *window, int *width, int *height, int fullscreen, u32 transform)
{
    cairo_matrix_t     *m = window_get_cmatrix(window);
    int                 w = *width, h = *height;
    
    if (fullscreen && transform)
    {
        switch (transform)
        {
        case 90:
            m->xx = 0.0, m->xy = -1.0, m->x0 = w;
            m->yx = 1.0, m->yy = 0.0, m->y0 = 0;
            //*width = h, *height = w;
            break;
            
        case 180:
            m->xx = -1.0, m->xy = 0.0, m->x0 = w;
            m->yx = 0.0, m->yy = -1.0, m->y0 = h;
            break;

        case 270:
            m->xx = 0.0, m->xy = 1.0, m->x0 = 0;
            m->yx = -1.0, m->yy = 0.0, m->y0 = h;
            //*width = h, *height = w;
            break;

        default:
            BUG(1, _x("invalid transformation: %u"), transform);
        }
    }
    else
    {
        /* ...set identity transformation matrix */
        cairo_matrix_init_identity(m);
    }
}

/* ...get window viewport data */
void window_get_viewport(window_data_t *window, int *w, int *h)
{
    switch(window_get_info(window)->transform)
    {
    case 90:
    case 270:
        /* ...swap vertical and horizontal dimensions */
        *w = window_get_height(window), *h = window_get_width(window);
        break;
        
    case 0:
    case 180:
        *w = window_get_width(window), *h = window_get_height(window);
        break;
        
    default:
        BUG(1, _x("invalid transformation: %u"), window_get_info(window)->transform);
    }
}

/* ...coordinates tranlsation */
void window_translate_coordinates(window_data_t *window, int x, int y, int *X, int *Y)
{
    int     w = window_get_width(window);
    int     h = window_get_height(window);
    
    switch (window_get_info(window)->transform)
    {
    case 0:
        *X = x, *Y = y;
        break;
        
    case 90:
        *X = y, *Y = w - x;
        break;
        
    case 180:
        *X = w - x, *Y = h - y;
        break;

    default:
        *X = w - y, *Y = x;
    }
}

/*******************************************************************************
 * Textures handling
 ******************************************************************************/

/* ...calculate cropping and viewport parameters for a texture */
void texture_set_view(texture_view_t *vcoord, float x0, float y0, float x1, float y1)
{
    float    *p;
    
    /* ...adjust coordinates to GL-space */
    x0 = x0 * 2 - 1, y0 = y0 * 2 - 1;
    x1 = x1 * 2 - 1, y1 = y1 * 2 - 1;

    /* ...fill-in vertex coordinates map */
    p = *vcoord;
    *p++ = x0, *p++ = y0;
    *p++ = x1, *p++ = y0;
    *p++ = x0, *p++ = y1;
    *p++ = x0, *p++ = y1;
    *p++ = x1, *p++ = y0;
    *p++ = x1, *p++ = y1;
}

/* ...transform view-port (rotation and flipping) */
void texture_transform_view(texture_view_t *vcoord, u32 transform)
{
    static const float   tm[4][4] = {
        /* ...normal translation */
        {   1, 0, 0, 1 },

        /* ...rotate by 90 degrees */
        {   0, 1, -1, 0 },

        /* ...rotate by 180 degrees */
        {   -1, 0, 0, -1 },

        /* ...rotate by 270 degrees */
        {   0, -1, 1, 0 },
    };
    
    const float    *m = tm[transform];
    float          *p;
    int             i;

    /* ...multiply coordinates by matrix */
    for (p = *vcoord, i = 0; i < 6; p += 2, i++)
    {
        float   x = p[0], y = p[1];
        
        p[0] = x * m[0] + y * m[1];
        p[1] = x * m[2] + y * m[3];
    }
}

/* ...scale texture to fill particular image area */
void texture_set_view_scale(texture_view_t *vcoord, int x, int y, int w, int h, int W, int H, int width, int height)
{
    float   x0 = (float)x / W, x1 = (float)(x + w) / W;
    float   y0 = (float)y / H, y1 = (float)(y + h) / H;
    int     t0 = height * w;
    int     t1 = width * h;
    int     t = t0 - t1;
    float   f;

    TRACE(0, _b("scale %d*%d : %d*%d"), W, H, width, height);
    
    if (t > 0)
    {
        /* ...texture fills the area vertically */
        f = (0.5 * (x1 - x0) * t) / t0;

        texture_set_view(vcoord, x0 + f, y0, x1 - f, y1);
    }
    else
    {
        /* ...texture fills the window horizontally */
        f = (-0.5 * (y1 - y0) * t) / t1;

        texture_set_view(vcoord, x0, y0 + f, x1, y1 - f);
    }
}

/* ...helper - scaling to window */
void texture_scale_to_window(texture_view_t *vcoord, window_data_t *window, int w, int h, cairo_matrix_t *m)
{
    int     W, H;
    u32     t =  window_get_info(window)->transform / 90;
    
    /* ...get effective window viewable area */
    window_get_viewport(window, &W, &H);    
    
    /* ...scale texture to fit viewable area */
    texture_set_view_scale(vcoord, 0, 0, W, H, W, H, w, h);

    /* ...apply transformation matrix */
    texture_transform_view(vcoord, t);

    /* ...set cairo transformation matrix */
    if (m)
    {
        double   scale;

        if (W * h < H * w)
        {
            /* ...letter-box case */
            scale = (double)W / w, m->x0 = 0, m->y0 = (H - h * scale) / 2;
        }
        else
        {
            /* ...vertical area filled */
            scale = (double)H / h, m->y0 = 0, m->x0 = (W - w * scale) / 2;
        }
        
        /* ...multiply coordinates by matrix */
        m->xx = m->yy = scale, m->xy = m->yx = 0;

        /* ...add target transformation */
        cairo_matrix_multiply(m, m, window_get_cmatrix(window));
    }
}

/* ...set texture cropping data */
void texture_set_crop(texture_crop_t *tcoord, float x0, float y0, float x1, float y1)
{
    float    *p = (float *)tcoord;

    /* ...fill-in texture coordinates */
    *p++ = x0, *p++ = y1;
    *p++ = x1, *p++ = y1;
    *p++ = x0, *p++ = y0;
    *p++ = x0, *p++ = y0;
    *p++ = x1, *p++ = y1;
    *p++ = x1, *p++ = y0;
}



/*******************************************************************************
 * Auxiliary widget helper functions
 ******************************************************************************/

/* ...create GL surface from PNG */
cairo_surface_t * widget_create_png(cairo_device_t *cairo, const char *path, int w, int h)
{
    cairo_surface_t    *image;
    cairo_surface_t    *cs;
    cairo_t            *cr;
    int                 W, H;
    
    /* ...create PNG surface */
    image = cairo_image_surface_create_from_png(path);
    if (__check_surface(image) != 0)
    {
        TRACE(ERROR, _x("failed to create image: %m"));
        return NULL;
    }
    else
    {
        W = cairo_image_surface_get_width(image);
        H = cairo_image_surface_get_height(image);
    }

    /* ...set widget dimensions */
    (w == 0 ? w = W : 0), (h == 0 ? h = H : 0);

    /* ...create new GL surface of requested size */
    cs = cairo_gl_surface_create(cairo, CAIRO_CONTENT_COLOR_ALPHA, w, h);
    if (__check_surface(cs) != 0)
    {
        TRACE(ERROR, _x("failed to create %u*%u GL surface: %m"), w, h);
        cs = NULL;
        goto out;
    }

    /* ...fill GL-surface */
    cr = cairo_create(cs);
    cairo_scale(cr, (double)w / W, (double)h / H);
    cairo_set_source_surface(cr, image, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    TRACE(DEBUG, _b("created GL-surface [%d*%d] from '%s' [%d*%d]"), w, h, path, W, H);

out:
    /* ...release scratch image surface */
    cairo_surface_destroy(image);

    return cs;
}

/* ...check surface status */
int __check_surface(cairo_surface_t *cs) {
    cairo_status_t status;

    switch (status = cairo_surface_status(cs)) {
        case CAIRO_STATUS_SUCCESS: return 0;
        case CAIRO_STATUS_READ_ERROR: errno = EINVAL;
            break;
        case CAIRO_STATUS_FILE_NOT_FOUND: errno = ENOENT;
            break;
        default: errno = ENOMEM;
            break;
    }

    TRACE(ERROR, _b("cairo surface error: '%s'"), cairo_status_to_string(status));

    return -errno;
}

/* ...get surface width */
int widget_image_get_width(cairo_surface_t *cs)
{
    return cairo_gl_surface_get_width(cs);
}

/* ...get surface height */
int widget_image_get_height(cairo_surface_t *cs)
{
    return cairo_gl_surface_get_height(cs);
}
