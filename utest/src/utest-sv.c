/*******************************************************************************
 * utest-app.c
 *
 * Combined surround view / object-detection unit-test
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

#define MODULE_TAG                      APP

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "utest.h"
#include "utest-app.h"
#include "utest-vsink.h"

/* ...system headers */
#include <sys/poll.h>
#include <linux/input.h>

#include <math.h>

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(DEBUG, 1);
    
/*******************************************************************************
 * Local types definitions
 ******************************************************************************/




/* ...debugging helper */
static inline void gl_dump_state(void);

/*******************************************************************************
 * Render queue access helpers
 ******************************************************************************/

/* ...pop buffers from a render queue */
static inline int sview_pop_buffers(app_data_t *app, GstBuffer **buf, texture_data_t **tex, GLuint *t, void **planes, s64 *ts)
{
    int     i;
    int     ready;
    
    /* ...lock access to internal data */
    pthread_mutex_lock(&app->lock);

    /* ...check for a termination request */
    if (app->flags & APP_FLAG_EOS)
    {
        /* ...drop all buffers */
        for (i = 0; i < CAMERAS_NUMBER; i++)
        {
            while (!g_queue_is_empty(&app->render[i]))
            {
                gst_buffer_unref(g_queue_pop_head(&app->render[i]));
            }
        }

        TRACE(DEBUG, _b("purged rendering queue"));
        
        /* ...mark we have no buffers to draw */
        ready = 0;
    }
    else if ((app->frames & ((1 << CAMERAS_NUMBER) - 1)) == 0)
    {
        s64     ts_acc = 0;
        
        /* ...collect the textures corresponding to the cameras */
        for (i = 0; i < CAMERAS_NUMBER; i++)
        {
            GQueue         *queue = &app->render[i];
            GstBuffer      *buffer;
            vsink_meta_t   *meta;
            texture_data_t *texture;
            
            /* ...buffer must be available */
            BUG(g_queue_is_empty(queue), _x("inconsistent state of camera-%d"), i);
            
            /* ...retrieve last (most actual) buffer */
            buf[i] = buffer = g_queue_peek_tail(queue);
            meta = gst_buffer_get_vsink_meta(buffer);
            tex[i] = texture = meta->priv;
            t[i] = texture->tex;
            planes[i] = texture->data[0];

            /* ...update timestamp accumulator */
            ts_acc += GST_BUFFER_DTS(buffer);

            /* ...drop all "previous" buffers */
            while (g_queue_peek_head(queue) != buffer)
            {
                gst_buffer_unref(g_queue_pop_head(queue));
            }
        }
        
        /* ...update accumulator */
        *ts = ts_acc / CAMERAS_NUMBER;

        /* ...return buffer readiness indication */
        ready = 1;
    }
    else
    {
        /* ...buffers not ready */
        ready = 0;
    }

    pthread_mutex_unlock(&app->lock);

    return ready;
}

/* ...release buffer set */
static inline void sview_release_buffers(app_data_t *app, GstBuffer **buffers)
{
    int     i;

    pthread_mutex_lock(&app->lock);
    
    /* ...drop the buffers - they are heads of the rendering queues */
    for (i = 0; i < CAMERAS_NUMBER; i++)
    {
        GQueue     *queue = &app->render[i];
        GstBuffer  *buffer;

        /* ...queue cannot be empty */
        BUG(g_queue_is_empty(queue), _x("inconsistent state of camera-%d"), i);

        /* ...remove head of the queue */
        buffer = g_queue_pop_head(queue);
        
        /* ...buffer must be at the head of the queue */
        BUG(buffers[i] != buffer, _x("invalid queue head: %p != %p"), buffers[i], buffer);
        
        /* ...return buffer to a pool */
        gst_buffer_unref(buffer);

        /* ...check if queue gets empty */
        (g_queue_is_empty(queue) ? app->frames ^= 1 << i : 0);
    }

    pthread_mutex_unlock(&app->lock);
}

/* ...purge render queues */
static inline void sview_purge_buffers(app_data_t *app)
{
    int     i;

    pthread_mutex_lock(&app->lock);

    for (i = 0; i < CAMERAS_NUMBER; i++)
    {
        while (!g_queue_is_empty(&app->render[i]))
        {
            gst_buffer_unref(g_queue_pop_head(&app->render[i]));
        }
    }

    pthread_mutex_unlock(&app->lock);
}

/*******************************************************************************
 * Interface exposed to the camera backend
 ******************************************************************************/

/* ...deallocate texture data */
static void __destroy_sv_texture(gpointer data, GstMiniObject *obj)
{
    GstBuffer      *buffer = (GstBuffer *)obj;
    vsink_meta_t   *meta = gst_buffer_get_vsink_meta(buffer);

    TRACE(DEBUG, _b("destroy texture referenced by meta: %p:%p"), meta, meta->priv);
    
    /* ...destroy texture */
    texture_destroy(meta->priv);
}

/* ...input buffer allocation from surround-view camera set */
static int sview_input_alloc(void *data, GstBuffer *buffer)
{
    app_data_t         *app = data;
    vsink_meta_t       *vmeta = gst_buffer_get_vsink_meta(buffer);
    int                 w = vmeta->width, h = vmeta->height;

    /* ...make sure input buffer dimensions are valid */
    CHK_ERR(w == 1280 && h == 800, -EINVAL);

    /* ...allocate texture to wrap the buffer */
    CHK_ERR(vmeta->priv = texture_create(w, h, vmeta->plane, vmeta->format), -errno);

    /* ...add custom destructor to the buffer */
    gst_mini_object_weak_ref(GST_MINI_OBJECT(buffer), __destroy_sv_texture, app);

    TRACE(INFO, _b("input buffer %p allocated"), buffer);

    return 0;
}

/* ...process new input buffer submitted from camera */
static int sview_input_process(void *data, int i, GstBuffer *buffer)
{
    app_data_t     *app = data;

    BUG(i >= CAMERAS_NUMBER, _x("invalid camera index: %d"), i);

    TRACE(DEBUG, _b("camera-%d: input buffer received"), i);

    /* ...get queue access lock */
    pthread_mutex_lock(&app->lock);

    /* ...check if playback is enabled */
    if ((app->flags & APP_FLAG_EOS) == 0)
    {
        /* ...place buffer into main rendering queue (take ownership) */
        g_queue_push_tail(&app->render[i], buffer), gst_buffer_ref(buffer);

        /* ...indicate buffer is available */
        app->frames &= ~(1 << i);
        
        /* ...schedule processing if all buffers are ready */
        if ((app->frames & ((1 << CAMERAS_NUMBER) - 1)) == 0)
        {
            /* ...all buffers available; trigger surround-view scene processing */
            window_schedule_redraw(app->window);
        }
    }

    /* ...release queue access lock */
    pthread_mutex_unlock(&app->lock);

    return 0;
}

/* ...callbacks for surround view camera set back-end */
static const camera_callback_t sv_camera_cb = {
    .allocate = sview_input_alloc,
    .process = sview_input_process,
};

/*******************************************************************************
 * Front camera interface
 ******************************************************************************/
#ifdef ENABLE_OBJDET

/* ...retrieve buffer from front-camera render queue */
static inline GstBuffer * objdet_pop_buffer(app_data_t *app)
{
    GQueue     *queue = &app->render[CAMERAS_NUMBER];
    GstBuffer  *buffer;
    
    /* ...lock internal data access */
    pthread_mutex_lock(&app->lock);

    if (app->flags & APP_FLAG_EOS)
    {
        /* ...drop all buffers */
        while (!g_queue_is_empty(queue))
        {
            gst_buffer_unref(g_queue_pop_head(queue));
        }

        /* ...destroy engine data if not already */
        pthread_mutex_lock(&app->access);
        (app->od ? objdet_engine_close(app->od), app->od = NULL : 0);
        pthread_mutex_unlock(&app->access);

        /* ...indicate we have no buffer to output */
        buffer = NULL;

        TRACE(DEBUG, _b("render-queue purged"));
    }
    else
    {
        /* ...get buffer from a head of render queue */
        buffer = (g_queue_is_empty(queue) ? NULL : g_queue_pop_head(queue));
    }

    /* ...unlock internal data */
    pthread_mutex_unlock(&app->lock);
    
    return buffer;
}

/* ...push buffer into object-detection library */
static void __objdet_buffer_ready(void *cdata, void *cookie, road_scene_t *scene)
{
    app_data_t     *app = cdata;
    GstBuffer      *buffer = cookie;
    GQueue         *queue = &app->render[CAMERAS_NUMBER];
    
    TRACE(DEBUG, _b("buffer returned from engine: %p"), buffer);

    /* ...get internal data lock */
    pthread_mutex_lock(&app->lock);

    /* ...check if visualization is still enabled */
    if ((app->flags & APP_FLAG_EOS) == 0)
    {
        /* ...submit buffer to a rendering queue (take the ownership) */
        g_queue_push_tail(queue, buffer), gst_buffer_ref(buffer);

        /* ...schedule rendering operation */
        window_schedule_redraw(app->window);
    }

    /* ...release internal data access lock */
    pthread_mutex_unlock(&app->lock);
}

/* ...buffer completion hook (tbd - need that at all?) */
static void __objdet_buffer_release(void *cdata, void *cookie)
{
    GstBuffer      *buffer = cookie;

    TRACE(DEBUG, _b("buffer %p released by engine (count=%d)"), buffer, GST_MINI_OBJECT_REFCOUNT(buffer));

    /* ...release buffer handle */
    gst_buffer_unref(buffer);
}

/* ...error processing hook (tbd - need that at all?) */
static void __objdet_buffer_error(void *cdata, int error)
{
    TRACE(INFO, _b("objdet-engine reported error: %d"), error);
}

/* ...object-detection engine callback */
static objdet_callback_t objdet_callback = {
    .ready = __objdet_buffer_ready,
    .release = __objdet_buffer_release,
    .error = __objdet_buffer_error,
};

/* ...deallocate texture data */
static void __destroy_od_texture(gpointer data, GstMiniObject *obj)
{
    GstBuffer      *buffer = (GstBuffer *)obj;
    vsink_meta_t   *vmeta = gst_buffer_get_vsink_meta(buffer);
    objdet_meta_t  *ometa = gst_buffer_get_objdet_meta(buffer);

    TRACE(DEBUG, _b("destroy texture referenced by meta: %p:%p"), vmeta, vmeta->priv);

    /* ...destroy memory mapping */
    texture_unmap(ometa->buf);

    /* ...destroy texture */
    texture_destroy(vmeta->priv);
}

/* ...buffer allocation from frontal camera (object detection engine) */
static int objdet_input_alloc(void *data, GstBuffer *buffer)
{
    app_data_t         *app = data;
    vsink_meta_t       *vmeta = gst_buffer_get_vsink_meta(buffer);
    int                 w = vmeta->width, h = vmeta->height;
    objdet_meta_t      *ometa;

    if (app->f_width)
    {
        /* ...verify buffer dimensions are valid */
        CHK_ERR(w == app->f_width && h == app->f_height, -EINVAL);
    }
    else
    {
        int     W = window_get_width(app->window);
        int     H = window_get_height(app->window);

        /* ...check dimensions are valid */
        CHK_ERR(w && h, -EINVAL);

        /* ...set buffer dimensions */
        app->f_width = w, app->f_height = h;

        /* ...initialize object detection engine */
        CHK_ERR(app->od = objdet_engine_init(&objdet_callback, app, w, h, __pixfmt_yuv_bpp(vmeta->format), app->od_cfg), -errno);

        /* ...create a viewport for data visualization */
        texture_scale_to_window(&app->view, app->window, w, h, &app->matrix);
        
        //texture_set_view_scale(&app->view, 0, 0, W, H, W, H, w, h);


        /* ...create transformation matrix */
        if (0)
        {
            cairo_matrix_t     *m = &app->matrix;
            
            m->xx = (double) W / w, m->xy = 0, m->x0 = 0;
            m->yx = 0, m->yy = (double) H / h, m->y0 = 0;
        }        
    }

    /* ...allocate texture to wrap the buffer */
    CHK_ERR(vmeta->priv = texture_create(w, h, vmeta->plane, vmeta->format), -errno);

    /* ...add custom buffer metadata */
    CHK_ERR(ometa = gst_buffer_add_objdet_meta(buffer), -(errno = ENOMEM));
    CHK_ERR(ometa->buf = texture_map(vmeta->priv, CL_MEM_READ_ONLY), -errno);
    GST_META_FLAG_SET(ometa, GST_META_FLAG_POOLED);

    /* ...add custom destructor to the buffer */
    gst_mini_object_weak_ref(GST_MINI_OBJECT(buffer), __destroy_od_texture, app);

    TRACE(INFO, _b("front-camera input buffer %p allocated (%p)"), buffer, ometa->buf);

    return 0;
}

/* ...process new input buffer submitted from camera */
static int objdet_input_process(void *data, int i, GstBuffer *buffer)
{
    app_data_t         *app = data;

    TRACE(DEBUG, _b("front-camera: input buffer received (%p, count=%d)"), buffer, GST_MINI_OBJECT_REFCOUNT(buffer));

    /* ...get internal data access lock */
    pthread_mutex_lock(&app->lock);

    /* ...bail out if playback is stopped */
    if ((app->flags & APP_FLAG_EOS) == 0)
    {
        vsink_meta_t       *vmeta = gst_buffer_get_vsink_meta(buffer);
        objdet_meta_t      *ometa = gst_buffer_get_objdet_meta(buffer);

        /* ...attach current vehicle status info to the buffer */
        memcpy(&ometa->info, &app->vehicle, sizeof(vehicle_info_t));

        /* ...release internal data access lock */
        pthread_mutex_unlock(&app->lock);

        /* ...serialize access to the object-detection engine */
        pthread_mutex_lock(&app->access);

        /* ...submit buffer to a processing engine */
        if (objdet_engine_push_buffer(app->od, buffer, vmeta->plane[0], &ometa->info, &ometa->scene, ometa->buf) < 0)
        {
            /* ...silently ignore the error */
            TRACE(ERROR, _x("failed to submit a buffer to detection engine: %m"));
        }
        else
        {
            /* ...buffer submitted successfully; take ownership */
            gst_buffer_ref(buffer);
        }

        /* ...release object-detection engine access lock */
        pthread_mutex_unlock(&app->access);
    }
    else
    {
        /* ...object detection visualization is disabled */
        pthread_mutex_unlock(&app->lock);
    }

    return 0;
}

/* ...callbacks for surround view camera set back-end */
static const camera_callback_t objdet_camera_cb = {
    .allocate = objdet_input_alloc,
    .process = objdet_input_process,
};

/* ...redraw main application window */
static void objdet_redraw(display_data_t *display, void *data)
{
    app_data_t     *app = data;
    window_data_t  *window = app->window;
    GstBuffer      *buffer;

    /* ...retrieve pending buffer from rendering queue */
    while ((buffer = objdet_pop_buffer(app)) != NULL)
    {
        vsink_meta_t       *vmeta = gst_buffer_get_vsink_meta(buffer);
        objdet_meta_t      *ometa = gst_buffer_get_objdet_meta(buffer);
        texture_data_t     *texture = vmeta->priv;
        float               fps = window_frame_rate_update(window);
        cairo_t            *cr;

        /* ...add some performance monitors here - tbd */
        TRACE(INFO, _b("redraw frame: %u"), app->frame_num++);

		/* ...clear buffer before drawing anything */
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClearDepthf(1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        /* ...output buffer on screen */
        texture_draw(texture, &app->view, NULL, 1.0);

        /* ...get cairo drawing context */
        cr = window_get_cairo(window);

        /* ...set cairo transformation matrix for a given view-port - tbd */
        cairo_save(cr);
        
        /* ...apply visualization matrix */
        cairo_set_matrix(cr, &app->matrix);

        /* ...visualize detection data as required */
        objdet_engine_ldw_draw(app->od, &ometa->scene, cr);

        /* ...restore transformation matrix */
        cairo_restore(cr);

        /* ...output current vehicle status? - use unprocessed frame, maybe... */
        if (app->flags & APP_FLAG_DEBUG)
        {
            /* ...output frame-rate in the upper-left corner */
            cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
            cairo_move_to(cr, 40, 80);
            draw_string(cr, "%.1f FPS", fps);
        }
        else
        {
            TRACE(DEBUG, _b("fps: %.2f"), fps);
        }

        /* ...output GUI graphics as needed */
        gui_redraw(app->gui, cr);

        /* ...release cairo drawing context */
        window_put_cairo(window, cr);

        /* ...submit window to composer */
        window_draw(window);

        /* ...return buffer to the pool */
        gst_buffer_unref(buffer);
    }

    TRACE(DEBUG, _b("frontal camera drawing complete.."));
}

/* ...set frontal camera interface */
int objdet_camera_init(app_data_t *app, camera_init_func_t camera_init)
{
    GstElement     *bin;

    /* ...clear input stream dimensions (force engine reinitialization) */
    app->f_width = app->f_height = 0;

    /* ...create camera interface (it may be network camera or file on disk) */
    CHK_ERR(bin = camera_init(&objdet_camera_cb, app), -errno);

    /* ...add camera to a pipe */
    gst_bin_add(GST_BIN(app->pipe), bin);

    /* ...synchronize state with a parent */
    gst_element_sync_state_with_parent(bin);

    /* ...save camera-set container */
    app->fr_camera = bin;

    TRACE(INIT, _b("frontal camera initialized"));

    return 0;
}

/* ...start object-detection track */
static track_desc_t * __app_objdet_track(app_data_t *app)
{
    track_desc_t *track;

    if (app->flags & APP_FLAG_LIVE)
    {
        track = objdet_track_live();
    }
    else if (app->flags & APP_FLAG_NEXT)
    {
        track = objdet_track_next();
    }
    else if (app->flags & APP_FLAG_PREV)
    {
        track = objdet_track_prev();
    }
    else
    {
        track = objdet_track_current();
    }
    
    BUG(!track, _x("invalid state"));
    
    /* ...select detector configuration */
    app->od_cfg = track->od_cfg;

    /* ...set global camera configuration parameters */
    ldwConfigurationFilePath = track->camera_cfg;

    /* ...set window rendering hook */
    app_main_info.redraw = objdet_redraw;

    return track;
}

#endif

/*******************************************************************************
 * Rendering functions
 ******************************************************************************/

/* ...draw multiline string - hmm; need to put that stuff into display */
static inline void draw_string(cairo_t *cr, const char *fmt, ...)
{
	char                    buffer[4096], *p, *end;
	cairo_text_extents_t    text_extents;
	cairo_font_extents_t    font_extents;
	va_list                 argp;

	cairo_save(cr);
	cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 40);
	cairo_font_extents(cr, &font_extents);

	va_start(argp, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, argp);
	va_end(argp);

    for (p = buffer; *p; p = end + 1)
    {
        /* ...output single line */
		((end = strchr(p, '\n')) != NULL ? *end = 0 : 0);
		cairo_show_text(cr, p);
		cairo_text_extents (cr, p, &text_extents);
		cairo_rel_move_to (cr, -text_extents.x_advance, font_extents.height);
        TRACE(0, _b("print text-line: <%f,%f>"), text_extents.x_advance, font_extents.height);

        /* ...stop when last line processes */
        if (!end)   break;
    }

    /* ...restore drawing context */
	cairo_restore(cr);
}

/* ...surround-view scene rendering */
static void sview_redraw(display_data_t *display, void *data)
{
    app_data_t         *app = data;
    window_data_t      *window = app->window;
    int                 W = window_get_width(window);
    int                 H = window_get_height(window);
    GstBuffer          *buffers[CAMERAS_NUMBER];
    texture_data_t     *texture[CAMERAS_NUMBER];
    GLuint              tex[CAMERAS_NUMBER];
    void               *planes[CAMERAS_NUMBER];
    s64                 ts;
    

    /* ...try to get buffers */
    while(sview_pop_buffers(app, buffers, texture, tex, planes, &ts))
    {
        float       fps = window_frame_rate_update(window);
        cairo_t    *cr;
        
        /* ...ready for rendering; clear surface content */
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClearDepthf(1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        /* ...specify a GL-viewport for an image */
        if (0) glViewport(0, 0, W, H);

        /* ...draw graphics on top of the scene */
        cr = window_get_cairo(window);

        /* ...generate a single scene; acquire engine access lock */
        pthread_mutex_lock(&app->access);
        
        sview_engine_process(app->sv, tex, planes, cr, ts);
        
        pthread_mutex_unlock(&app->access);

        /* ...output frame-rate in the upper-left corner */
        if(app->flags & APP_FLAG_DEBUG)
        {
            cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
            cairo_move_to(cr, 40, 80);
            draw_string(cr, "%.1f FPS", fps);
        }
        else
        {
            TRACE(DEBUG, _b("main-window fps: %.1f"), fps);
        }
        
        /* ...output GUI graphics as needed */
        gui_redraw(app->gui, cr);
        
        /* ...release cairo interface */
        window_put_cairo(window, cr);

        /* ...submit window to a compositor */
        window_draw(window);

        /* ...release buffers collected */
        sview_release_buffers(app, buffers);
    }

    TRACE(DEBUG, _b("surround-view drawing complete"));
}

static void sview_init_bv(display_data_t *display, void *data)
{
    app_data_t         *app = data;
    /* ...generate a single scene; acquire engine access lock */
    pthread_mutex_lock(&app->access);
    app->sv = sview_bv_reinit(app->sv, app->sv_cfg, 1280, 800);
    pthread_mutex_unlock(&app->access);
}

/*******************************************************************************
 * Runtime initialization
 ******************************************************************************/

static inline void gl_dump_state(void)
{
    GLint       iv[4];
    GLfloat     fv[4];
    GLboolean   bv[4];
    
    glGetIntegerv(GL_DEPTH_BITS, iv);
    TRACE(1, _b("depth-bits: %u"), iv[0]);
    glGetFloatv(GL_DEPTH_CLEAR_VALUE, fv);
    TRACE(1, _b("depth-clear-value: %f"), fv[0]);
    glGetIntegerv(GL_DEPTH_FUNC, iv);
    TRACE(1, _b("depth-func: %u"), iv[0]);
    glGetFloatv(GL_DEPTH_RANGE, fv);
    TRACE(1, _b("depth-range: %f/%f"), fv[0], fv[1]);
    glGetBooleanv(GL_DEPTH_TEST, bv);
    TRACE(1, _b("GL_DEPTH_TEST: %d"), bv[0]);
    glGetBooleanv(GL_DEPTH_WRITEMASK, bv);
    TRACE(1, _b("GL_DEPTH_WRITEMASK: %d"), bv[0]);
    glGetIntegerv(GL_ACTIVE_TEXTURE, iv);
    TRACE(1, _b("GL_ACTIVE_TEXTURE: %X"), iv[0]);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, iv);
    TRACE(1, _b("GL_ELEMENT_ARRAY_BUFFER_BINDING: %d"), iv[0]);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, iv);
    TRACE(1, _b("GL_FRAMEBUFFER_BINDING: %d"), iv[0]);
    glGetIntegerv(GL_STENCIL_BACK_FAIL, iv);
    TRACE(1, _b("GL_STENCIL_BACK_FAIL: %X"), iv[0]);
    glGetIntegerv(GL_STENCIL_BACK_FUNC, iv);
    TRACE(1, _b("GL_STENCIL_BACK_FUNC: %X"), iv[0]);
    glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_FAIL, iv);
    TRACE(1, _b("GL_STENCIL_BACK_PASS_DEPTH_FAIL: %X"), iv[0]);
    glGetIntegerv(GL_STENCIL_BACK_PASS_DEPTH_PASS, iv);
    TRACE(1, _b("GL_STENCIL_BACK_PASS_DEPTH_PASS: %X"), iv[0]);
    glGetIntegerv(GL_STENCIL_BACK_REF, iv);
    TRACE(1, _b("GL_STENCIL_BACK_REF: %u"), iv[0]);
    glGetIntegerv(GL_STENCIL_BACK_VALUE_MASK, iv);
    TRACE(1, _b("GL_STENCIL_BACK_VALUE_MASK: %u"), iv[0]);
    glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, iv);
    TRACE(1, _b("GL_STENCIL_BACK_WRITEMASK: %u"), iv[0]);
    glGetIntegerv(GL_STENCIL_BITS, iv);
    TRACE(1, _b("GL_STENCIL_BITS: %u"), iv[0]);
    glGetIntegerv(GL_STENCIL_CLEAR_VALUE, iv);
    TRACE(1, _b("GL_STENCIL_CLEAR_VALUE: %u"), iv[0]);
    glGetIntegerv(GL_STENCIL_FAIL, iv);
    TRACE(1, _b("GL_STENCIL_FAIL: %X"), iv[0]);
    glGetIntegerv(GL_STENCIL_FUNC, iv);
    TRACE(1, _b("GL_STENCIL_FUNC: %X"), iv[0]);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, iv);
    TRACE(1, _b("GL_STENCIL_PASS_DEPTH_FAIL: %X"), iv[0]);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, iv);
    TRACE(1, _b("GL_STENCIL_PASS_DEPTH_PASS: %X"), iv[0]);
    glGetIntegerv(GL_STENCIL_REF, iv);
    TRACE(1, _b("GL_STENCIL_REF: %u"), iv[0]);
    glGetBooleanv(GL_STENCIL_REF, bv);
    TRACE(1, _b("GL_STENCIL_REF: %d"), bv[0]);
    glGetIntegerv(GL_STENCIL_VALUE_MASK, iv);
    TRACE(1, _b("GL_STENCIL_VALUE_MASK: %u"), iv[0]);
    glGetIntegerv(GL_STENCIL_WRITEMASK, iv);
    TRACE(1, _b("GL_STENCIL_WRITEMASK: %u"), iv[0]);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, iv);
    TRACE(1, _b("GL_TEXTURE_BINDING_2D: %u"), iv[0]);
    glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, iv);
    TRACE(1, _b("GL_TEXTURE_BINDING_CUBE_MAP: %u"), iv[0]);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, iv);
    TRACE(1, _b("GL_UNPACK_ALIGNMENT: %u"), iv[0]);
    glGetIntegerv(GL_VIEWPORT, iv);
    TRACE(1, _b("GL_VIEWPORT: %u/%u/%u/%u"), iv[0], iv[1], iv[2], iv[3]);
    glGetIntegerv(GL_SCISSOR_BOX, iv);
    TRACE(1, _b("GL_SCISSOR_BOX: %u/%u/%u/%u"), iv[0], iv[1], iv[2], iv[3]);
    glGetBooleanv(GL_SCISSOR_TEST, bv);
    TRACE(1, _b("GL_SCISSOR_TEST: %d"), bv[0]);
    glGetBooleanv(GL_COLOR_WRITEMASK, bv);
    TRACE(1, _b("GL_COLOR_WRITEMASK: %d/%d/%d/%d"), bv[0], bv[1], bv[2], bv[3]);
    glGetBooleanv(GL_CULL_FACE, bv);
    TRACE(1, _b("GL_CULL_FACE: %d"), bv[0]);
    glGetIntegerv(GL_CULL_FACE_MODE, iv);
    TRACE(1, _b("GL_CULL_FACE_MODE: %u"), iv[0]);                    
}

/* ...initialize GL-processing context */
static int app_context_init(widget_data_t *widget, void *data)
{
    app_data_t     *app = data;
    window_data_t  *window = (window_data_t *)widget;
    int             W = widget_get_width(widget);
    int             H = widget_get_height(widget);
    
    /* ...initialize surround-view engine (dimensions are fixed?) */
    CHK_ERR(app->sv = sview_engine_init(app->sv_cfg, 1280, 800), -errno);

    
    
    /* ...create UI layer */
    CHK_ERR(app->gui = gui_create(window, app), -errno);

//    /* ...display context is shared with all windows; context is surfaceless */
//    eglMakeCurrent(display->egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, display->egl.ctx);
//    BUG(eglGetCurrentContext() == EGL_NO_CONTEXT, _x("invalid egl context"));
//    
//    sview_engine_destroy(app->sv);
//    app->sv = sview_engine_init(app->sv_cfg, 1280, 800);
//    
//    sview_engine_destroy(app->sv);
//    app->sv = sview_engine_init(app->sv_cfg, 1280, 800);
//    
//    sview_bv_reinit(app->sv,app->sv_cfg, 1280, 800);
    
    TRACE(INIT, _b("run-time initialized: %u*%u"), W, H);

    return 0;
}

/*******************************************************************************
 * Input events processing
 ******************************************************************************/
#ifdef SPACENAV_ENABLED
/* ...3D-joystick input processing */
static inline widget_data_t * app_spnav_event(app_data_t *app, widget_data_t *widget, widget_spnav_event_t *event)
{
    /* ...pass notification to the surround-view engine */
    if (app->flags & APP_FLAG_SVIEW)
    {
        if (event->e->type == SPNAV_EVENT_MOTION)
        {
            pthread_mutex_lock(&app->access);
            sview_engine_spnav_event(app->sv, event->e);
            pthread_mutex_unlock(&app->access);
        }
    }

    return widget;
}
#endif

/*******************************************************************************
 * Touchscreen inputs processing
 ******************************************************************************/

/* ...touch-screen event processing */
static inline widget_data_t * app_touch_event(app_data_t *app, widget_data_t *widget, widget_touch_event_t *event)
{
    if (app->flags & APP_FLAG_SVIEW)
    {
        pthread_mutex_lock(&app->access);

        switch (event->type)
        {
        case WIDGET_EVENT_TOUCH_DOWN:
            sview_engine_touch(app->sv, TOUCH_DOWN, event->id, event->x, event->y);
            break;
            
        case WIDGET_EVENT_TOUCH_MOVE:
            sview_engine_touch(app->sv, TOUCH_MOVE, event->id, event->x, event->y);
            break;

        case WIDGET_EVENT_TOUCH_UP:
            sview_engine_touch(app->sv, TOUCH_UP, event->id, event->x, event->y);
            break;
        }

        pthread_mutex_unlock(&app->access);
    }
    
    return widget;
}

/* ...keyboard event processing */
static inline widget_data_t * app_key_event(app_data_t *app, widget_data_t *widget, widget_key_event_t *event)
{
    pthread_mutex_lock(&app->access);

    if (app->flags & APP_FLAG_SVIEW)
    {
        if (event->type == WIDGET_EVENT_KEY_PRESS)
        {
            TRACE(DEBUG, _b("Key pressed: %i"), event->code);
            sview_engine_keyboard_key(app->sv, event->code, event->state);
        }
    }
    else
    {
#ifdef ENABLE_OBJDET
        if (event->type == WIDGET_EVENT_KEY_PRESS)
        {
            objdet_engine_keyboard_key(app->od, event->code, event->state);
        }
#endif
    }

    pthread_mutex_unlock(&app->access);

    return widget;
}

/* ...keyboard event processing */
static inline widget_data_t * app_mouse_event(app_data_t *app, widget_data_t *widget, widget_mouse_event_t *event)
{
    pthread_mutex_lock(&app->access);

    if (app->flags & APP_FLAG_SVIEW)
    {
        if (event->type == WIDGET_EVENT_MOUSE_BUTTON)
        {
            sview_engine_mouse_button (app->sv, event->button, event->state);
        }
        else if (event->type == WIDGET_EVENT_MOUSE_MOVE)
          {
            sview_engine_mouse_motion (app->sv, event->x, event->y);
          }
        else if (event->type == WIDGET_EVENT_MOUSE_AXIS)
          {
            sview_engine_mouse_wheel (app->sv, event->axis, event->value);
          }
    }
    else
    {
#ifdef ENABLE_OBJDET
        if (event->type == WIDGET_EVENT_KEY_PRESS)
        {
            objdet_engine_keyboard_key(app->od, event->code, event->state);
        }
#endif
    }

    pthread_mutex_unlock(&app->access);

    return widget;
}

/* ...event-processing function */
static widget_data_t * app_input_event(widget_data_t *widget, void *cdata, widget_event_t *event)
{
    app_data_t     *app = cdata;
    widget_data_t  *focus;

    /* ...pass event to GUI layer first */
    if (!app->gui || !(focus = widget_input_event(app->gui, event)) || focus == widget)
    {
        switch (WIDGET_EVENT_TYPE(event->type))
        {
#ifdef SPACENAV_ENABLED
        case WIDGET_EVENT_SPNAV:
            return app_spnav_event(app, widget, &event->spnav);
#endif
        case WIDGET_EVENT_TOUCH:
            return app_touch_event(app, widget, &event->touch);
            
        case WIDGET_EVENT_KEY:
            return app_key_event(app, widget, &event->key);
            
        case WIDGET_EVENT_MOUSE:
          return app_mouse_event (app, widget, &event->mouse);

        default:
            return NULL;
        }
    }

    /* ...event is consumed by GUI layer */
    return (focus ? : widget);
}

/*******************************************************************************
 * Pipeline control flow callback
 ******************************************************************************/

static gboolean app_bus_callback(GstBus *bus, GstMessage *message, gpointer data)
{
    app_data_t     *app = data;
    GMainLoop      *loop = app->loop;

    switch (GST_MESSAGE_TYPE(message))
    {
    case GST_MESSAGE_ERROR:
    {
        GError     *err;
        gchar      *debug;

        /* ...dump error-message reported by the GStreamer */
        gst_message_parse_error (message, &err, &debug);
        TRACE(ERROR, _b("execution failed: %s"), err->message);
        g_error_free(err);
        g_free(debug);

        /* ...right now this is a fatal error */
        BUG(1, _x("breakpoint"));

        /* ...and terminate the loop */
        g_main_loop_quit(loop);
        break;
    }

    case GST_MESSAGE_EOS:
    {
        /* ...end-of-stream encountered; break the loop */
        TRACE(INFO, _b("execution completed"));
        g_main_loop_quit(loop);
        break;
    }

    case GST_MESSAGE_STATE_CHANGED:
    {
        /* ...state has changed; test if it is start or stop */
        if (GST_MESSAGE_SRC(message) == GST_OBJECT_CAST(app->pipe))
        {
            GstState        old, new, pending;
        
            /* ...parse state message */
            gst_message_parse_state_changed(message, &old, &new, &pending);

            TRACE(INFO, _b("transition from %d to %d"), old, new);
        }

        break;
    }
    
    default:
        /* ...ignore message */
        TRACE(0, _b("ignore message: %s"), gst_message_type_get_name(GST_MESSAGE_TYPE(message)));
    }

    /* ...remove message from the queue */
    return TRUE;
}

/*******************************************************************************
 * Module initialization
 ******************************************************************************/

/* ...processing window parameters (surround-view scene) */
static window_info_t app_main_info = {
    .fullscreen = 1,
    .transform = 180,
};

/* ...main window widget paramters (input-interface + GUI?) */
static widget_info_t app_main_info2 = {
    .init = app_context_init,
    .event = app_input_event,
};

/* ...start surround-view track */
static track_desc_t * __app_sview_track(app_data_t *app)
{
    track_desc_t *track;
    
    if (app->flags & APP_FLAG_LIVE)
    {   
        track = sview_track_live();
    }
    else if (app->flags & APP_FLAG_NEXT)
    {
        track = sview_track_next();
    }
    else if (app->flags & APP_FLAG_PREV)
    {
        track = sview_track_prev();
    }
    else
    {
        track = sview_track_current();
    }

    /* ...mark all queues are empty */
    app->frames = (1 << CAMERAS_NUMBER) - 1;

    /* ...set window rendering hook */
    app_main_info.redraw = sview_redraw;
    app_main_info.init_bv = sview_init_bv;
    
    return track;
}



/* ...gstreamer thread (separated from decoding) */
void * app_thread(void *arg)
{
    app_data_t     *app = arg;
    track_desc_t   *track = NULL;

    /* ...acquire internal data access lock */
    pthread_mutex_lock(&app->lock);
    
    /* ...play all tracks in a loop */
    while ((app->flags & APP_FLAG_EXIT) == 0)
    {
        /* ...select whether we need to start surround-view or frontal camera track */
#ifdef ENABLE_OBJDET
        if (app->flags & APP_FLAG_SVIEW)
        {
#endif
            track = __app_sview_track(app);
 #ifdef ENABLE_OBJDET           
        }
        else
        {
            track = __app_objdet_track(app);
        }
#endif             
        TRACE(INIT, _b("Track type: %d"), track->type);
        
        /* ... Only for sview tracks: reset bv if cfg is different */
#ifdef ENABLE_OBJDET
        if (track->type == TRACK_TYPE_SVIEW)
        {
#endif   

          if (track->camera_cfg && (strcmp (app->sv_cfg->config_path, track->camera_cfg) != 0))
            {
              int i;
              app->sv_cfg->config_path = track->camera_cfg;
              app->sv_cfg->pixformat = track->pixformat;
              for (i = 0; i < CAMERAS_NUMBER; i++)
                {
                  app->sv_cfg->cam_names[i] = track->camera_names[i];
                }
              window_reinit_bv (app->window);
            }
            
#ifdef ENABLE_OBJDET
        }
#endif          
        /* ...start a selected track (ignore error) */
        app_track_start(app, track, 1);

        /* ...release internal data access lock */
        pthread_mutex_unlock(&app->lock);

        /* ...set pipeline to playing state (start streaming from selected cameras) */
        gst_element_set_state(app->pipe, GST_STATE_PLAYING);

        TRACE(INIT, _b("enter main loop"));
        
        /* ...start main application loop */
        g_main_loop_run(app->loop);

        /* ...re-acquire internal data access lock */
        pthread_mutex_lock(&app->lock);

        /* ...put end-of-stream flag */
        app->flags |= APP_FLAG_EOS;

        /* ...kick renderer window to drop all buffers */
        window_schedule_redraw(app->window);

        TRACE(INFO, _b("track '%s' completed"), (track->info ? : "default"));

        /* ...release internal lock to allow termination sequence to complete */
        pthread_mutex_unlock(&app->lock);
        
        /* ...stop the pipeline (stop streaming) */
        gst_element_set_state(app->pipe, GST_STATE_NULL);

        /* ...stop current track */
        app_track_start(app, track, 0);

        TRACE(DEBUG, _b("streaming stopped"));

        /* ...wait for a renderer completion */
        pthread_mutex_lock(&app->lock);

        /* ...destroy camera interface */
        (app->sv_camera ? gst_bin_remove(GST_BIN(app->pipe), app->sv_camera), app->sv_camera = NULL : 0);
        (app->fr_camera ? gst_bin_remove(GST_BIN(app->pipe), app->fr_camera), app->fr_camera = NULL : 0);

        TRACE(DEBUG, _b("bins removed"));

        /* ...clear end-of-stream status */
        app->flags &= ~APP_FLAG_EOS;
    }

    /* ...release internal data access lock */
    pthread_mutex_unlock(&app->lock);

    /* ...destroy pipeline and all hosted elements */
    gst_object_unref(app->pipe);

    return NULL;
}

/* ...end-of-stream signalization */
void app_eos(app_data_t *app)
{
    GstMessage     *message = gst_message_new_eos(GST_OBJECT(app->pipe));

    gst_element_post_message(GST_ELEMENT_CAST(app->pipe), message);
}

#ifdef ENABLE_CAMERA_MJPEG
/* ...network packet reception hook */	 
void app_packet_receive(app_data_t *app, int id, u8 *pdu, u16 len, u64 ts)	 
{	 
    /* ...pass packet to the camera bin */	 
    camera_mjpeg_packet_receive(id, pdu, len, ts); 
}
#endif

#ifdef ENABLE_OBJDET
/* ...CAN data parsing - should be somehow delegated to external code */
static inline float steering_wheel_degree(u8 *p)
{
    u16     t16 = *(u16 *)p;
    float   angle = (t16 & 0x7FFF) * 0.04375;
    
    return (t16 & 0x8000 ? -angle : angle);
}

/* ...CAN message reception hook */
void app_can_message_receive(app_data_t *app, u32 can_id, u8 *msg, u8 dlc, u64 ts)
{
    vehicle_info_t     *info = &app->vehicle;
    
    switch (can_id)
    {
    case 0x280:
        info->rpm = (msg[2] | (msg[3] << 8)) * 0.25;
        TRACE(DEBUG, _b("engine rpm: %.1f"), info->rpm);
        break;
        
    case 0x2AC:
        /* ...parse vehicle speed information; km/h, * 0.01 */
        if (dlc != 6)   return;
        
        info->speed = (*(u16 *)(msg + 3)) * 0.01;
        TRACE(DEBUG, _b("vehicle speed: %f km/h"), info->speed);
        break;

    case 0xC2:
        /* ...steering wheel data; degree, * 0.04375 */
        if (dlc != 8)   return;

        info->steering_angle = steering_wheel_degree(msg);
        info->steering_rotation = steering_wheel_degree(msg + 2);
        TRACE(DEBUG, _b("steering wheel: %f degree, rot = %f degree/sec"), info->steering_angle, info->steering_rotation);
        break;
        
    case 0x488:
        /* ...accelerator pedal position: percent, factor 0.4 */
        if (dlc != 8)       return;

        info->accelerator = (*(msg + 6)) * 0.4;
        TRACE(DEBUG, _b("accelerator pedal: %.1f%%"), info->accelerator);
        break;

    case 0x440:
        /* ...current/target gear */
        info->gear = msg[1] & 0xF;
        TRACE(DEBUG, _b("current gear: %d (%s)"), info->gear, msg[0] & 1 ? "target" : "current");
        break;
        
    case 0x1AC:
        /* ...brake switch status */
        info->brake_switch = *(msg + 5) & (1 << 4);
        TRACE(DEBUG, _b("brake switch status: %d"), info->brake_switch);
        break;

    case 0x390:
        /* ...miscellaneous status bits */
        info->direction_switch = msg[4];
        break;

    case 0x4A8:
        /* ...brake pedal pressure (in bars) */
        info->brake_pressure = (msg[2] | ((msg[3] & 0xF) << 8)) * 0.1;
        TRACE(DEBUG, _b("brake pressure: %.2f bars"), info->brake_pressure);
        break;
        
    case 0x594:
        /* ...wheel arc height */
        if (dlc != 8)       return;
        
        memcpy(info->wheel_arc, msg + 2, 4);
        TRACE(DEBUG, _b("wheel arc height: %u/%u/%u/%u"), msg[2], msg[3], msg[4], msg[5]);
        break;
    }
}
#endif

/*******************************************************************************
 * GUI events processing callbacks
 ******************************************************************************/

/* ...enable spherical projection */
void sview_sphere_enable(app_data_t *app, int enable)
{
    pthread_mutex_lock(&app->access);
    sview_engine_keyboard_key(app->sv, KEY_H, 1);
    pthread_mutex_unlock(&app->access);
}

/* ...select live capturing mode */
void app_live_enable(app_data_t *app, int enable)
{
    /* ...set surround view scene showing */
    pthread_mutex_lock(&app->lock);

    if (enable)
        app->flags |= APP_FLAG_LIVE;
    else
        app->flags &= ~APP_FLAG_LIVE;

    pthread_mutex_unlock(&app->lock);
    
    TRACE(INFO, _b("live capturing mode: %d"), enable);

    /* ...force track restart */
    app_eos(app);
}

/* ...reset projection to preset */
void sview_set_view(app_data_t *app, int view)
{
    pthread_mutex_lock(&app->access);
    sview_engine_keyboard_key(app->sv, (view ? KEY_9 : KEY_0), 1);
    pthread_mutex_unlock(&app->access);
}

void sview_adjust(app_data_t *app) {
    pthread_mutex_lock(&app->access);
    sview_engine_keyboard_key(app->sv, KEY_Q, 1);
    pthread_mutex_unlock(&app->access);
}

void sview_calibrate(app_data_t *app) {
    pthread_mutex_lock(&app->access);
    sview_engine_keyboard_key(app->sv, KEY_C, 1);
    pthread_mutex_unlock(&app->access);
}

void sview_load_calibration(app_data_t *app) {
    pthread_mutex_lock(&app->access);
    sview_engine_keyboard_key(app->sv, KEY_8, 1);
    pthread_mutex_unlock(&app->access);
}

void sview_escape(app_data_t *app) {
    pthread_mutex_lock(&app->access);
    sview_engine_keyboard_key(app->sv, KEY_ESC, 1);
    pthread_mutex_unlock(&app->access);
}

/* ...switch to next track */
void app_next_track(app_data_t *app)
{
    pthread_mutex_lock(&app->lock);
    app->flags |= APP_FLAG_NEXT;
    pthread_mutex_unlock(&app->lock);
    
    /* ...emit end-of-stream to a main-loop */
    app_eos(app);
}

/* ...switch to previous track */
void app_prev_track(app_data_t *app)
{
    /* ...force switching to previous track */
    pthread_mutex_lock(&app->lock);
    app->flags |= APP_FLAG_PREV;
    pthread_mutex_unlock(&app->lock);

    /* ...emit end-of-stream to a main-loop */
    app_eos(app);
}

/* ...restart current track */
void app_restart_track(app_data_t *app)
{
    /* ...emit end-of-stream to a main-loop */
    app_eos(app);
}

/* ...enable surround-view scene showing */
void sview_scene_enable(app_data_t *app, int enable)
{
    /* ...set surround view scene showing */
    pthread_mutex_lock(&app->lock);

    if (enable)
        app->flags |= APP_FLAG_SVIEW;
    else
        app->flags &= ~APP_FLAG_SVIEW;

    pthread_mutex_unlock(&app->lock);
    
    TRACE(INFO, _b("surround-view scene: %d"), enable);

    /* ...force track restart */
    app_eos(app);
}

/* ...enable debugging output */
void app_debug_enable(app_data_t *app, int enable)
{
    /* ...set debug output status */
    pthread_mutex_lock(&app->lock);

    if (enable)
        app->flags |= APP_FLAG_DEBUG;
    else
        app->flags &= ~APP_FLAG_DEBUG;

    pthread_mutex_unlock(&app->lock);
    
    TRACE(INFO, _b("debug-data output enable: %d"), enable);
}

/* ...close application */
void app_exit(app_data_t *app)
{
    TRACE(INFO, _b("application termination command"));

    /* ...cause application to terminate */
    pthread_mutex_lock(&app->lock);
    app->flags |= APP_FLAG_EXIT;
    pthread_mutex_unlock(&app->lock);

    /* ...emit end-of-stream condition */
    app_eos(app);
}

/*******************************************************************************
 * Module entry-points
 ******************************************************************************/

/* ...module destructor */
static void app_destroy(gpointer data, GObject *obj)
{
    app_data_t     *app = data;

    TRACE(INIT, _b("destruct module"));

    /* ...destroy main loop */
    g_main_loop_unref(app->loop);

    /* ...destroy GUI layer */
    (app->gui ? widget_destroy(app->gui) : 0);

    /* ...destroy surround-view engine data */
    (app->sv ? sview_engine_destroy(app->sv) : 0);
    
    /* ...destroy main application window */
    (app->window ? window_destroy(app->window) : 0);
    
    /* ...free application data structure */
    free(app);

    TRACE(INIT, _b("module destroyed"));
}

/* ...set surround-view camera set interface */
int sview_camera_init(app_data_t *app, camera_init_func_t camera_init)
{
    GstElement     *bin;
    
    /* ...create camera interface (it may be network camera or file on disk) */
    CHK_ERR(bin = camera_init(&sv_camera_cb, app, CAMERAS_NUMBER), -errno);

    /* ...add cameras to a pipe */
    gst_bin_add(GST_BIN(app->pipe), bin);

    /* ...synchronize state with a parent */
    gst_element_sync_state_with_parent(bin);

    /* ...save camera-set container */
    app->sv_camera = bin;

    TRACE(INIT, _b("surround-view camera-set initialized"));

    return 0;
}

/* ...module initialization function */
app_data_t * app_init(display_data_t *display, sview_cfg_t *sv_cfg, int flags)
{
    app_data_t     *app;
    GstElement     *pipe;

    /* ...create local data handle */
    CHK_ERR(app = calloc(1, sizeof(*app)), (errno = ENOMEM, NULL));

    /* ...save menu information*/
    app->configuration = flags;
    
    /* ...save global configuration data pointers */
    app->sv_cfg = sv_cfg;
#ifdef ENABLE_OBJDET
    app->od_cfg = NULL;
#endif
    
    /* ...default flags for an application */
//    app->flags = 1 * APP_FLAG_SVIEW | 1 * APP_FLAG_LIVE | APP_FLAG_NEXT;
    app->flags = app->configuration | APP_FLAG_NEXT;
    
    /* ...set output device number */
    app_main_info.output = __output_main;

    /* ...set transformation */
    app_main_info.transform = __output_transform;
    
    /* ...create full-screen window for processing results visualization */
    TRACE(DEBUG, _b("window_create app [%p]"), app);
    if ((app->window = window_create(display, &app_main_info, &app_main_info2, app)) == NULL)
    {
        TRACE(ERROR, _x("failed to create main window: %m"));
        goto error;
    }

    /* ...create main loop object (use default context) */
    if ((app->loop = g_main_loop_new(NULL, FALSE)) == NULL)
    {
        TRACE(ERROR, _x("failed to create main loop object"));
        errno = ENOMEM;
        goto error_window;
    }
    else
    {
        /* ...push default thread context for all subsequent sources */
        g_main_context_push_thread_default(g_main_loop_get_context(app->loop));
    }

    /* ...create a pipeline */
    if ((app->pipe = pipe = gst_pipeline_new(NULL)) == NULL)
    {
        TRACE(ERROR, _x("pipeline creation failed"));
        errno = ENOMEM;
        goto error_loop;
    }
    else
    {
        GstBus  *bus = gst_pipeline_get_bus(GST_PIPELINE(pipe));
        gst_bus_add_watch(bus, app_bus_callback, app);
        gst_object_unref(bus);
    }

    /* ...add destructor to the pipe */
    g_object_weak_ref(G_OBJECT(pipe), app_destroy, app);

    /* ...initialize internal data access lock */
    pthread_mutex_init(&app->lock, NULL);

    /* ...initialize engine access lock */
    pthread_mutex_init(&app->access, NULL);

    /* ...initialize synchronous operation completion variable */
    pthread_cond_init(&app->wait, NULL);

    TRACE(INIT, _b("module initialized"));

    return app;

error_loop:
    /* ...destroy main loop */
    g_main_loop_unref(app->loop);

error_window:
    /* ...destroy main application window */
    window_destroy(app->window);
    
error:
    /* ...destroy data handle */
    free(app);
    return NULL;
}

int app_has_multiple_sources(app_data_t *app)
{
    return (app->configuration & (APP_FLAG_FILE | APP_FLAG_LIVE)) == (APP_FLAG_FILE | APP_FLAG_LIVE);
}
