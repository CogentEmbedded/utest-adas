/*******************************************************************************
 * utest-debug.h
 *
 * Debugging interface for UTEST application
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

#include <time.h>
#include <limits.h>

/*******************************************************************************
 * Auxiliary macros
 ******************************************************************************/

#ifndef offset_of
#define offset_of(type, member)         \
    ((int)(intptr_t)&(((const type *)(0))->member))
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((void *)(ptr) - offset_of(type, member)))
#endif

/*******************************************************************************
 * Bug check for constant conditions (file scope)
 ******************************************************************************/

#define __C_BUG(n)      __C_BUG2(n)
#define __C_BUG2(n)     __c_bug_##n
#define C_BUG(expr)     typedef char __C_BUG(__LINE__)[(expr) ? -1 : 1]

/*******************************************************************************
 * Compilation-time types control
 ******************************************************************************/

#if UTEST_DEBUG
#define __C_TYPE_CONTROL(d, type)       ((void) ((d) != (type*) 0))
#else
#define __C_TYPE_CONTROL(d, type)       ((void) 0)
#endif

/*******************************************************************************
 * Unused variable
 ******************************************************************************/

#define C_UNUSED(v)                     (void)(0 ? (v) = (v), 1 : 0)

/*******************************************************************************
 * Auxiliary macros
 ******************************************************************************/

/* ...define a stub for unused declarator */
#define __utest_stub(tag, line)        __utest_stub2(tag, line)
#define __utest_stub2(tag, line)       typedef int __utest_##tag##_##line

/* ...convert anything into string */
#define __utest_string(x)              __utest_string2(x)
#define __utest_string2(x)             #x

/*******************************************************************************
 * Tracing facility
 ******************************************************************************/

extern int LOG_LEVEL;

enum
{
	LOG_1	 		= 0,
	LOG_ERROR 		= 0,
	LOG_INIT 		= 1,
	LOG_INFO 		= 2,
	LOG_WARNING		= 2,
	LOG_PROCESS		= 3,
	LOG_EVENT 		= 4,
	LOG_PERFORMANCE = 4,
	LOG_BUFFER 		= 5,
	LOG_DEBUG 		= 5,
	LOG_BMCA 		= 6,
	LOG_RX	 		= 6,
	LOG_SM 			= 6,
	LOG_TIME 		= 6,
	LOG_TX	 		= 6,
	LOG_SYNC 		= 6,
	LOG_PDELAY 		= 6,
	LOG_INFLIGHT	= 6,
	LOG_DUMP		= 6,

	LOG_0 		= INT_MAX,
};

#if UTEST_TRACE

/* ...tracing to communication processor */
extern int  _trace(const char *format, ...) __attribute__((format (printf, 1, 2)));

/* ...tracing facility initialization */
extern void _trace_init(const char *banner);

/* ...initialize tracing facility */
#define TRACE_INIT(banner)              (_trace_init(banner))

/* ...trace tag definition */
#define TRACE_TAG(tag, on)              enum { __utest_trace_##tag = on }

/* ...check if the trace tag is enabled */
#define TRACE_CFG(tag)                  (__utest_trace_##tag)

/* ...tagged tracing primitive */
//#define TRACE(tag, fmt, ...)            (void)(__utest_trace_##tag ? __utest_trace(tag, __utest_format##fmt, ## __VA_ARGS__), 1 : 0)

#define TRACE(tag, fmt, ...)            (void)((__utest_trace_##tag && LOG_##tag <= LOG_LEVEL)  ? __utest_trace2(tag, __utest_format##fmt, ## __VA_ARGS__), 1 : 0)

#define __utest_trace2(fmt, ...)                                   \
({                                                                  \
    int  __oldstate;                                                \
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &__oldstate);    \
    __utest_trace(fmt, ##__VA_ARGS__);                             \
    pthread_setcancelstate(__oldstate, NULL);                       \
})
    
#define __trace__(var)                  __attribute__ ((unused)) var

/*******************************************************************************
 * Tagged tracing formats
 ******************************************************************************/

/* ...tracing primitive */
#define __utest_trace(tag, fmt, ...)   \
    ({ __attribute__((unused)) const char *__utest_tag = #tag; _trace(fmt, ## __VA_ARGS__); })

/* ...just a format string */
#define __utest_format_n(fmt)          fmt

/* ...module tag and trace tag shown */
#define __utest_format_b(fmt)          "%x:[%s.%s] " fmt, (unsigned)pthread_self(), __utest_string(MODULE_TAG), __utest_tag

/* ...module tag, trace tag, file name and line shown */
#define __utest_format_x(fmt)          "%x:[%s.%s] - %s@%d - " fmt, (unsigned)pthread_self(), __utest_string(MODULE_TAG), __utest_tag, __FILE__, __LINE__

/*******************************************************************************
 * Globally defined tags
 ******************************************************************************/

/* ...unconditionally OFF */
TRACE_TAG(0, 0);

/* ...unconditionally ON */
TRACE_TAG(1, 1);

/* ...error output - on by default */
TRACE_TAG(ERROR, 1);

/* ...warning output - on by default */
TRACE_TAG(WARNING, 1);

#else

#define TRACE_INIT(banner)              (void)0
#define TRACE_TAG(tag, on)              __utest_stub(trace_##tag, __LINE__)
#define TRACE(tag, fmt, ...)            (void)0
#define __utest_trace(tag, fmt, ...)   (void)0

#endif  /* UTEST_TRACE */

/*******************************************************************************
 * Bugchecks
 ******************************************************************************/

#if UTEST_DEBUG

/* ...run-time bugcheck */
#define BUG(cond, fmt, ...)                                         \
do                                                                  \
{                                                                   \
    if (cond)                                                       \
    {                                                               \
        /* ...output message */                                     \
        __utest_trace(BUG, __utest_format##fmt, ## __VA_ARGS__);  \
                                                                    \
        /* ...and die (tbd) */                                      \
        abort();                                                    \
    }                                                               \
}                                                                   \
while (0)

#else
#define BUG(cond, fmt, ...)             (void)0
#endif  /* UTEST_DEBUG */

/*******************************************************************************
 * Run-time error processing
 ******************************************************************************/

/* ...check the API call succeeds */
#define UTEST_CHK_API(cond)                            \
({                                                      \
    int __ret;                                          \
                                                        \
    if ((__ret = (int)(cond)) < 0)                      \
    {                                                   \
        TRACE(ERROR, _x("API error: %d"), __ret);       \
        return __ret;                                   \
    }                                                   \
    __ret;                                              \
})

/* ...check the condition is true */
#define UTEST_CHK_ERR(cond, error)                     \
({                                                      \
    int __ret;                                          \
                                                        \
    if (!(__ret = (int)(intptr_t)(cond)))               \
    {                                                   \
        TRACE(ERROR, _x("check failed: %d"), __ret);    \
        return (error);                                 \
    }                                                   \
    __ret;                                              \
})

/*******************************************************************************
 * Performance counters
 ******************************************************************************/

/* ...retrieve CPU cycles count - due to thread migration, use generic clock interface */
static inline u32 __get_cpu_cycles(void)
{
    struct timespec     ts;

    /* ...retrieve value of monotonic clock */
    clock_gettime(CLOCK_MONOTONIC, &ts);

    /* ...translate value into milliseconds (ignore wrap-around) */
    return (u32)((u64)ts.tv_sec * 1000000000ULL + ts.tv_nsec);
}

static inline u32 __get_time_usec(void)
{
    struct timespec     ts;

    /* ...retrieve value of monotonic clock */
    clock_gettime(CLOCK_MONOTONIC, &ts);

    /* ...translate value into milliseconds (ignore wrap-around) */
    return (u32)((u64)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000);
}

/*******************************************************************************
 * Capturing facility
 ******************************************************************************/

/* ...capture tag definitions */
#define CAPTURE_TAG(tag, type, cfg)                                 \
    __CAPTURE_TAG_EX(tag, type)                                     \
    enum { __UTEST_CAPTURE_##tag = (cfg) }

#define CAPTURE_CFG(tag)                                            \
    (__UTEST_CAPTURE_##tag)

/* ...capturing macro */
#define CAPTURE(tag, x)                 \
    (void)(UTEST_CAPTURE && __UTEST_CAPTURE_##tag ? __CAPTURE(tag, x) : 0)

/* ...capturing macro */
#define __CAPTURE(tag, x)               \
    utest_capture_##tag(x)

#ifndef __UTEST_DEBUG_DIR
#define __UTEST_DEBUG_DIR "/tmp/"
#endif

/* ...capturing tag definition */
#define __CAPTURE_TAG_EX(tag, type)                                         \
__attribute__((unused)) static void utest_capture_##tag (type x)           \
{                                                                           \
    static  FILE *f = NULL;                                                 \
                                                                            \
    /* ...open file as needed */                                            \
    if (!f && (f = fopen(__UTEST_DEBUG_DIR #tag "." #type, "wb")) == NULL) \
    {                                                                       \
        TRACE(ERROR, _x("failed to open tag file " #tag ": %m"));           \
    }                                                                       \
                                                                            \
    /* ...write portion of data */                                          \
    if (fwrite((void*)&x, sizeof(type), 1, f) != 1)                         \
    {                                                                       \
        TRACE(ERROR, _x("couldn't write into "#tag ": %m"));                \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        fflush(f);                                                          \
    }                                                                       \
}

/*******************************************************************************
 * Performance monitoring support
 ******************************************************************************/

/* ...performance monitor tag definition */
#define PM_TAG(tag, cfg)                                \
    __PM_TAG_EX(tag)                                    \
    enum { __UTEST_PM_##tag = (cfg) }

/* ...performance monitor tag enable status */
#define PM_CFG(tag)                                     \
    (__UTEST_PM_##tag)

/* ...performance monitor command */
#define PM(tag, cmd)                    \
    (void)(UTEST_PM && __UTEST_PM_##tag ? __PM(tag, cmd) : 0)

/*******************************************************************************
 * Performance monitoring
 ******************************************************************************/

/* ...performance counter setting */
#define __PM(tag, cmd)                  \
    utest_pm_##tag(cmd)

/* ...performance counter tag definition */
#define __PM_TAG_EX(tag)                                        \
CAPTURE_TAG(PM_##tag, u32, 1);                                  \
__attribute__((unused)) static void utest_pm_##tag(int cmd)    \
{                                                               \
    static u32      delta = 0;                                  \
    u32             ts;                                         \
                                                                \
    /* ...get the timestamp */                                  \
    ts = __get_cpu_cycles();                                    \
                                                                \
    /* ...process particular command type */                    \
    if (cmd)                                                    \
    {                                                           \
        /* ...calculate delta */                                \
        delta = ts - delta;                                     \
                                                                \
        /* ...save the processing time */                       \
        __CAPTURE(PM_##tag, delta);                             \
    }                                                           \
    else                                                        \
    {                                                           \
        /* ...save timestamp */                                 \
        delta = ts;                                             \
    }                                                           \
}

/*******************************************************************************
 * Auxiliary helpers
 ******************************************************************************/

#define CHK_API(cond)                           \
({                                              \
    int  __r = (cond);                          \
    if (__r < 0)                                \
        return TRACE(ERROR, _x("%m")), __r;     \
    __r;                                        \
})

#define CHK_ERR(cond, err)                                  \
({                                                          \
    if (!(cond))                                            \
        return TRACE(ERROR, _x("condition failed")), (err); \
    1;                                                      \
})

#define xmalloc(size, err)                                                                  \
({                                                                                          \
    void *__p = malloc((size));                                                             \
    if (!__p)                                                                               \
        return errno = ENOMEM, TRACE(ERROR, _x("alloc failed (%u bytes)"), (size)), (err);  \
    __p;                                                                                    \
})
