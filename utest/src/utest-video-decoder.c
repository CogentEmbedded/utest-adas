/*******************************************************************************
 * utest-video-decoder.c
 *
 *  Video file decoding support
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

#define MODULE_TAG                      VIDEO

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "utest.h"
#include "utest-common.h"
#include "utest-camera.h"
#include "utest-vsink.h"
#include "utest-display-wayland.h"
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <glib-2.0/glib/gslice.h>

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(DEBUG, 0);
TRACE_TAG(BUFFER, 0);

/*******************************************************************************
 * Local typedefs
 ******************************************************************************/

/* ...camera data */
typedef struct video_stream
{
    /* ...bin containing all internal nodes */
    GstElement                 *bin;

    /* ...application callbacks */
    const camera_callback_t    *cb;

    /* ...application data */
    void                       *cdata;
    
    /* ...camera identifier */
    int                         id;
    
}   video_stream_t;

/*******************************************************************************
 * Video sink callbacks
 ******************************************************************************/

/* ...buffer allocation callback */
static int __video_buffer_allocate(video_sink_t *sink, GstBuffer *buffer, void *data)
{
    video_stream_t  *stream = data;

    TRACE(INFO, _b("buffer allocated (%p)"), buffer);

    /* ...pass allocation request to the application */
    return CHK_API(stream->cb->allocate(stream->cdata, buffer));
}

/* ...data availability callback */
static int __video_buffer_process(video_sink_t *sink, GstBuffer *buffer, void *data)
{
    video_stream_t  *stream = data;
    int              id = stream->id;
    vsink_meta_t    *meta = gst_buffer_get_vsink_meta(buffer);   

    /* ...make sure we have a valid metadata */
    CHK_ERR(meta, -EPIPE);

    /* ...pass buffer to decoder */
    CHK_API(stream->cb->process(stream->cdata, id, buffer));

    return 0;
}

/* ...video-sink callbacks */
static const vsink_callback_t   vsink_cb = {
    .allocate = __video_buffer_allocate,
    .process = __video_buffer_process,
};   

/*******************************************************************************
 * Auxiliary graph control functions
 ******************************************************************************/

/* ...custom destructor function */
static void __stream_destructor(gpointer data, GObject *obj)
{
    video_stream_t     *stream = data;

    /* ...deallocate stream data */
    free(stream);

    TRACE(INIT, _b("video-stream %p destroyed"), stream);
}

/* ...decodebin dynamic pad registration callback */
static void decodebin_pad_added(GstElement *decodebin, GstPad *pad, gpointer data)
{
    video_stream_t *stream = data;
    GstElement     *bin = stream->bin;
    GstCaps        *caps;
    GstStructure   *str;
    const gchar    *name;
       
    /* ...check media type of newly created pad */
    caps = gst_pad_query_caps(pad, NULL);
    str = gst_caps_get_structure(caps, 0);
    name = gst_structure_get_name(str);

    TRACE(INFO, _b("discovered pad: '%s'"), name);

    /* ...connect only raw video pads */
    if (!g_strcmp0(name, "video/x-raw"))
    {
        GstElement     *sink;
        GstPad         *_pad;
        GstVideoInfo    vinfo;
        
        /* ...parse video-stream parameters */
        gst_video_info_from_caps(&vinfo, caps);

        TRACE(INFO, _b("video-info: %u * %u, format: %s"), vinfo.width, vinfo.height, vinfo.finfo->name);

        /* ...ignore media if it's not NV12 format */
        if (strcmp(vinfo.finfo->name, "NV12") != 0)
        {
            TRACE(INFO, _b("ignore non-supported video format: %s"), vinfo.finfo->name);
            goto out;
        }
        
        /* ...connect custom video sink */
        sink = video_sink_element(video_sink_create(caps, &vsink_cb, stream));

        /* ...make sink synchronized to the timestamps */
        g_object_set(GST_OBJECT(sink), "sync", TRUE, NULL);
        
        /* ...add sink to a stream bin */
        gst_bin_add(GST_BIN(bin), sink);
        /* ...link pad to an element */
        _pad = gst_element_get_static_pad(sink, "sink");
        gst_pad_link(pad, _pad);
        gst_object_unref(_pad);

        /* ...synchronize sink state with a pipeline */
        gst_element_sync_state_with_parent(sink);
        
        TRACE(INFO, _b("added video-sink to a pipe"));
    }
    else
    {
        TRACE(INFO, _b("ignore media: %s"), name);
    }

out:
    /* ...release capabilities structure */
    gst_caps_unref(caps);
}



/*******************************************************************************
 * Camera bin initialization
 ******************************************************************************/

GstElement * video_stream_create(const camera_callback_t *cb, void *cdata, int n)
{
    video_stream_t     *stream;
    GstElement         *bin, *source, *decoder;
    
    /* ...create single bin object that hosts all cameras */
    CHK_ERR(bin = gst_bin_new("video-stream::bin"), (errno = ENOMEM, NULL));
    int i;
    for (i=0; i < n; i++) {
        /* ...allocate new video stream data */
        CHK_ERR(stream = malloc(sizeof(*stream)), (errno = ENOMEM, NULL));
        const char         *filename = video_stream_get_file(i);
        /* ...save stream data */
        stream->bin = bin;

        /* ...reset stream id */
        stream->id = i;

        /* ...save stream callback data */
        stream->cb = cb, stream->cdata = cdata;
        /* ...create graph nodes */
        source = gst_element_factory_make("filesrc", NULL);
        decoder = gst_element_factory_make("decodebin", NULL);
        g_assert(source && decoder);
        /* ...add nodes into the bin */
        gst_bin_add_many(GST_BIN(bin), source, decoder, NULL);
        gst_element_link(source, decoder);
        /* ...specify a callback for connection with decodebin */
        g_signal_connect_data(decoder, "pad-added", G_CALLBACK(decodebin_pad_added),
                                stream, NULL, 0);
        /* ...set video file name */
        g_object_set(source, "location", filename, NULL);

        /* ...set custom destructor */
        g_object_weak_ref(G_OBJECT(bin), __stream_destructor, stream);

        TRACE(INIT, _b("video-stream created"));
    }
    return bin;
}
