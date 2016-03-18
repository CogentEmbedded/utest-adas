/*******************************************************************************
 * utest-main.c
 *
 * Surround-view unit-test
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

#define MODULE_TAG                      MAIN

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include "utest.h"
#include "utest-common.h"
#include "utest-app.h"
#include <getopt.h>

#ifdef COMPILE_WITH_PRIVATE
#include "utest-camera-mjpeg.h"
#include "utest-netif.h"
#endif

/*******************************************************************************
 * Tracing configuration
 ******************************************************************************/

TRACE_TAG(INIT, 1);
TRACE_TAG(INFO, 1);
TRACE_TAG(DEBUG, 0);

/*******************************************************************************
 * Global variables definitions
 ******************************************************************************/

/* ...output devices for main / auxiliary windows */
int                 __output_main = 0, __output_transform = 0;

#ifdef ENABLE_CAMERA_MJPEG
/* ...pointer to effective AVB MJPEG cameras MAC addresses */
u8                (*camera_mac_address)[6];
#endif

/* ...log level (looks ugly) */
int                 LOG_LEVEL = 1;

/*******************************************************************************
 * Local variables
 ******************************************************************************/

/* ...live VIN cameras capturing flag */
static int              vin;

#ifdef ENABLE_CAMERA_MJPEG
/* ...network interface (live capturing over EthAVB) */
static netif_data_t    netif;

/* ...network interface for live capturing */
static char            *iface = NULL;
#endif

/* ...live source processing */
static int              __live_source;

/* ...global configuration data */
static sview_cfg_t      __sv_cfg = { .pixformat = GST_VIDEO_FORMAT_NV12,
                                     .config_path = "config.xml"};

#ifdef ENABLE_OBJDET
/* ...LDW configuration file */
char                   *ldwConfigurationFilePath;
#endif

/* ...jpeg decoder device name  */
char                   *jpu_dev_name = "/dev/video1";

/* ...default joystick device name  */
char                   *joystick_dev_name = "/dev/input/js0";

/* ...application flags */
static int flags;

/*******************************************************************************
 * Tracks parsing
 ******************************************************************************/

/* ...track list head */
static track_list_t     __sv_tracks; 
/* ...current track position */
static track_list_t    *__sv_current;
/* ...live track */
static track_desc_t    *__sv_live;

#ifdef ENABLE_OBJDET
/* ...track list head */
static track_list_t     __od_tracks;
/* ...current track position */
static track_list_t    *__od_current;
/* ...live track */
static track_desc_t    *__od_live;
#endif


/*******************************************************************************
 * Track reading interface
 ******************************************************************************/

/* ...switch to next track */
static inline track_list_t * track_next(track_list_t *head, track_list_t *track)
{
    BUG(head->next == head, _x("list is empty"));

    if (track)
    {
        /* ..switch to the next track in the list */
        return ((track = track->next) == head ? head->next : track);
    }
    else
    {
        /* ...no list position defined yet */
        return head->next;
    }
}

/* ...switch to previous track */
static inline track_list_t * track_prev(track_list_t *head, track_list_t *track)
{
    BUG(head->prev == head, _x("list is empty"));
    
    /* ..switch to the previous track in the list */
    if (track)
    {
        return ((track = track->prev) == head ? head->prev : track);
    }
    else
    {
        return head->prev;
    }
}

/* ...create new track */
static inline track_desc_t * track_create(track_list_t *head, int type)
{
    track_desc_t    *track;
    
    /* ...allocate a structure */
    CHK_ERR(track = calloc(1, sizeof(*track)), (errno = ENOMEM, NULL));

    if (head)
    {
        /* ...insert track into the global list */
        track->list.next = head, track->list.prev = head->prev;

        /* ...adjust sentinel node */
        head->prev->next = &track->list, head->prev = &track->list;
    }
    else
    {
        track->list.next = track->list.prev = &track->list;
    }

    /* ...set track type */
    track->type = type;

    return track;
}

/* ...return next surround-view track */
track_desc_t * sview_track_next(void)
{
    return (track_desc_t *)(__sv_current = track_next(&__sv_tracks, __sv_current));
}

/* ...return previous surround-view track */
track_desc_t * sview_track_prev(void)
{
    return (track_desc_t *)(__sv_current = track_prev(&__sv_tracks, __sv_current));
}

/* ...return current surround-view track */
track_desc_t * sview_track_current(void)
{
    return (track_desc_t *)__sv_current;
}

/* ...return live surround-view track */
track_desc_t * sview_track_live(void)
{
    return __sv_live;
}

#ifdef ENABLE_OBJDET
/* ...return next object-detection track */
track_desc_t * objdet_track_next(void)
{
    return (track_desc_t *)(__od_current = track_next(&__od_tracks, __od_current));
}

/* ...return previous object-detection track */
track_desc_t * objdet_track_prev(void)
{
    return (track_desc_t *)(__od_current = track_prev(&__od_tracks, __od_current));
}

/* ...return current object-detection track */
track_desc_t * objdet_track_current(void)
{
    return (track_desc_t *)__od_current;
}

/* ...return live object-detection track */
track_desc_t * objdet_track_live(void)
{
    return __od_live;
}

/* ...send CAN message to the application */
static void camera_source_can(void *cdata, u32 can_id, u8 *msg, u8 dlc, u64 ts)
{
    app_data_t   *app = cdata;

    app_can_message_receive(app, can_id, msg, dlc, ts);
}

#endif

#ifdef ENABLE_CAMERA_MJPEG

/* ...camera initialization function */
camera_data_t * mjpeg_camera_create(int id, GstBuffer * (*get_buffer)(void *, int), void *cdata)
{
    extern camera_data_t * __camera_mjpeg_create(netif_data_t *netif, int id, u8 *da, u8 *sa, u16 vlan, GstBuffer * (*get_buffer)(void *, int), void *cdata);
 
    /* ...validate camera id */
    CHK_ERR((unsigned)id < CAMERAS_NUMBER, (errno = ENOENT, NULL));

    /* ...create AVB camera object */
    return __camera_mjpeg_create((__live_source ? &netif : NULL), id, NULL, camera_mac_address[id], (u16)0x56, get_buffer, cdata);
}

/* ...default cameras MAC addresses */
static u8               default_mac_addresses[CAMERAS_NUMBER][6];

/*******************************************************************************
 * Offline network interface callback structure
 ******************************************************************************/

static void camera_source_eos(void *cdata)
{
    app_data_t   *app = cdata;
    
    TRACE(INFO, _b("end-of-stream signalled"));

    /* ...pass end-of-stream to the application */
    app_eos(app);
}

/* ...send PDU to the camera */
static void camera_source_pdu(void *cdata, int id, u8 *pdu, u16 len, u64 ts)
{
    app_data_t   *app = cdata;

    /* ...pass packet to the camera bin */
    app_packet_receive(app, id, pdu, len, ts);
}

/* ...camera source callback structure */
static camera_source_callback_t camera_source_cb = {
    .eos = camera_source_eos,
    .pdu = camera_source_pdu,
#ifdef OBJDET_H
    .can = camera_source_can,
#endif
};

#endif

/*******************************************************************************
 * Live capturing from VIN cameras
 ******************************************************************************/

/* ...default V4L2 device names */
char * vin_devices[CAMERAS_NUMBER] = {
    "/dev/video0",
    "/dev/video1",
    "/dev/video2",
    "/dev/video3",
};

/* ...VIN camera set creation for a object-detection */
static GstElement * __camera_vin_create(const camera_callback_t *cb, void *cdata, int n)
{
    return camera_vin_create(cb, cdata, vin_devices, n);
}


/*******************************************************************************
 * Parameters parsing
 ******************************************************************************/

static inline void vin_addresses_to_name(char* str[4], char *vin[4])
{
	int i, j;
	for(i = 0; i < 4; i++)
	{
		if(!str[i])
		{
			str[i] = malloc(40);
			memset(str[i], 0, 40);
		}

		memcpy(str[i], vin[i], strlen(vin[i]));
		for(j = 0; j < (int)strlen(vin[i]); j++)
			if(	str[i][j]=='/')
				str[i][j] = '_';
	}
}

/* ...parse VIN device names */
static inline int parse_vin_devices(char *str, char **name, int n)
{
    char   *s;
    
    for (s = strtok(str, ","); n > 0 && s; n--, s = strtok(NULL, ","))
    {
        /* ...copy a string */
        *name++ = strdup(s);
    }

    /* ...make sure we have parsed all addresses */
    CHK_ERR(n == 0, -EINVAL);

    return 0;
}

/* ...parse video stream file names */
static inline int parse_video_file_names(char *str, char **name, int n)
{
    char   *s;
    
    for (s = strtok(str, ","); n > 0 && s; n--, s = strtok(NULL, ","))
    {
        /* ...copy a string */
        *name++ = strdup(s);
    }

    /* ...make sure we have parsed all addresses */
    CHK_ERR(n == 0, -EINVAL);

    return 0;
}

static inline void mac_addresses_to_name(char* str[4], u8 addr[4][6]) {
    int i;
    for (i = 0; i < 4; i++) {
        if (!str[i]) {
            str[i] = malloc(40);
            memset(str[i], 0, 40);
        }

        sprintf(str[i], "%02x-%02x-%02x-%02x-%02x-%02x", addr[i][0], addr[i][1], addr[i][2],
                addr[i][3], addr[i][4], addr[i][5]);
    }
}

/* ...MAC address parsing */
static inline int parse_mac_addresses(char *str, u8(*addr)[6], int n) {
    char *s;

    for (s = strtok(str, ","); n > 0 && s; n--, s = strtok(NULL, ",")) {
        u8 *b = *addr++;

        /* ...parse MAC address from the string */
        CHK_ERR(sscanf(s, "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX", b, b + 1, b + 2, b + 3, b + 4, b + 5) == 6, -EINVAL);
    }

    /* ...make sure we have parsed all addresses */
    CHK_ERR(n == 0, -EINVAL);

    return 0;
}

/* ...configuration file parsing */
static int parse_cfg_file(char *name)
{
    track_desc_t   *track = NULL;
//    int             cameras = 0;
    int             num = 0;
    FILE           *f;

    /* ...open file handle */
    CHK_ERR(f = fopen(name, "rt"), -errno);

    /* ...parse tracks */
    while (1)
    {
        char    buf[4096], *s;
        
        /* ...get next string from the file */
        if (fgets(buf, sizeof(buf) - 1, f) == NULL)     break;

        /* ...strip trailing newline */
        ((s = strchr(buf, '\n')) != NULL ? *s = '\0' : 0);
        
        /* ...parse string */
        if (!strcmp(buf, "[sv-track]"))
        {
            /* ...start new surround-view track */
            CHK_ERR(track = track_create(&__sv_tracks, 0), -errno);
            track->pixformat = GST_VIDEO_FORMAT_NV12;
            /* ...advance tracks number */
            num++;

            /* ...mark we have a surround-view track */
//            cameras = CAMERAS_NUMBER;
            
            flags |= APP_FLAG_SVIEW;
            flags |= APP_FLAG_FILE;
            
            continue;
        }
#ifdef ENABLE_OBJDET
        else if (!strcmp(buf, "[od-track]"))
        {
            /* ...start new track for object-detection */
            CHK_ERR(track = track_create(&__od_tracks, 1), -(errno = ENOMEM));

            /* ...set fake detector configuration */
            CHK_ERR(track->od_cfg = calloc(1, sizeof(*track->od_cfg)), -(errno = ENOMEM));
            
            /* ...advance tracks number */
            num++;

            /* ...mark we have a frontal-camera track */
            cameras = 1;
            
            continue;
        }
#endif
        else if (!track)
        {
            /* ...no current track defined; skip line */
            continue;
        }
       
        /* ...parse track section */
        if (!strncmp(buf, "file=", 5))
        {
            CHK_ERR(track->file = strdup(buf + 5), -(errno = ENOMEM));
        }
        else if (!strncmp(buf, "info=", 5))
        {
            CHK_ERR(track->info = strdup(buf + 5), -(errno = ENOMEM));
        }
        else if (!strncmp(buf, "mac=", 4))
        {
            /* ...parse MAC addresses */
            CHK_API(parse_mac_addresses(buf + 4, track->mac, CAMERAS_NUMBER));
           mac_addresses_to_name(track->camera_names, track->mac);
        }
        else if (!strncmp(buf, "cfg=", 4))
        {
            CHK_ERR(track->camera_cfg = strdup(buf + 4), -(errno = ENOMEM));
        }
    }

    TRACE(INIT, _b("configuration file parsed (%u tracks)"), num);

    return num;
}

/* ...command-line options */
static const struct option    options[] = {
    {   "debug",    required_argument,  NULL,   'd' },
    {   "iface",    required_argument,  NULL,   'i' },
    {   "mac",      required_argument,  NULL,   'm' },
    {   "vin",      required_argument,  NULL,   'v' },
    {   "camera",   required_argument,  NULL,   'l' },
    {   "cfg",      required_argument,  NULL,   'c' },
    {   "output",   required_argument,  NULL,   'o' },
    {   "transform",required_argument,  NULL,   't' },
	{   "jpu",      required_argument,  NULL,   'j' },
    {   "js",       required_argument,  NULL,   'w' },

    /* ...svlib configuration options */
	{   "patternZoom",    required_argument,  NULL,   1 },
	{   "patternStep",    required_argument,  NULL,   2 },
	{   "patternRect",    required_argument,  NULL,   3 },
	{   "patternSize",    required_argument,  NULL,   4 },
	{   "patternSizeW",   required_argument,  NULL,   5 },
	{   "patternSizeH",   required_argument,  NULL,   6 },
	{   "patternGap",     required_argument,  NULL,   7 },
	{   "calibBoard",     	required_argument,  NULL, 8 },
	{   "calibSquare",     	required_argument,  NULL, 9 },
	{   "calibGrabInterval",required_argument,  NULL, 10 },
	{   "calibNumBoards",   required_argument,  NULL, 11 },
	{   "view",     	required_argument,  NULL, 12 },
        {   "calibMode",        no_argument,    NULL,   13 },
        {   "nonFisheyeCam",    no_argument,    NULL,   14 },
        {   "save",             no_argument,    NULL,   15 },

    /* ...object detection engine library configuration options - tbd */
    {   NULL,               0,                  NULL, 0 },
};

/* ...option parsing */
static int parse_cmdline(int argc, char **argv)
{
    sview_cfg_t    *cfg = &__sv_cfg;
    int             index = 0;
    int             opt;

    /* ...process command-line parameters */
    while ((opt = getopt_long(argc, argv, "d:i:m:v:c:o:t:j:w:l:", options, &index)) >= 0)
    {
        switch (opt)
        {
        case 'd':
            /* ...debug level */
            TRACE(INIT, _b("debug level: '%s'"), optarg);
            LOG_LEVEL = atoi(optarg);
            break;

#ifdef ENABLE_CAMERA_MJPEG          
        case 'i':
            /* ...short option - network interface */
            TRACE(INIT, _b("net interface: '%s'"), optarg);
            iface = optarg;
            break;

        case 'm':
            /* ...MAC address of network camera */
            TRACE(INIT, _b("MAC address: '%s'"), optarg);
            CHK_API(parse_mac_addresses(optarg, default_mac_addresses, CAMERAS_NUMBER));
            mac_addresses_to_name(cfg->cam_names, default_mac_addresses);
            cfg->pixformat = GST_VIDEO_FORMAT_NV12;
            break;
#endif          
        case 'v':
            /* ...VIN device name (for live capturing from frontal camera) */
            TRACE(INIT, _b("VIN devices: '%s'"), optarg);
            CHK_API(parse_vin_devices(optarg, vin_devices, CAMERAS_NUMBER));
            vin_addresses_to_name(cfg->cam_names, vin_devices);
            cfg->pixformat = GST_VIDEO_FORMAT_UYVY;
            vin = 1;
            break;

#ifdef ENABLE_OBJDET
        case 'l':
            /* ...camera configuration for a live capturing */
            TRACE(INIT, _b("camera-cfg: '%s'"), optarg);
            ldwConfigurationFilePath = optarg;
            break;
#endif

        case 'c':
            /* ...parse offline track configuration */
            TRACE(INIT, _b("read tracks from configuration file '%s'"), optarg);
            CHK_API(parse_cfg_file(optarg));
            break;

        case 'o':
            /* ...set global display output for a main window (surround-view scene) */
            __output_main = atoi(optarg);
            TRACE(INIT, _b("output for main window: %d"), __output_main);
            break;

#ifdef ENABLE_OBJDET            
        case 't':
            /* ...set global display output for a frontal camera */
            __output_transform = atoi(optarg);
            TRACE(INIT, _b("output transformation: %d"), __output_transform);
            break;
#endif
            
        case 'j':
            /* ...set default JPU decoder V4L2 device name */
            TRACE(INIT, _b("jpec decoder dev name : '%s'"), optarg);
            jpu_dev_name = optarg;
            break;

        case 'w':
            /* ...set default joystick device name */
            TRACE(INIT, _b("joystick device: '%s'"), optarg);
            joystick_dev_name = optarg;
            break;

		/*... beginning surround view cmd line parameters*/
        case 1:
			TRACE(INIT, _b("patternZoom: '%s'"), optarg);
			cfg->pattern_zoom = atof(optarg);
			break;

		case 2:
			TRACE(INIT, _b("patternStep: '%s'"), optarg);
			cfg->pattern_step = atoi(optarg);
			break;

		case 3:
			TRACE(INIT, _b("patternRect: '%s'"), optarg);

			if(sscanf(optarg, "%dx%d", &cfg->pattern_rect_w, &cfg->pattern_rect_h)  != 2)
			{
				TRACE(ERROR, _x("Wrong patternRect format. Example: --patternRect 444x444"));
				cfg->pattern_rect_w = cfg->pattern_rect_h = 0;
			}
			break;

		case 4:
			TRACE(INIT, _b("patternSize: '%s'"), optarg);

			if(sscanf(optarg, "%dx%d", &cfg->pattern_circles_hor_w, &cfg->pattern_circles_hor_h)  != 2)
			{
				TRACE(ERROR, _x("Wrong patternSize format. Example: --patternSize 10x4"));
				cfg->pattern_circles_hor_w = cfg->pattern_circles_hor_h = 0;
			}
			else
			{
				cfg->pattern_circles_vert_w = cfg->pattern_circles_hor_w;
				cfg->pattern_circles_vert_h = cfg->pattern_circles_hor_h;
			}
			break;

		case 5:
			TRACE(INIT, _b("patternSizeW: '%s'"), optarg);

			if(sscanf(optarg, "%dx%d", &cfg->pattern_circles_hor_w, &cfg->pattern_circles_hor_h)  != 2)
			{
				TRACE(ERROR, _x("Wrong patternSizeW format. Example: --patternSizeW 10x4"));
				cfg->pattern_circles_hor_w = cfg->pattern_circles_hor_h = 0;
			}
			break;

		case 6:
			TRACE(INIT, _b("patternSizeH: '%s'"), optarg);

			if(sscanf(optarg, "%dx%d", &cfg->pattern_circles_vert_w, &cfg->pattern_circles_vert_h)  != 2)
			{
				TRACE(ERROR, _x("Wrong patternSizeH format. Example: --patternSizeH 10x4"));
				cfg->pattern_circles_vert_w = cfg->pattern_circles_vert_h = 0;
			}
			break;

		case 7:
			TRACE(INIT, _b("patternGap: '%s'"), optarg);

			if(sscanf(optarg, "%dx%d", &cfg->pattern_gap_w, &cfg->pattern_gap_h)  != 2)
			{
				TRACE(ERROR, _x("Wrong patternGap format. Example: "
						"--patternGap 0x0 for solid pattern (default), "
						"--patternGap 150x150 for separated patter"));

				cfg->pattern_gap_w = cfg->pattern_gap_h = 0;
			}
			break;

		case 8:
			TRACE(INIT, _b("calibBoard: '%s'"), optarg);

			if(sscanf(optarg, "%dx%d", &cfg->calib_board_w, &cfg->calib_board_h)  != 2)
			{
				TRACE(ERROR, _x("Wrong calibBoard format. Example: --calibBoard 9x6"));
				cfg->calib_board_w = cfg->calib_board_h = 0;
			}
			break;

		case 9:
			TRACE(INIT, _b("calibSquare: '%s'"), optarg);
			cfg->calib_square = atof(optarg);
			break;

		case 10:
			TRACE(INIT, _b("calibGrabInterval: '%s'"), optarg);
			cfg->calib_grab_interval = atoi(optarg);
			break;

		case 11:
			TRACE(INIT, _b("calibNumBoards: '%s'"), optarg);
			cfg->calib_boards_required = atoi (optarg);
			break;

		case 12:
			TRACE (INIT, _b ("view: '%s'"), optarg);
			cfg->start_view = atoi (optarg);
			break;
		case 13:
			TRACE (INIT, _b ("calibMode ON"));
			cfg->calibration_mode = 1;
			break;

		case 14:
			TRACE (INIT, _b ("nonFisheyeCam ON"));
			cfg->non_fisheye_camera = 1;
			break;

		case 15:
			TRACE (INIT, _b ("save ON"));
			cfg->saveFrames = 1;
			break;

		default:
		return -EINVAL;
        }
    }
#ifdef ENABLE_CAMERA_MJPEG
    /* ...check we have found both live tracks */
    if (iface)
    {
        /* ...create live track descriptor */
        CHK_ERR(__sv_live = track_create(NULL, 0), -(errno = ENOMEM));

        /* ...set parameters */
        memcpy(__sv_live->mac, default_mac_addresses, sizeof(default_mac_addresses));
        __sv_live->camera_cfg = __sv_cfg.config_path;
        __sv_live->pixformat = GST_VIDEO_FORMAT_NV12;
        mac_addresses_to_name(__sv_live->camera_names, default_mac_addresses);
        __sv_live->camera_type = TRACK_CAMERA_TYPE_MJPEG;
        flags |= APP_FLAG_SVIEW;
        flags |= APP_FLAG_LIVE;
    }
#endif
    if (vin)
    {
    /* ...create live track descriptor */
        CHK_ERR(__sv_live = track_create(NULL, 0), -(errno = ENOMEM));
        __sv_live->camera_cfg = __sv_cfg.config_path;
        vin_addresses_to_name(__sv_live->camera_names, vin_devices);
        __sv_live->pixformat = GST_VIDEO_FORMAT_UYVY;
        __sv_live->camera_type = TRACK_CAMERA_TYPE_VIN;
        flags |= APP_FLAG_SVIEW;
        flags |= APP_FLAG_LIVE;
    }
    
#ifdef ENABLE_OBJDET
    if (!vin)
    {
        TRACE(ERROR, _x("live track for object-detection is missing"));
        return -1;
    }
    else
    {

        /* ...create live track descriptor */
        CHK_ERR(__od_live = track_create(NULL, 1), -(errno = ENOMEM));

        /* ...set parameters */
        __od_live->camera_cfg = strdup(ldwConfigurationFilePath);
        CHK_ERR(__od_live->od_cfg = calloc(1, sizeof(*__od_live->od_cfg)), -(errno = ENOMEM));

    }
    #endif
    return 0;
}

/*******************************************************************************
 * Offline replay thread
 ******************************************************************************/

/* ...video-stream initialization function */
//const char * video_stream_filename(void)
//{
//    return container_of(__sv_current, track_desc_t, list)->file;
//}

#ifdef ENABLE_CAMERA_MJPEG
/* ...play data captured in PCAP file */
static inline int playback_pcap(app_data_t *app, track_desc_t *track, int start)
{
    /* ...PCAP is allowed only for surround-view track */
    CHK_ERR(track->type == 0, -EINVAL);
    camera_mac_address = track->mac;
    if (start)
    {
        /* ...initialize camera bin */
        CHK_API(sview_camera_init(app, camera_mjpeg_create));
    
        /* ...start PCAP replay thread (we should get a control handle, I guess) */
        CHK_ERR(track->priv = pcap_replay(track->file, &camera_source_cb, app), -errno);
    }
    else
    {
        /* ...stop playback */
        pcap_stop(track->priv);
    }
    
    return 0;
}

/* ...play data captured in BLF file */
static inline int playback_blf(app_data_t *app, track_desc_t *track, int start)
{
    /* ...BLF is allowed only for surround-view track */
    CHK_ERR(track->type == 0, -EINVAL);

    if (start)
    {
        /* ...initialize camera bin */
        CHK_API(sview_camera_init(app, camera_mjpeg_create));
    
        /* ...start BLF replay thread */
        CHK_ERR(track->priv = blf_replay(track->file, &camera_source_cb, app), -errno);
    }
    else
    {
        /* ...stop BLF thread */
        blf_stop(track->priv);
    }

    return 0;
}
#endif

/* File names for mp4 replay */
char * file_names[CAMERAS_NUMBER];

const char * video_stream_get_file(int i) {
    if (i > CAMERAS_NUMBER - 1) {
        return NULL;
    } else {
        return file_names[i];
    }
}

/* ...play data from a movie file */
static inline int playback_video(app_data_t *app, track_desc_t *track, int start)
{
    if (start)
    {
#ifdef ENABLE_OBJDET
        if (!(app->flags & APP_FLAG_SVIEW)) {
            /* ...generic video clip is allowed only for object-detection track */
            CHK_ERR(track->type == 1, -EINVAL);
            /* ...initialize camera bin (frontal camera only) */
            CHK_API(objdet_camera_init(app, video_stream_create));
        }
        else
        {
#endif
        CHK_ERR(track->type == 0, -EINVAL);
        parse_video_file_names(track->file, file_names, CAMERAS_NUMBER);
        CHK_API(sview_camera_init(app, video_stream_create));
#ifdef ENABLE_OBJDET
        }
#endif
    }
    else
    {
        /* ...no special stop command; just emit eos */
    }

    return 0;
}

/*******************************************************************************
 * Track preparation - public API
 ******************************************************************************/

/* ...VIN capturing control */
static inline int app_vin_capturing(app_data_t *app, track_desc_t *track, int start)
{
    TRACE(INIT, _b("%s live capturing from VIN cameras"), (start ? "start" : "stop"));

    /* ...set live source flag */
    __live_source = 1;

    if (start)
    {
        
#ifdef ENABLE_OBJDET
        if (!(app->flags & APP_FLAG_SVIEW))
        {
            /* ...make sure it is a live objdet track */
            CHK_ERR(track == __od_live, -EINVAL);
            /* ...make sure devices are set - tbd */    
            CHK_API(objdet_camera_init(app, __camera_vin_create, 1));
        }
        else
        {
#endif
            /* ...make sure it is a live sview track */
            CHK_ERR(track == __sv_live, -EINVAL);
            CHK_API(sview_camera_init(app, __camera_vin_create));
#ifdef ENABLE_OBJDET
        }
#endif        
    }
    else
    {
        /* ...anything special? - tbd */
    }
    
    return 0;
}

#ifdef ENABLE_CAMERA_MJPEG
/* ...live network capturing control */
static inline int app_net_capturing(app_data_t *app, track_desc_t *track, int start)
{
    /* ...it must be a live surround-view track */
    CHK_ERR(track == __sv_live, -EINVAL);

    /* ...set live source flag */
    __live_source = 1;
    
    TRACE(INIT, _b("%s live capturing from '%s'"), (start ? "start" : "stop"), iface);

    if (start)
    {
        /* ...add surround-view camera set */
        CHK_API(sview_camera_init(app, camera_mjpeg_create));
    }
    else
    {
        /* ...anything special? - tbd */
    }

    return 0;
}
#endif

/* ...offline playback control */
static inline int app_offline_playback(app_data_t *app, track_desc_t *track, int start)
{
    char   *filename = track->file;

    TRACE(INIT, _b("%s offline playback: file='%s'"), (start ? "start" : "stop"), filename);
    
    /* ...clear live interface flag */
    __live_source = 0;

#ifdef ENABLE_CAMERA_MJPEG
    char   *ext;    
    /* ...get file extension */
    if ((ext = strrchr(filename, '.')) != NULL)
    {
        ext++;
    
        if (!strcasecmp(ext, "pcap"))
        {
            /* ...file is a TCPDUMP output */
            return CHK_API(playback_pcap(app, track, start));
        }
        else if (!strcasecmp(ext, "blf"))
        {
            /* ...file is a Vector BLF format */
            return CHK_API(playback_blf(app, track, start));
        }
    }
#endif
    /* ...unrecognized extension; treat file as a movie clip */
    return CHK_API(playback_video(app, track, start));
}

/* ...start track */
int app_track_start(app_data_t *app, track_desc_t *track, int start)
{
    /* ...initialize active cameras set (not always required) */
            
    /* ...current played file? - tbd */
    if (track == __sv_live)
    {
        TRACE(DEBUG, _b("track start"));
#ifdef ENABLE_CAMERA_MJPEG
        if (track->camera_type == TRACK_CAMERA_TYPE_MJPEG)
        {
            camera_mac_address = track->mac;
            return app_net_capturing(app, track, start);
        }
#endif
        if (track->camera_type == TRACK_CAMERA_TYPE_VIN)
        {
            return app_vin_capturing(app, track, start);
        }
    }
#ifdef ENABLE_OBJDET
    else if (track == __od_live)
    {
        return app_vin_capturing(app, track, start);
    }
#endif
    else if (track->file)
    {
        return app_offline_playback(app, track, start);
    }
    
    return CHK_API(- EINVAL);
}

/*******************************************************************************
 * Entry point
 ******************************************************************************/

int main(int argc, char **argv)
{
    display_data_t  *display;
    app_data_t      *app;

    /* ...initialize tracer facility */
    TRACE_INIT("Combined ADAS demo");

    /* ...initialize GStreamer */
    gst_init(&argc, &argv);

    /* ...initialize global tracks list */
    __sv_tracks.next = __sv_tracks.prev = &__sv_tracks;

#ifdef ENABLE_OBJDET
    /* ...initialize global tracks list */
    __od_tracks.next = __od_tracks.prev = &__od_tracks;
#endif    
    
    /* ...parse application specific parameters */
    CHK_API(parse_cmdline(argc, argv));
    
    /* ...verify track lists are not empty */
#ifdef ENABLE_OBJDET
    CHK_ERR(__sv_tracks.next != &__sv_tracks && __od_tracks.next != &__od_tracks, -EINVAL);
#endif
//    CHK_ERR(__sv_tracks.next != &__sv_tracks, -EINVAL);

    /* ...initialize display subsystem */
    CHK_ERR(display = display_create(), -errno);
    /* ...initialize surround-view application */
    CHK_ERR(app = app_init(display, &__sv_cfg, flags), -errno);
    
#ifdef ENABLE_CAMERA_MJPEG
    /* ...initialize network interface for a live capturing case */
    if(iface)
    	CHK_API(netif_init(&netif, iface));
#endif
    
    /* ...execute mainloop thread */
    app_thread(app);

    TRACE_INIT("application terminated");
    
    return 0;
}
