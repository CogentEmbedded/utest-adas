/*******************************************************************************
 * utest-gui.c
 *
 * ADAS unit-test - graphical user interface
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

#define MODULE_TAG                      GUI

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "utest.h"
#include "utest-app.h"
#include "utest-event.h"
#include "utest-widgets.h"
#include "utest-display.h"
#include <math.h>

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(DEBUG, 1);

/*******************************************************************************
 * Resources location
 ******************************************************************************/

/* ...root for all GUI-resources (do we have any?) */ 
#define GUI_RES_DIR                 "resources/"

/*******************************************************************************
 * Local types definitions
 ******************************************************************************/

/* ...main menu data */
typedef struct menu
{
    /* ...generic menu widget */
    gui_menu_t              base;
    
    /* ...prepared gradient pattern */
    cairo_pattern_t        *pattern;

    /* ...prepared rounded rectangle path */
    cairo_path_t           *path;
    
}   menu_t;
    
/* ...main GUI layer data */
typedef struct gui
{
    /* ...application reference */
    app_data_t         *app;

    /* ...activity state */
    int                 active;

    /* ...menu widget */
    menu_t              menu;

    /* ...main application window reference */
    window_data_t      *window;

    /* ...timer source */
    timer_source_t     *fadeout;

    /* ...long touch detector */
    timer_source_t     *touch_timer;

    /* ...current transparency level */
    float               alpha;

    /* ...transparency counter */
    int                 fadeout_cnt;

    /* ...spacenav accumulator for "forward/backward" transitions */
    int                 spnav_rewind;

    /* ...spacenav accumulator for "push" event */
    int                 spnav_push;

    /* ...spacenav buttons state */
    int                 spnav_buttons;

    /* ...output debug status */
    int                 debug;

}   gui_t;

/*******************************************************************************
 * Static data definition
 ******************************************************************************/

/* ...GUI singleton */
static gui_t    __gui;

/*******************************************************************************
 * GUI commands
 ******************************************************************************/

#define GUI_CMD_NONE                    0
#define GUI_CMD_ENTER                   1
#define GUI_CMD_LEAVE                   2
#define GUI_CMD_FORWARD                 3
#define GUI_CMD_BACKWARD                4
#define GUI_CMD_SELECT                  5
#define GUI_CMD_CLOSE                   6

/*******************************************************************************
 * Menu commands processing
 ******************************************************************************/

/* ...process menu command */
static void gui_menu_command(gui_menu_t *menu, int command)
{
    TRACE(DEBUG, _b("menu command %X, focus: %p"), command, menu->focus);
    
    switch (command)
    {
    case GUI_CMD_SELECT:
        /* ...pass ENTER command to current focus item */
        if (menu->focus)
        {
            TRACE(INFO, _b("select command (focus=%p)"), menu->focus);
            menu->focus->select(menu->focus, menu->widget);
            break;
        }
        
        /* ...if no current focus, treat as "enter" command */

    case GUI_CMD_ENTER:
        /* ...focus receiving command */
        menu->focus = gui_menu_first(menu);

        TRACE(INFO, _b("enter command (focus=%p)"), menu->focus);
        
        /* ...enable controls */
        break;

    case GUI_CMD_FORWARD:
        /* ...forward movement command */
        menu->focus = (gui_menu_next(menu, menu->focus) ? : gui_menu_first(menu));
        break;

    case GUI_CMD_BACKWARD:
        /* ...backward movement command */
        menu->focus = (gui_menu_prev(menu, menu->focus) ? : gui_menu_last(menu));
        break;

    case GUI_CMD_CLOSE:
        /* ...disable GUI controls; pass through */

    case GUI_CMD_LEAVE:
        /* ...clear current focus item */
        menu->focus = NULL;
        break;
    }

    /* ...update widget content */
    widget_schedule_redraw(menu->widget);
}

/*******************************************************************************
 * Boolean menu item (check-box) drawing function 
 ******************************************************************************/

/* ...output check-box menu item in given position */
static inline void menu_item_draw(gui_menu_item_t *item, cairo_t *cr, int x, int y, int width, int height, int active)
{
    cairo_text_extents_t    text_extents;
    int                     h = height / 2, r = h - 5;

    /* ...update region covered by item */
    item->x = x, item->y = y, item->w = width, item->h = height;

    cairo_save(cr);

    /* ...if item has a boolean value, show a checkbox */
    if (item->flags & GUI_MENU_ITEM_CHECKBOX)
    {
        /* ...output checkbox item in current context */
        cairo_set_line_width(cr, 3.0);
        cairo_set_source_rgb(cr, 0.9, 0.9, 1.0);
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + width - h, y + h, r, 0, 2 * M_PI);
        cairo_stroke(cr);

        /* ...fill center of the circle if selected */
        if (item->flags & GUI_MENU_ITEM_CHECKBOX_STATE)
        {
            cairo_set_line_width(cr, 1.0);
            cairo_set_source_rgb(cr, 0.8, 0.8, 1.0);
            cairo_arc(cr, x + width - h, y + h, r - 4, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }
    
    /* ...output text next to the checkbox icon */
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_text_extents(cr, item->text, &text_extents);
    cairo_move_to(cr, x + 0 * height , y + (height - text_extents.y_bearing) / 2);
    cairo_show_text(cr, item->text);

    if (active)
    {
        cairo_set_source_rgba(cr, 0.7, 0.7, 1.0, 0.5);
        cairo_rectangle(cr, x, y, width, height);
        cairo_fill(cr);
    }
    
    cairo_restore(cr);
}

/* ...test if menu widget is hit */
static inline int __menu_hit(widget_data_t *widget, int x, int y)
{
    int     x0 = widget_get_left(widget);
    int     x1 = widget_get_width(widget) + x0;
    int     y0 = widget_get_top(widget);
    int     y1 = widget_get_height(widget) + y0;
        
    return (x > x0 - 10) && (x < x1 + 10) && (y > y0 - 10) && (y < y1 + 1);
}

/* ...test if menu item is hit */
static inline gui_menu_item_t * __menu_item_hit(widget_data_t *widget, int x, int y)
{
    gui_t              *gui = &__gui;
    menu_t             *menu = &gui->menu;
    gui_menu_t         *m = &menu->base;
    gui_menu_item_t    *item;

    /* ...translate coordinates to widget space */
    x -= widget_get_left(widget), y -= widget_get_top(widget);
    
    /* ...doesn't look well, but still */
    for (item = gui_menu_first(m); item; item = gui_menu_next(m, item))
        if (item->x <= x && item->x + item->w >= x && item->y <= y && item->y + item->h >= y)
            break;

    /* ...update menu focus */
    return (menu->base.focus = item);
}

/* ...menu drawing primitive */
static void __menu_draw(widget_data_t *widget, void *cdata, cairo_t *cr)
{
    gui_t                  *gui = cdata;
    menu_t                 *menu = &gui->menu;
    gui_menu_t             *m = &menu->base;
    int                     W = widget_get_width(widget);
    int                     H = widget_get_height(widget);
    float                   r = MIN(H / 3.0, 15);
    int                     x, y, h;
    gui_menu_item_t        *item;
	cairo_font_extents_t    font_extents;

    /* ...draw rounded rectangle as a background */
    cairo_new_sub_path(cr);
    cairo_arc(cr, W - r, r, r, -M_PI_2, 0);
    cairo_arc(cr, W - r, H - r, r, 0, M_PI_2);
    cairo_arc(cr, r, H - r, r,M_PI_2, M_PI);
    cairo_arc(cr, r, r, r, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);

    /* ...fill background */
    cairo_set_source(cr, menu->pattern);
    cairo_fill(cr);

    /* ...draw bouding line */
    if (0) {
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_stroke(cr);
    }
    
    /* ...select font to output items */
	cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 32);
	cairo_font_extents(cr, &font_extents);
    
    /* ...start drawing the item */
    x = y = (h = font_extents.height) / 2;
    
    /* ...output all menu items */
    for (item = gui_menu_first(m); item; item = gui_menu_next(m, item))
    {
        /* ...output item in current position */
        menu_item_draw(item, cr, x, y, W - h, h, (item == m->focus));

        /* ...advance drawing position */
        y += h;
    }
    TRACE(DEBUG, _b("Menu draw called!!!!"));
}

/*******************************************************************************
 * GUI widget interface
 ******************************************************************************/

/* ...main GUI layer drawing */
void gui_redraw(widget_data_t *widget, cairo_t *cr)
{
    gui_t      *gui = &__gui;

    /* ...check if GUI layer is active */
    if (!gui->active)       return;

    /* ...fade-out window content */
    cairo_set_source_rgba(cr, 0, 0, 0, gui->alpha);
    cairo_paint(cr);
    
    /* ...render menu widget image on the current surface */
    widget_render(gui->menu.base.widget, cr, gui->alpha);

    TRACE(DEBUG, _b("GUI drawing complete (alpha=%.2f)"), gui->alpha);
}

/*******************************************************************************
 * GUI command processing
 ******************************************************************************/

/* ...command processing */
static void gui_command(gui_t *gui, int command)
{
    /* ...pass command to a menu widget */
    gui_menu_command(&gui->menu.base, command);
    
    /* ...process closing command and probably something else */
    TRACE(INFO, _b("command '%u' processed"), command);
}

/*******************************************************************************
 * Fade-out state machine
 ******************************************************************************/

/* ...inactivity interval before fadeout sequence start (5 seconds) */
#define __FADEOUT_WATCHDOG_TIME     5000

/* ...time interval in ms between fadeout steps (linear descending of "alpha") */
#define __FADEOUT_STEP_TIME         30

/* ...total number of steps in a fadeout sequence (3 seconds overall) */
#define __FADEOUT_STEPS             100

/* ...initial value of alpha-channel for a controls plane */
#define __FADEOUT_ALPHA             0.5

/* ...step of alpha-channel change */
#define __FADEOUT_ALPHA_STEP        ((__FADEOUT_ALPHA - 0.05) / __FADEOUT_STEPS)

/* ...alpha level at step #n */
#define __FADEOUT_ALPHA_LEVEL(c)    (0.05 + (c) * __FADEOUT_ALPHA_STEP)

/* ...control-surface fade-out machine */
static gboolean gui_fadeout_timeout(void *data)
{
    gui_t  *gui = data;

    /* ...calculate current transparency level */
    if (gui->fadeout_cnt > 0)
    {
        /* ...set new transparency level */
        gui->alpha = __FADEOUT_ALPHA_LEVEL(gui->fadeout_cnt--);

        TRACE(0, _b("fadeout step: %d (alpha=%f)"), gui->fadeout_cnt, gui->alpha);
    }
    else
    {
        /* ...clear controls output flag */
        gui->active = 0;

        /* ...remove event source from the loop (no removal) */
        timer_source_stop(gui->fadeout);

        TRACE(0, _b("fadeout sequence complete"));
    }

    /* ...schedule main window redraw */
    window_schedule_redraw(gui->window);
    
    return TRUE;
}

/* ...reset fade-out watchdog */
static inline void gui_fadeout_reset(gui_t *gui)
{
    /* ...(re)start fadeout timer operation */
    timer_source_start(gui->fadeout, __FADEOUT_WATCHDOG_TIME, __FADEOUT_STEP_TIME);

    /* ..set initial transparency level */
    gui->alpha = __FADEOUT_ALPHA;

    /* ...set number of steps */
    gui->fadeout_cnt = __FADEOUT_STEPS;

    /* ...schedule main window redraw */
    window_schedule_redraw(gui->window);
    
    TRACE(0, _b("watchdog timer (re)started"));
}

/* ...start control-plane */
static inline void gui_controls_enable(gui_t *gui, int start)
{
    if (start)
    {
        /* ...make sure we have no control plane yet */
        if (gui->active == 0)
        {
            /* ...enable control plane */
            gui->active = 1;
            
            /* ...start watchdog timer */
            gui_fadeout_reset(gui);

            /* ...generate "enter" command */
            gui_command(gui, GUI_CMD_ENTER);
            
            TRACE(INFO, _b("controls plane enabled"));
        }
    }
    else
    {
        /* ...make sure we do have a control plane shown */
        if (gui->active != 0)
        {
            /* ...stop fadeout timer */
            timer_source_stop(gui->fadeout);

            /* ...clear controls flag */
            gui->active = 0;

            /* ...generate "leave" command */
            gui_command(gui, GUI_CMD_LEAVE);

            /* ...and force window redraw */
            window_schedule_redraw(gui->window);

            TRACE(INFO, _b("controls plane disabled"));
        }
    }
}

/*******************************************************************************
 * Input evets processing
 ******************************************************************************/

/* ...threshold for any pending sequence reset (in milliseconds) */
#define __SPNAV_SEQUENCE_THRESHOLD      200

/* ...threshold for a rewinding (tbd - taken out of the air) */
#define __SPNAV_REWIND_THRESHOLD        5000

/* ...threshold for a push event detection (again - out of the air - tbd) */
#define __SPNAV_PUSH_THRESHOLD          300

#ifdef SPACENAV_ENABLED
/* ...SpaceNav 3d-joystick events processing */
static widget_data_t * gui_input_spnav(gui_t *gui, widget_data_t *widget, spnav_event *e)
{
    /* ...process acivation event */
    if (gui->active == 0)
    {
        if (e->type == SPNAV_EVENT_BUTTON && e->button.press && e->button.bnum == 0)
        {
            /* ...activate GUI layer */
            gui_controls_enable(gui, 1);
            
            /* ...save buttons state */
            gui->spnav_buttons = 1 << 0;
            
            /* ...reset motion accumulators */
            gui->spnav_rewind = gui->spnav_push = 0;
            
            /* ...indicate input is intercepted */
            return widget;
        }
        else if (e->type == SPNAV_EVENT_BUTTON && e->button.press && e->button.bnum == 1)
        {
            /* ...treat that as a "next-track" event */
            app_next_track(gui->app);
        }
        else
        {
            /* ...ignore any other input event when inactive */
            return NULL;
        }
    }

    /* ...layer is active; process event */
    if (e->type == SPNAV_EVENT_MOTION)
    {
        int     rewind = gui->spnav_rewind;
        int     push = gui->spnav_push;

        TRACE(0, _b("spnav-event-motion: <x=%d,y=%d,z=%d>, <rx=%d,ry=%d,rz=%d>, p=%d"),
              e->motion.x, e->motion.y, e->motion.z,
              e->motion.rx, e->motion.ry, e->motion.rz,
              e->motion.period);

        /* ...reset accumulator if period exceeds a threshold */
        (e->motion.period > __SPNAV_SEQUENCE_THRESHOLD ? rewind = push = 0 : 0);

        TRACE(DEBUG, _b("spnav event: rewind=%d, push=%d, ry=%d, z=%d"), rewind, push, e->motion.rz, e->motion.z);
        
        /* ...use rotation around "y" axis for forward/backward movement */
        if ((rewind -= e->motion.ry) > __SPNAV_REWIND_THRESHOLD)
        {
            TRACE(DEBUG, _b("spnav 'forward' event decoded"));
            gui_command(gui, GUI_CMD_FORWARD);
            rewind = push = 0;
        }
        else if (rewind < -__SPNAV_REWIND_THRESHOLD)
        {
            TRACE(DEBUG, _b("spnav 'backward' event decoded"));
            gui_command(gui, GUI_CMD_BACKWARD);
            rewind = push = 0;
        }

        /* ...process push-movement (no accumulator here, maybe?) */
        if (push == 0)
        {
            /* ...push-event detector is enabled */
            if (e->motion.y < -__SPNAV_PUSH_THRESHOLD)
            {
                TRACE(DEBUG, _b("spnav 'push' event decoded"));
                gui_command(gui, GUI_CMD_SELECT);
                rewind = 0, push = -1;
            }
        }
        else
        {
            /* ...enable push-event detector if joystick is unpressed */
            if (e->motion.y >= -__SPNAV_PUSH_THRESHOLD / 10)
            {
                TRACE(DEBUG, _b("spnav 'push' detector activated"));
                push = 0;
            }
        }

        /* ...update accumulators state */
        gui->spnav_rewind = rewind, gui->spnav_push = push;
    }
    else if (e->type == SPNAV_EVENT_BUTTON)
    {
        int     old = gui->spnav_buttons;
        int     chg = (e->button.press ? 1 << e->button.bnum : 0) ^ old;

        /* ...process 'left-button' */
        if (chg & (1 << 0))
        {
            if ((old & (1 << 0)) == 0)
            {
                /* ...pressing "left" button is identical to 'push' event */
                TRACE(DEBUG, _b("spnav 'left-button-pressed' event decoded"));
                gui_command(gui, GUI_CMD_SELECT);
            }
            else
            {
                TRACE(DEBUG, _b("spnav 'left-button-released' event ignored"));
            }
        }

        /* ...process 'right-button' */
        if (chg & (1 << 1))
        {
            if ((old & (1 << 1)) == 0)
            {
                /* ...pressing "right" button is mapped to 'close' event */
                TRACE(DEBUG, _b("spnav 'right-button-pressed' event decoded"));

                /* ...deactivate GUI layer */
                gui_controls_enable(gui, 0);
            }
            else
            {
                TRACE(DEBUG, _b("spnav 'right-button-released' event ignored"));
            }
        }

        /* ...update buttons state */
        gui->spnav_buttons = (old ^= chg);

        TRACE(DEBUG, _b("spnav buttons state: %d:%d"), !!(old & 1), !!(old & 2));
    }

    /* ...in active state, input is intercepted */
    return widget;
}
#endif

/*******************************************************************************
 * Touchscreen events processing
 ******************************************************************************/

/* ...timeout for long-touch detection */
#define __TOUCH_LONG_TOUCH_THRESHOLD    750

/* ...long-touch detector timer */
static gboolean gui_touch_timeout(void *data)
{
    gui_t      *gui = data;
    
    /* ...enable controls plane */
    gui_controls_enable(gui, 1);
    
    return TRUE;
}

/* ...touch interface processing */
static widget_data_t * gui_input_touch(gui_t *gui, widget_data_t *widget, widget_touch_event_t *event)
{
    int     x, y;
    
    /* ...process activation event */
    if (gui->active == 0)
    {
        if ((event->type == WIDGET_EVENT_TOUCH_DOWN || event->type == WIDGET_EVENT_TOUCH_MOVE) && event->id == 0)
        {
            TRACE(DEBUG, _b("long-touch timer (re)started"));

            /* ...start one-shot long-touch detector */
            timer_source_start(gui->touch_timer, __TOUCH_LONG_TOUCH_THRESHOLD, 0);
        }
        else
        {
            TRACE(DEBUG, _b("long-touch timer stopped"));

            /* ...stop timer if it's running */
            timer_source_stop(gui->touch_timer);
        }

        /* ...do not intercept input */
        return NULL;
    }

    /* ...process only events from first touch-point */
    if (event->id != 0)     return widget;

    /* ...translate touch event with respect to window transformation */
    window_translate_coordinates(gui->window, event->x, event->y, &x, &y);
    
    /* ...process event type */
    switch (event->type)
    {
    case WIDGET_EVENT_TOUCH_DOWN:
        if (!__menu_hit(widget, x, y))
        {
            /* ...deactivate GUI layer */
            gui_controls_enable(gui, 0);
            break;
        }
        
    case WIDGET_EVENT_TOUCH_MOVE:
        /* ...process focus change */
        if (__menu_item_hit(widget, x, y) != NULL)
        {
            /* ...schedule widget update */
            widget_schedule_redraw(widget);
        }

        break;
        
    case WIDGET_EVENT_TOUCH_UP:
        /* ...treat as selection command for active item */
        gui_command(gui, GUI_CMD_SELECT);
        
        break;
    }
    
    /* ...active state processing */
    return widget;
}

/*******************************************************************************
 * Input event dispatcher
 ******************************************************************************/

static widget_data_t * __menu_input(widget_data_t *widget, void *cdata, widget_event_t *event)
{
    gui_t          *gui = cdata;
    widget_data_t  *focus;
    
    /* ...any input causes reset of watchdog sequence - hmm, tbd */
    (gui->active ? gui_fadeout_reset(gui) : 0);
    
    switch (WIDGET_EVENT_TYPE(event->type))
    {
#ifdef SPACENAV_ENABLED
    case WIDGET_EVENT_SPNAV:
        /* ...translate spnav event into internal commands */
        focus = gui_input_spnav(gui, widget, event->spnav.e);
        break;
#endif
    case WIDGET_EVENT_TOUCH:
        TRACE(DEBUG, _b("touch event: %X"), event->type);
        
        /* ...process touch interface */
        focus = gui_input_touch(gui, widget, &event->touch);
        break;
        
    default:
        /* ...ignore any other events (keep focus) */
    	TRACE(DEBUG, _b("ignore event: %u"), event->type);
        focus = NULL;
    }

    return (focus ? : widget_get_parent(widget));
}

/*******************************************************************************
 * GUI control layer redraw
 ******************************************************************************/


/*******************************************************************************
 * Menu widget initialization
 ******************************************************************************/

enum __menu_items {
//    __MENU_ITEM_OUTPUT,
	__MENU_ITEM_DUMMY,
    __MENU_ITEM_LIVE,
    __MENU_ITEM_DEBUG,
    __MENU_ITEM_SPHERE,
    __MENU_ITEM_ADJUST,
	__MENU_ITEM_CALIBRATE,
	__MENU_ITEM_LOAD_CALIBRATION,
    __MENU_ITEM_TOP,
    __MENU_ITEM_BACK45,
    __MENU_ITEM_CLOSE,
	__MENU_ITEM_ESC,
};


/* ...enable/disable spherical projection */
static void __sview_sphere(gui_menu_item_t *item, widget_data_t *widget)
{
    gui_t      *gui = &__gui;

    /* ...toggle menu state */
    item->flags ^= GUI_MENU_ITEM_CHECKBOX_STATE;

    TRACE(INFO, _b("surround-view sphere: %d"), !!(item->flags & GUI_MENU_ITEM_CHECKBOX_STATE));

    /* ...invoke application-provided callback */
    sview_sphere_enable(gui->app, !!(item->flags & GUI_MENU_ITEM_CHECKBOX_STATE));

    /* ...force widget update */
    widget_schedule_redraw(widget);
}

/* ...select live capturing mode */
static void __sview_live(gui_menu_item_t *item, widget_data_t *widget)
{
    gui_t      *gui = &__gui;

    /* ...toggle menu state */
    item->flags ^= GUI_MENU_ITEM_CHECKBOX_STATE;

    TRACE(INFO, _b("select live capturing: %d"), !!(item->flags & GUI_MENU_ITEM_CHECKBOX_STATE));

    /* ...invoke application-provided callback */
    app_live_enable(gui->app, !!(item->flags & GUI_MENU_ITEM_CHECKBOX_STATE));

    /* ...force widget update */
    widget_schedule_redraw(widget);
}

/* ...set top-view */
static void __sview_top_view(gui_menu_item_t *item, widget_data_t *widget)
{
    gui_t      *gui = &__gui;
    
    TRACE(INFO, _b("set top view"));

    /* ...invoke application callback */
    sview_set_view(gui->app, 0);
}

/* ...set back-45-view */
static void __sview_back45_view(gui_menu_item_t *item, widget_data_t *widget)
{
    gui_t      *gui = &__gui;
    
    TRACE(INFO, _b("set back-45 view"));

    /* ...invoke application callback */
    sview_set_view(gui->app, 1);
}

static void __sview_adjust_bv(gui_menu_item_t *item, widget_data_t *widget)
{
    gui_t      *gui = &__gui;
    
    TRACE(INFO, _b("adjust bird view"));

    /* ...invoke application callback */
    sview_adjust(gui->app);
}

static void __sview_calibrate_cam(gui_menu_item_t *item, widget_data_t *widget)
{
    gui_t      *gui = &__gui;

    TRACE(INFO, _b("calibrate camera"));

    /* ...invoke application callback */
    sview_calibrate(gui->app);
}

static void __sview_load_calibration(gui_menu_item_t *item, widget_data_t *widget)
{
    gui_t      *gui = &__gui;

    TRACE(INFO, _b("load camera calibration"));

    /* ...invoke application callback */
    sview_load_calibration(gui->app);
}

static void __sview_escape(gui_menu_item_t *item, widget_data_t *widget)
{
    gui_t      *gui = &__gui;

    TRACE(INFO, _b("escape"));

    /* ...invoke application callback */
    sview_escape(gui->app);
}


/* ...toggle surround-view scene */
//static void __sview_scene(gui_menu_item_t *item, widget_data_t *widget)
//{
//    gui_t      *gui = &__gui;
//
//    /* ...toggle menu state */
//    item->flags ^= GUI_MENU_ITEM_CHECKBOX_STATE;
//
//    TRACE(INFO, _b("surround-view scene: %d"), !!(item->flags & GUI_MENU_ITEM_CHECKBOX_STATE));
//
//    /* ...invoke application-provided callback */
//    sview_scene_enable(gui->app, !!(item->flags & GUI_MENU_ITEM_CHECKBOX_STATE));
//
//    /* ...force widget update */
//    widget_schedule_redraw(widget);
//}

/* ...switch to the next track */
static void __dummy_item(gui_menu_item_t *item, widget_data_t *widget)
{
	/* ...toggle debug output status */
	TRACE(INFO, _b("GUI dummy item"));
}

/* ...switch to the next track */
static void __debug_output(gui_menu_item_t *item, widget_data_t *widget)
{
    gui_t      *gui = &__gui;

    /* ...toggle menu state */
    item->flags ^= GUI_MENU_ITEM_CHECKBOX_STATE;

    TRACE(INFO, _b("debug-status: %d"), !!(item->flags & GUI_MENU_ITEM_CHECKBOX_STATE));

    /* ...invoke application callback */
    app_debug_enable(gui->app, !!(item->flags & GUI_MENU_ITEM_CHECKBOX_STATE));
}

/* ...close GUI layer */
static void __close_gui(gui_menu_item_t *item, widget_data_t *widget)
{
    gui_t      *gui = &__gui;

    /* ...toggle debug output status */
    TRACE(INFO, _b("GUI close command received"));

    /* ...just disable GUI layer */
    gui_controls_enable(gui, 0);
}

/* ...menu items */
static gui_menu_item_t __menu_items[] = {
//    [__MENU_ITEM_OUTPUT] = {
//        .text = "Surround view",
//        .flags = GUI_MENU_ITEM_CHECKBOX_ENABLED,
//        .select = __sview_scene,
//    },
    [__MENU_ITEM_LIVE] = {
        .text = "Live capturing",
        .flags = GUI_MENU_ITEM_CHECKBOX_ENABLED,
        .select = __sview_live,
    },
    [__MENU_ITEM_DUMMY] = {
        .text = "",
        .select = __dummy_item,
    },
    [__MENU_ITEM_SPHERE] = {
        .text = "Sphere projection",
        .flags = GUI_MENU_ITEM_CHECKBOX_ENABLED,
        .select = __sview_sphere,
    },
    [__MENU_ITEM_ADJUST] = {
        .text = "Adjust Bird View",
        .select = __sview_adjust_bv,
    },
	[__MENU_ITEM_CALIBRATE] = {
		.text = "Calibrate cam",
		.select = __sview_calibrate_cam,
	},
	[__MENU_ITEM_LOAD_CALIBRATION] = {
		.text = "Load calibration",
		.select = __sview_load_calibration,
	},
    [__MENU_ITEM_DEBUG] = {
        .text = "Debugging output",
        .flags = GUI_MENU_ITEM_CHECKBOX,
        .select = __debug_output,
    },
    [__MENU_ITEM_TOP] = {
        .text = "Top view",
        .select = __sview_top_view,
    },
    [__MENU_ITEM_BACK45] = {
        .text = "Back-45 view",
        .select = __sview_back45_view,
    },
    [__MENU_ITEM_CLOSE] = {
        .text = "Close menu",
        .select = __close_gui,
    },
	[__MENU_ITEM_ESC] = {
		.text = "Esc",
		.select = __sview_escape,
	},
};

/* ...menu widget initialized */    
static int __menu_init(widget_data_t *widget, void *cdata)
{
    gui_t      *gui = cdata;
    menu_t     *menu = &gui->menu;
    int         w = widget_get_width(widget);
    int         h = widget_get_height(widget);
    u32         i;
    /* ...initialize generic menu */
    gui_menu_init(&menu->base, widget);
    
    /* ...add boolean items */
    for (i = 0; i < sizeof(__menu_items) / sizeof(__menu_items[0]); i++)
    {
        /* TODO: consider refactoring */
        if (!app_has_multiple_sources(gui->app)) {
            if( i == __MENU_ITEM_LIVE)
                continue;
        }
        gui_menu_item_add(&menu->base, &__menu_items[i]);
    }

    /* ...create gradient pattern */
    menu->pattern = cairo_pattern_create_linear(0, 0, 0, h);
    cairo_pattern_add_color_stop_rgb(menu->pattern, 1, 0, 0, 0.8);
    cairo_pattern_add_color_stop_rgb(menu->pattern, 0, 0.5, 0.5, 0.5);
    //cairo_pattern_add_color_stop_rgb(menu->pattern, 0, 0.1, 0.1, 1);

    /* ...default parameters initialization? */
    TRACE(INIT, _b("menu initialized (%u*%u, pattern=%p (%s))"), w, h, menu->pattern, cairo_status_to_string(cairo_pattern_status(menu->pattern)));

    return 0;
}

/*******************************************************************************
 * Module entry point
 ******************************************************************************/

/* ...menu widget info */
static widget_info_t    menu_info = {
    .init = __menu_init,
    .draw = __menu_draw,
    .event = __menu_input,
};

/* ...module initialization structure */
widget_data_t * gui_create(window_data_t *window, app_data_t *app)
{
    gui_t          *gui = &__gui;
    int             w, h;
    widget_data_t  *widget;
    
    /* ...create menu widget */
    CHK_ERR(gui->menu.base.widget == NULL, (errno = EBUSY, NULL));

    /* ...get window viewport data */
    window_get_viewport(window, &w, &h);

    /* ...position menu widget in the center */
    menu_info.width = w / 2, menu_info.height = 48 * sizeof(__menu_items) / sizeof(__menu_items[0]);
    menu_info.top = (h - menu_info.height) / 2, menu_info.left = (w - menu_info.width) / 2;

    /* ...save main window handle */
    gui->window = window;

    /* ...save application data */
    gui->app = app;

    /* ...create main menu widget */
    CHK_ERR(widget = widget_create(window, &menu_info, gui), NULL);

    /* ...create fadeout watchdog timeout (place into default mainloop context) */
    if ((gui->fadeout = timer_source_create(gui_fadeout_timeout, gui, NULL, NULL)) == NULL)
    {
        TRACE(ERROR, _x("failed to create timer source: %m"));
        goto error_widget;
    }

    /* ...long-touch timer creation */
    if ((gui->touch_timer = timer_source_create(gui_touch_timeout, gui, NULL, NULL)) == NULL)
    {
        TRACE(ERROR, _x("failed to create timer source: %m"));
        goto error_widget;
    }

    TRACE(INIT, _b("GUI layer initialized"));

    return widget;

error_widget:
    /* ...destroy menu widget */
    widget_destroy(widget);
    
    /* ...mark layer is not initialized */
    gui->menu.base.widget = NULL;
    
    return NULL;
}
