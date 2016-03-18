/*******************************************************************************
 * utest.h
 *
 * Utest main header
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

#ifdef  __UTEST_H
#error  "utest.h included more than once"
#endif

#define __UTEST_H

/*******************************************************************************
 * Configuration macros (tbd)
 ******************************************************************************/

/* ...enable bugchecking */
#define UTEST_DEBUG                    1

/* ...enable tracing */
#define UTEST_TRACE                    1

/* ...enable capturing */
#define UTEST_CAPTURE                  0

/* ...enable performance monitor counters collection */
#define UTEST_PM                       1

/*******************************************************************************
 * Includes
 ******************************************************************************/

/* ...primitive typedefs */
#include "utest-types.h"

/* ...standard headers */
#include "utest-debug.h"

#ifdef COMPILE_WITH_PRIVATE
/* ...private headers...*/
#include "utest-private.h"
#endif

