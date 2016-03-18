/*******************************************************************************
 * utest-display-wayland.h
 *
 * Display support for X11 client
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

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <GL/gl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "utest.h"
#include "utest-common.h"
#include "utest-display.h"

/* ...display data */
struct display_data
{
    /*display handle */
    Display* display;

    /* ...pending display event status */
    int                         pending;

    /* ...polling structure handle */
    int                         efd;

    /* ...dispatch thread handle */
    pthread_t                   thread;

    /* ...display lock (need that really? - tbd) */
    pthread_mutex_t             lock;
};

/* ...output window data */
struct window_data
{
	/*handle*/
	Window window;

    /* ...reference to a display data */
    display_data_t             *display;

    /* ...window information */
    const window_info_t        *info;

    /* ...client data for a callback */
    void                       *cdata;

    /* ...current window size */
    int                         width, height;

    /* ...command pipe */
    int                         pipe[2];

    /* ...synchronous commands processing lock */
    pthread_mutex_t             lock;

    /* ...conditional variable for synchronous commands processing */
    pthread_cond_t              wait;

    /* ...pending redraw flag */
    int                         redraw;
};

static display_data_t   __display;

/* ...communication between application and display thread */
typedef struct window_cmd
{
    /* ...user-supplied callback */
    int           (*hook)(window_data_t *, void *);

    /* ...callback-specific data */
    void           *data;

}   window_cmd_t;


/*******************************************************************************
 * Window internal functions
 ******************************************************************************/

/* ...process window redraw command */
static int __window_redraw(window_data_t *window, void *data)
{
    /* ...clear redraw event */
    window->redraw = 0;

    /* ...invoke user-supplied hook */
    window->info->redraw(window->display, window->cdata);

    return 0;
}

/* ...create EGL ARGB surface */
window_data_t * window_create(display_data_t *display, const window_info_t *info, void *cdata)
{
    int                 width = info->width, height = info->height;
    window_data_t      *window;

    Display* dsp = display->display;

    printf("create window\n");
    /* ...allocate a window data */
    if ((window = malloc(sizeof(*window))) == NULL)
    {
        return NULL;
    }

    memset(window, 0 , sizeof (window_data_t));

    /* ...create window communication pipe */
    if (pipe2(window->pipe, O_NONBLOCK) != 0)
    {
        goto error;
    }

    /* ...create mutex and conditional variable for synchronous commands processing */
    //pthread_cond_init(&window->wait, NULL);
    //pthread_mutex_init(&window->lock, NULL);

    /* ...save display handle */
    window->display = display;

    /* ...set current window parameters */
    window->width = width, window->height = height;

    /* ...save window info data */
    window->info = info, window->cdata = cdata;

    /* ...clear redraw flag */
    window->redraw = 0;

    int screenNumber = DefaultScreen(dsp);
    unsigned long white = WhitePixel(dsp,screenNumber);
    unsigned long black = BlackPixel(dsp,screenNumber);

    Window win = XCreateSimpleWindow(dsp,
                                 DefaultRootWindow(dsp),
                                 500, 500,   // origin
                                 1280, 800, // size
                                 0, black, // border
                                 black );  // backgd
    window->window = win;

    XMapWindow( dsp, win );

    long eventMask = StructureNotifyMask;
    XSelectInput( dsp, win, eventMask );

    XEvent evt;
    do
    {
      XNextEvent( dsp, &evt );   // calls XFlush
    }
    while( evt.type != MapNotify );

    GC gc = XCreateGC( dsp, win,
                       0,        // mask of values
                       NULL );   // array of values
    XSetForeground( dsp, gc, black );

    eventMask = ButtonPressMask|ButtonReleaseMask;
    XSelectInput(dsp,win,eventMask); // override prev

    return window;

error:
    /* ...destroy window memory */
    free(window);
    return NULL;
}

/* ...destroy a window */
void window_destroy(window_data_t *window)
{
    display_data_t     *display = window->display;

    XDestroyWindow( display->display, window->window );

    /* ...destroy object */
    free(window);
}

/* ...return current window width */
int window_get_width(window_data_t *window)
{
    return window->width;
}

/* ...return current window height */
int window_get_height(window_data_t *window)
{
    return window->height;
}

/* ...schedule redrawal of the window */
void window_schedule_redraw(window_data_t *window)
{
    /* ...emit redraw command to a dispatch loop */
    pthread_mutex_lock(&window->lock);

    /* ...send a command only if there is no pending event */
    if (!window->redraw)
    {
        window->redraw = 1;

        //__window_command(window, __window_redraw, window);
        __window_redraw(window, NULL);
    }

    pthread_mutex_unlock(&window->lock);
}

/* ...submit window to a renderer */
void window_draw(window_data_t *window)
{
    display_data_t     *display = window->display;


}

/*******************************************************************************
 * Display module initialization
 ******************************************************************************/

/* ...create display data */
display_data_t * display_create(void)
{
    display_data_t     *display = &__display;
    pthread_attr_t      attr;
    int                 r;

    /* ...reset display data */
    memset(display, 0, sizeof(*display));

    XInitThreads();

    /* ...connect to X server display */
    display->display = XOpenDisplay(NULL);
    if (display->display == NULL)
    {
        goto error;
    }

    /* ...initialize thread attributes (joinable, default stack size) */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    /* ...wait until display thread starts? */
    TRACE(INIT, _b("X11 display interface initialized"));

    return display;

error_epoll:

error_disp:

error:
    return NULL;
}

/*******************************************************************************
 * Texture creation command
 ******************************************************************************/

/* ...command handle */
typedef struct texture_create_cmd
{
    /* ...image size */
    u16                 width, height;

    /* ...planes memory */
    void               *y, *uv;

    /* ...pointer where texture should be stored */
    texture_data_t    **out;

}   texture_create_cmd_t;

static int __texture_create(window_data_t *window, void *data)
{
    texture_create_cmd_t   *cmd = data;
    display_data_t         *display = window->display;
    texture_data_t         *texture;

    printf("texture create\n");

    /* ...allocate texture data */
    if ((texture = malloc(sizeof(*texture))) == NULL)
    {
        TRACE(ERROR, _x("failed to allocate memory"));
        return -ENOMEM;
    }

    /* ...save texture context */
    texture->window = window;

    GLuint vtex;
    int vtarget = GL_TEXTURE_2D;
    glGenTextures(1, &vtex);
    glBindTexture(vtarget, vtex);

    glTexParameteri(vtarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(vtarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexParameteri(vtarget, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(vtarget, GL_TEXTURE_WRAP_T, GL_CLAMP);

    glTexImage2D(vtarget, 0, GL_RGBA, cmd->width, cmd->height, 0,
    			GL_RGBA, GL_UNSIGNED_BYTE, cmd->y);
    glBindTexture(vtarget, 0);

    /* ...save texture in output pointer */
    *cmd->out = texture;

    return 0;
}

/* ...public API - display texture creation */
texture_data_t * window_texture_create(window_data_t *window, int width, int height, void *y, void *uv)
{
    texture_data_t         *texture = NULL;
    texture_create_cmd_t    cmd = { .width = width, .height = height, .y = y, .uv = uv, .out = &texture };

    /* ...issue a command to main display thread */
    __texture_create(window, &cmd);

    return texture;
}

/* ...destroy texture data */
void window_texture_destroy(window_data_t *window, texture_data_t *texture)
{

}

/*******************************************************************************
 * Texture drawing helpers
 ******************************************************************************/

/* ...output texture in current EGL context */
int texture_draw(texture_data_t *texture, int x0, int y0, int x1, int y1)
{

    return 0;
}
