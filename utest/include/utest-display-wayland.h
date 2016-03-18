/*******************************************************************************
 * utest-display-wayland.h
 *
 * Display support for a Wayland
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

#ifndef __UTEST_DISPLAY_H
#define __UTEST_DISPLAY_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "utest-display.h"
#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-cursor.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#ifdef ENABLE_OBJDET
#include <CL/cl.h>
#include <CL/cl_egl.h>
#endif

#include <cairo-gl.h>


/*******************************************************************************
 * EGL functions binding (make them global; create EGL adaptation layer - tbd)
 ******************************************************************************/

/* ...EGL extensions */
extern PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
extern PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
extern PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC eglSwapBuffersWithDamageEXT;
extern PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
extern PFNGLMAPBUFFEROESPROC glMapBufferOES;
extern PFNGLUNMAPBUFFEROESPROC glUnmapBufferOES;

extern PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR;
extern PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR;
extern PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR;

/*******************************************************************************
 * Types definitions
 ******************************************************************************/

/* ...EGL configuration data */
typedef struct egl_data
{
    /* ...EGL display handle associated with current wayland display */
    EGLDisplay              dpy;

    /* ...shared EGL context */
    EGLContext              ctx;

    /* ...current EGL configuration */
    EGLConfig               conf;

}   egl_data_t;


    

/* ...return EGL surface associated with window */
extern EGLSurface window_egl_surface(window_data_t *window);
extern EGLContext window_egl_context(window_data_t *window);

/* ...associated cairo surface handling */
extern cairo_t * window_get_cairo(window_data_t *window);
extern void window_put_cairo(window_data_t *window, cairo_t *cr);
extern cairo_device_t  *__window_cairo_device(window_data_t *window);



extern int window_set_invisible(window_data_t *window);




/*******************************************************************************
 * External VBO support
 ******************************************************************************/

/* ...external vertex-buffer object data */
typedef struct vbo_data
{
    /* ...buffer array object id */
    GLuint              vbo;

    /* ...index array object id */
    GLuint              ibo;

    /* ...individual vertex element size */
    u32                 size;

    /* ...number of elements in buffer */
    u32                 number;

    /* ...mapped buffer array */
    void               *buffer;

    /* ...mapped index buffer */
    void               *index;

}   vbo_data_t;

/* ...handling of VBOs */
extern vbo_data_t * vbo_create(u32 v_size, u32 v_number, u32 i_size, u32 i_number);
extern int vbo_map(vbo_data_t *vbo, int buffer, int index);
extern void vbo_draw(vbo_data_t *vbo, int offset, int stride, int number, GLfloat *pvm);
extern void vbo_unmap(vbo_data_t *vbo);
extern void vbo_destroy(vbo_data_t *vbo);

/*******************************************************************************
 * Public API
 ******************************************************************************/

/* ...get current EGL configuration data */
extern egl_data_t  * display_egl_data(display_data_t *display);

/*******************************************************************************
 * Miscellaneous helpers for 2D-graphics
 ******************************************************************************/

/* ...PNG images handling */
extern cairo_surface_t * widget_create_png(cairo_device_t *cairo, const char *path, int w, int h);
extern int widget_image_get_width(cairo_surface_t *cs);
extern int widget_image_get_height(cairo_surface_t *cs);

/* ...simple text output - tbd */

#endif  /* __UTEST_DISPLAY_H */
