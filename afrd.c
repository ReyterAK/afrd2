/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

#include "afrd.h"
#include "uevent_filter.h"
#include "colorspace.h"
#include "androp.h"
#include "platform.h"
#include "vdec.h"
#include "sfmodes.h"
#include "sink.h"
#include "guard.h"
#include "exec.h"
#include "fps_class.h"

#define __USE_GNU
#include <unistd.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#ifndef SOCK_CLOEXEC
#  define SOCK_CLOEXEC O_CLOEXEC
#endif

#define CMSG_FOREACH(cmsg, mh)                                          \
	for ((cmsg) = CMSG_FIRSTHDR(mh); (cmsg); (cmsg) = CMSG_NXTHDR((mh), (cmsg)))

const char *g_hdmi_dev = NULL;
const char *g_mode_path = NULL;

static const char *g_hdmi_state = NULL;
static int g_uevent_sock = -1;
static const char *g_vdec_sysfs = NULL;

static int g_switch_delay_on;
static int g_switch_delay_off;
static int g_switch_delay_retry;
static int g_switch_timeout;
static int g_switch_blackout;
static int g_switch_ignore;
static int g_switch_hdmi;

static int g_mode_prefer_exact;
static int g_mode_use_fract;
static int g_mode_blacklist_rates [10];
static int g_mode_blacklist_rates_count;

static bool g_enable;                 // enabled in config file

static uevent_filter_t g_filter_frhint;
static uevent_filter_t g_filter_vdec;
static uevent_filter_t g_filter_hdmi;
static uevent_filter_t g_filter_hdcp;

static strlist_t g_vdec_blacklist;
static strlist_t g_frhint_vdec_blacklist;

/**
 * The one-shot timer used to switch display mode.
 * If we switch display mode instantly after we receive an uevent,
 * this will work but can lead to excessive display mode switches,
 * which means worse user experience. Thus, we delay display mode
 * switching by SWITCH_DELAY milliseconds. If we don't receive
 * more uevents during that time, we'll switch the mode. This will
 * allow to accumulate several mode switches into one 
 */
static mstime_t g_ost_switch;

/**
 * One more timer to delay querying current video mode after HDMI
 * link becomes active.
 */
static mstime_t g_ost_hdmi;

/**
 * Screen blackout timer
 */
static mstime_t g_ost_blackout;

/**
 * Timer for periodic config timestamp checks
 */
static mstime_t g_ost_config;

/**
 * Time stamp of the last refresh rate restore event
 */
static mstime_t g_ost_off;

/**
 * Will re-enable HDCP if getting an "HDCP HDMI off" event
 * during this timer
 */
static mstime_t g_ost_hdcp;

/**
 * Periodic HDCP sanity check
 */
static mstime_t g_ost_hdcp_check;

/**
 * Available sources for fps values
 */
typedef enum
{
	// FRAME_RATE_HINT should be immediately usable
	SRC_FRH,
	// dump_vdec_chunks is more reliable than others, only 2 confirmations
	SRC_CHUNKS,
	// dump_vdec_blocks is less reliable, so 3 confirmations
	SRC_BLOCKS,
	// need 4 confirmations to accept vdec_status
	SRC_VDEC,

	// number of fps sources
	SRC_COUNT
} hz_source_t;

static const uint8_t src_weight [SRC_COUNT] =
{
	100,	// FRH
	50,	// CHUNKS
	34,	// BLOCKS
	25,	// VDEC
};

// accept hz when weight sum up to this
#define ACCEPT_HZ_WEIGHT        100

// minimum sane refresh rate, .8 fixed-point
#define HZ_MIN		FP8 ( 10,000)
#define HZ_MAX		FP8 (100,000)

typedef struct
{
	// last seen value from this source
	int hz;
	// total weight of hz samples
	int weight;
	// expiration timer for collecting data from this source
	mstime_t timeout;
} hz_stat_t;

/**
 * Frame rate detector data.
 */
static struct
{
	// true to restore original display mode, false to set from current movie
	bool restore;
	// true if a mode switch is pending
	bool delayed_switch;
	// desired refresh rate in hz (fixed-point 24.8) if known, or 0 if not known
	int hz;
	// the initial video mode (this mode is set when restore==true)
	display_mode_t orig_mode;
	// active video decoder driver name
	char modalias [32];
	// hz detection timeout
	mstime_t hz_ost;

	// we accumulate fps data from every source
	hz_stat_t hz_stat [SRC_COUNT];

	// a stamp to detect when dump_vdec_blocks stays still
	int hz_samples_stamp;
} g_state;

/**
 *
 */
static struct
{
	// time stamp when this hint was valid
	mstime_t stamp;
	// the declared movie fps
	int fps;
} g_frame_rate_hint;


static bool uevent_open (int buf_sz)
{
	struct sockaddr_nl addr;

	memset (&addr, 0, sizeof (addr));
	addr.nl_family = AF_NETLINK;
	addr.nl_pid = getpid ();
	addr.nl_groups = 0xffffffff;

	g_uevent_sock = socket (PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
	if (g_uevent_sock < 0)
		return false;

	setsockopt (g_uevent_sock, SOL_SOCKET, SO_RCVBUFFORCE, &buf_sz, sizeof (buf_sz));

	int one = 1;
	setsockopt (g_uevent_sock, SOL_SOCKET, SO_PASSCRED, &one, sizeof (one));

	if (bind (g_uevent_sock, (struct sockaddr *)&addr, sizeof (addr)) < 0) {
		close (g_uevent_sock);
		g_uevent_sock = -1;
		return false;
	}

	fcntl (g_uevent_sock, F_SETFL, O_NONBLOCK);

	return true;
}

/* ---- ATV14: состояние для статистики API (afrd -s, status) ----
 * run_atv14() не ведёт legacy-структуры g_state/g_current_mode, поэтому
 * актуальные значения складываются сюда, а update_stats() их подхватывает. */
static struct
{
	bool valid;		/* ATV14-цикл уже заполнил поля */
	bool switched;		/* режим уведён от исходного (backdoor поднят) */
	int current_hz;		/* 24.8 fixed-point */
	int original_hz;	/* 24.8 fixed-point */
} g_atv14_state;

static void update_stats ()
{
	g_afrd_stats.enabled = g_enable;

	if (g_atv14_state.valid)
	{
		g_afrd_stats.switched = g_atv14_state.switched;
		g_afrd_stats.blackened = false;
		g_afrd_stats.current_hz = g_atv14_state.current_hz;
		g_afrd_stats.original_hz = g_atv14_state.original_hz;
	}
	else
	{
		g_afrd_stats.switched = (g_state.orig_mode.name [0] != 0);
		g_afrd_stats.blackened = g_blackened;
		g_afrd_stats.current_hz = display_mode_hz (&g_current_mode);
		g_afrd_stats.original_hz = display_mode_hz (g_afrd_stats.switched ?
			&g_state.orig_mode : &g_current_mode);
	}

	shmem_update ();
}

static bool rate_is_blacklisted (int rate)
{
	for (int i = 0; i < g_mode_blacklist_rates_count; i++)
		if (abs (g_mode_blacklist_rates [i] - rate) <= 1)
			return true;
	return false;
}

// note! this function considers fractional rate "equal" to integer rate!
// it is a rough test to filter off data that is way off the mainline.
static bool hz_close (int hz1, int hz2)
{
	// if is +- 1 units, consider it a rounding error
	if (abs (hz1 - hz2) <= 1)
		return true;

	// if they are no more than 0.5% apart, it's a rounding error
	return abs (10000 - ((hz1 * 10000 + hz2 / 2) / hz2)) <= 50;
}

// round hz to nearest known standard framerate
static int hz_round (int hz)
{
	static short sane_hz [] =
	{
		FP8 (23,976), FP8 (24,000),
		FP8 (25,000),
		FP8 (29,970), FP8 (30,000),
		FP8 (50,000),
		FP8 (59,940), FP8 (60,000),
	};

	int closest_hz = 0;
	int closest_delta = 99999999;

	for (size_t i = 0; i < ARRAY_SIZE (sane_hz); i++) {
		int shz = sane_hz [i];
		int delta = abs (shz - hz);
		if (delta < closest_delta) {
			closest_delta = delta;
			closest_hz = shz;
		}
	}

	dtrace (2, "\t> hz "HZ_FMT" closest "HZ_FMT"\n", HZ_ARGS(hz), HZ_ARGS(closest_hz));

	// if frame rates are close enough, consider them equal
	if (hz_close (hz, closest_hz))
		return closest_hz;

	// otherwise, invalid frame rate
	return 0;
}

// accumulate fps data from different sources
static void accumulate_fps (int hz, hz_source_t src)
{
	hz_stat_t *stat = &g_state.hz_stat [src];
	if (stat->weight && !hz_close (hz, stat->hz)) {
		g_state.hz_stat [src].weight = 0;
		trace (2, "Resetting Hz weight src %d\n", src);
	}

	int weight = src_weight [src];

	stat->hz = hz;
	stat->weight += weight;
	mstime_arm (&stat->timeout, g_switch_delay_retry * 2);

	trace (2, "Accumulating "HZ_FMT"fps src %d weight %d total %d\n",
		HZ_ARGS (hz), src, weight, stat->weight);
}

// guess the best fps from accumulated data, more insistent if last_chance is true
static int best_fps (bool last_chance)
{
	hz_stat_t *best_stat = NULL;
	int best_weight = 0;
	int best_src = -1;

	for (int i = 0; i < SRC_COUNT; i++) {
		hz_stat_t *stat = &g_state.hz_stat [i];

		if (last_chance) {
			// if last chance, use any detection that we're at least half sure
			if (stat->weight < ACCEPT_HZ_WEIGHT/2)
				continue;
		} else {
			// we still have time, wait best src that has a chance to finish
			if (stat->weight < ACCEPT_HZ_WEIGHT) {
				if (mstime_running (&stat->timeout))
					break;
				continue;
			}
		}

		if (best_weight < stat->weight) {
			best_weight = stat->weight;
			best_stat = stat;
			best_src = i;
		}
	}

	if (!best_stat)
		return 0;

	trace (2, "Best src %d "HZ_FMT"Hz weight %d timeout %d\n",
	        best_src, HZ_ARGS (best_stat->hz), best_stat->weight,
	        mstime_left (&best_stat->timeout));

	return best_stat->hz;
}

static bool query_vdec_blocks ()
{
	if (!g_vdec_sysfs)
		return false;

	trace (2, "Querying vdec_blocks\n");

	char line [200];
	snprintf (line, sizeof (line), "%s/dump_vdec_blocks", g_vdec_sysfs);
	FILE *vsf = fopen (line, "r");
	if (!vsf)
		return false;

	bool ok = fgets (line, sizeof (line), vsf) != NULL;
	fclose (vsf);
	if (!ok)
		return false;

	unsigned dsize = find_ulong (line, ",dsize=", &ok);
	unsigned nframes = find_ulong (line, ",frames:", &ok);
	unsigned timeint = find_ulong (line, ",dur:", &ok);

	dtrace (2, "\t> dsize %u frames %u dur %u\n", dsize, nframes, timeint);

	// if we don't have enough stats, don't take it into account
	if (!ok || nframes < 5 || timeint < 120 ||
	    g_state.hz_samples_stamp == dsize)
		return false;

	g_state.hz_samples_stamp = dsize;
	int hz = hz_round ((nframes * 256000 + timeint / 2) / timeint);
	if (hz == 0)
		return false;

	trace (2, "\t> %d frames played over last %dms at "HZ_FMT"fps\n",
		nframes, timeint, HZ_ARGS (hz));
	accumulate_fps (hz, SRC_BLOCKS);
	return true;
}

static int compare_pts (const void *a, const void *b)
{
	int ptsa = *(int *)a;
	int ptsb = *(int *)b;
	return ptsa - ptsb;
}

static bool query_vdec_chunks ()
{
	if (!g_vdec_sysfs)
		return false;

	trace (2, "Querying vdec_chunks\n");

	char buff [4096];
	snprintf (buff, sizeof (buff), "%s/dump_vdec_chunks", g_vdec_sysfs);
	int vsh = open (buff, O_RDONLY);
	if (vsh < 0)
		return false;

	int size = read (vsh, buff, sizeof (buff) - 1);
	close (vsh);
	if (size < 100)
		return false;

	buff [size] = 0;
	char *cur = buff;
	int pts [64];
	int pts_size = 0;
	unsigned long long pts64_base;

	while (*cur) {
		char *eol = strchr (cur, '\n');
		if (!eol)
			break; // incomplete line
		*eol = '\0';

		bool ok = true;
		unsigned long long pts64 = find_ulonglong (cur, "pts64=", &ok);
		if (ok) {
			if (!pts_size)
				pts64_base = pts64;
			pts [pts_size++] = (int)(pts64 - pts64_base);
			if (pts_size > ARRAY_SIZE (pts))
				break; // enough data
		}

		cur = eol + 1;
	}

	if (pts_size < 5)
		return false;

	qsort (pts, pts_size, sizeof (pts [0]), compare_pts);

	// transform to usecs per frame
	int i;
	for (i = 1; i < pts_size; i++)
		pts [i - 1] = pts [i] - pts [i - 1];
	pts_size--;

	// now compute the average frame rate
	int base_pts = pts [0];
	int avg_pts = base_pts;
	int avg_count = 1;
	for (i = 1; i < pts_size; i++) {
		int rate = 128 * pts [i] / base_pts;
		if ((rate >= 247) && (rate <= 264)) {
			// pts [i] is a frame skip
			avg_count++;
		} else if ((rate >= 62) && (rate <= 66)) {
			// all previous pts were frame skips
			avg_count *= 2;
			base_pts = pts [i];
		} else if ((pts [i] > base_pts + 1500) || (pts [i] < base_pts - 1500))
			continue; // totally wrong pts
		avg_count++;
		avg_pts += pts [i];
	}

	if (avg_count < 3)
		return false;

	trace (2, "\t> %d pts in %d us, base pts %d\n", avg_count, avg_pts, base_pts);

	// warning: 32 bits easily overflow here, using longlong.
	int hz = hz_round ((avg_count * 256000000ULL) / avg_pts);
	if (hz == 0)
		return false;

	// this gives fps with very high precision
	accumulate_fps (hz, SRC_CHUNKS);
	return true;
}

static bool query_vdec ()
{
	if (!g_vdec_sysfs)
		return false;

	trace (2, "Querying vdec_status\n");

	char line [200];
	snprintf (line, sizeof (line), "%s/vdec_status", g_vdec_sysfs);
	FILE *vsf = fopen (line, "r");
	if (!vsf) {
		trace (1, "\t> Failed to open %s/vdec_status\n", g_vdec_sysfs);
		g_vdec_sysfs = NULL;
		return false;
	}

	// the values we're going to extract
	int fps = 0, frame_dur = 0;

	while (fgets (line, sizeof (line), vsf)) {
		// Bionic sscanf sucks badly, so parse the string manually...
		char *cur = line;
		const char *attr, *val;

		cur += strspn (cur, spaces);
		attr = cur;
		while (*cur && (*cur != ':'))
			cur++;
		if (!*cur)
			continue;
		*cur = 0;
		strip_trailing_spaces (cur, attr);
		cur++;
		cur += strspn (cur, spaces);

		val = cur;
		cur = strchr (cur, 0);
		strip_trailing_spaces (cur, val);

		dtrace (2, "\tattr [%s] val [%s]\n", attr, val);

		if (strcmp (attr, "frame rate") == 0) {
			char *endp;
			fps = strtol (val, &endp, 10);
			endp += strspn (endp, spaces);
			if ((*endp != 0) && (strcmp (endp, "fps") != 0)) {
				trace (2, "\tgarbage at end of 'frame rate': [%s]\n", endp);
				fps = 0;
			} else
				trace (2, "\t> frame rate %d\n", fps);
		} else if (strcmp (attr, "frame dur") == 0) {
			char *endp;
			frame_dur = strtol (val, &endp, 10);
			if (*endp != 0) {
				trace (2, "\tgarbage at end of 'frame dur': [%s]\n", endp);
				frame_dur = 0;
			} else
				trace (2, "\t> frame dur %d\n", frame_dur);
		}
	}

	fclose (vsf);

	// Prefer frame_dur over fps, but sometimes it's 0 and sometimes it's insane
	int hz = 0;
	if (frame_dur)
		hz = hz_round ((256*96000 + frame_dur / 2) / frame_dur);

	if ((hz == 0) && (fps != 0)) {
		switch (fps) {
			case 23: hz = (2997 * 256 + 62) / 125; break;
			case 29: hz = (2997 * 256 + 50) / 100; break;
			case 59: hz = (5994 * 256 + 50) / 100; break;
			case 24:
			case 25:
			case 30:
			case 50:
			case 60: hz = (fps << 8); break;
			default: trace (2, "ignoring non-standard frame rate %d fps\n", fps); break;
		}
	}

	if (!hz)
		return false;

	accumulate_fps (hz, SRC_VDEC);
	return true;
}

static void blackout ()
{
	mstime_disable (&g_ost_blackout);

	if (g_blackened)
		return;

	display_mode_get_current ();
	g_state.orig_mode = g_current_mode;
	display_mode_null ();

	update_stats ();
}

static void framerate_restore (bool only_if_black)
{
	mstime_disable (&g_ost_blackout);
	mstime_disable (&g_ost_switch);

	if (only_if_black && !g_blackened)
		return;

	mstime_arm (&g_ost_hdcp, DEFAULT_SWITCH_HDCP);
	if (g_state.orig_mode.name [0])
		display_mode_switch (&g_state.orig_mode, false);
	else
		display_mode_switch (&g_current_mode, false);

	memset (&g_state, 0, sizeof (g_state));
	update_stats ();
}

static void framerate_switch (bool force)
{
	g_state.delayed_switch = false;

	if (g_state.restore) {
		if (!g_state.orig_mode.name [0])
			trace (1, "No saved display mode to restore\n");
		framerate_restore (false);
		return;
	}

	if (!g_enable) {
		trace (1, "User disabled AFR\n");
		framerate_restore (true);
		return;
	}

	if ((g_state.hz == 0) &&
	    mstime_expired (&g_state.hz_ost)) {
		g_state.hz = best_fps (true);
		if (g_state.hz == 0) {
giveup:			trace (1, "Timeout detecting movie frame rate, giving up\n");
			framerate_restore (true);
			return;
		}
	}

	// ask every source until we have a valid refresh rate
	if (g_state.hz == 0) {
		query_vdec_chunks ();
		query_vdec_blocks ();
		query_vdec ();
		g_state.hz = best_fps (false);
		if (g_state.hz == 0) {
			// Cannot determine movie frame rate, retry if allowed
			if (!g_switch_delay_retry)
				goto giveup;

			mstime_arm (&g_ost_switch, g_switch_delay_retry);
			return;
		}
	}

	// use fractional or integer frame rates if user requested so
	if (g_mode_use_fract != 0) {
		display_mode_t tmp;
		tmp.framerate = (g_state.hz + 0x80) >> 8;
		tmp.fractional = (g_mode_use_fract == 1);
		g_state.hz = display_mode_hz (&tmp);
	}

	trace (1, "Current mode is "DISPMODE_FMT"\n",
		DISPMODE_ARGS (g_current_mode, display_mode_hz (&g_current_mode)));
	trace (1, "Looking for display mode closest to %dx%d@"HZ_FMT"Hz\n",
		g_current_mode.width, g_current_mode.height, HZ_ARGS (g_state.hz));

	/* Find the video mode that:
	 * a) Has same width and height and interlace flag
	 * b) Closely divides by the required framerate, error less
	 *    than 4.1% (difference between 23.976 and 25 Hz)
	 * c) Has the highest framerate (e.g. 50Hz display modes are
	 *    better than 25Hz display modes for displaying 25Hz video)
	 *    if g_mode_prefer_exact is 0, or
	 * d) Has the closest framerate if g_mode_prefer_exact is 1.
	 */
	display_mode_t best_mode;
	unsigned best_rating = 0;
	int i;
	best_mode.name [0] = 0;
	for (i = 0; i < g_modes_n; i++) {
		display_mode_t *mode = &g_modes [i];
		if ((mode->width != g_current_mode.width) ||
		    (mode->height != g_current_mode.height) ||
		    (mode->interlaced != g_current_mode.interlaced))
			continue;

		unsigned rate_n = 1;
		unsigned rate = (mode->framerate << 16) / g_state.hz;
		while (rate > 0x180) {
			rate_n++;
			rate = (mode->framerate << 16) / (g_state.hz * rate_n);
		}

		unsigned delta = abs ((int)(rate - 0x100));
		if (delta > 11)
			continue; // freq error > 4.3%

		// rating is larger as delta is closer to 1.0 rate
		int rating = (11 - delta) * 16;

		unsigned n = (rate_n > 3) ? 3 : (rate_n - 1);
		rating += 4 * (g_mode_prefer_exact ? (3 - n) : n);

		if (rating > best_rating) {
			display_mode_t tmp = *mode;
			// decide if integer or fractional framerate is better
			display_mode_set_hz (&tmp, g_state.hz);

			// if framerate is blacklisted, try to invert fractional
			if (rate_is_blacklisted (display_mode_hz (&tmp))) {
				tmp.fractional = !tmp.fractional;
				if (rate_is_blacklisted (display_mode_hz (&tmp)))
					// no luck, both framerates are banned
					continue;
			}

			best_rating = rating;
			best_mode = tmp;
		}
	}

	if (!best_mode.name [0]) {
		trace (1, "Failed to find a suitable display mode\n");
		framerate_restore (true);
		return;
	}

	// if we already switched mode to something close to what we found,
	// avoid unneeded irritating framerate switching in the middle of watching
	if (g_state.orig_mode.name [0] && !g_blackened && !force) {
		int hz1 = display_mode_hz (&best_mode);
		int hz2 = display_mode_hz (&g_current_mode);
		if (hz_close (hz1, hz2)) {
			trace (1, "Skipping mode switch since current refresh is close enough\n");
			framerate_restore (true);
			return;
		}
	}

	mstime_disable (&g_ost_blackout);

	// remember current video mode to restore it later
	if (!g_state.orig_mode.name [0])
		g_state.orig_mode = g_current_mode;

	mstime_arm (&g_ost_hdcp, DEFAULT_SWITCH_HDCP);
	display_mode_switch (&best_mode, force);
	update_stats ();
}

/* @param restore true to delay restoring refresh rate to original,
 *      false to set refresh rate to match currently playing movie.
 * @param hz screen refresh rate in fixed-point 24.8 format if known,
 *      or 0 to ask movie refresh rate from vdec.
 * @param modalias the value of MODALIAS= uevent attribute (codec name)
 */
static void delay_framerate_switch (bool restore, int hz, const char *modalias)
{
	mstime_disable (&g_ost_blackout);
	mstime_disable (&g_ost_switch);

	if (g_switch_ignore) {
		if (restore)
			mstime_arm (&g_ost_off, g_switch_ignore);
		else if (mstime_running (&g_ost_off) &&
			 (g_state.orig_mode.name [0] != 0) &&
		         !g_blackened &&
		         !g_state.delayed_switch) {
			trace (1, "Ignore framerate switch because restore event was %d ms ago\n",
				g_switch_ignore - mstime_left (&g_ost_off));
			g_state.restore = false;
			mstime_disable (&g_ost_blackout);
			mstime_disable (&g_ost_switch);
			update_stats ();
			return;
		}
	}

	mstime_t delay = restore ? g_switch_delay_off : g_switch_delay_on;

	if (restore && !g_switch_delay_off) {
		trace (1, "Refresh rate restoration disabled by user\n");

		framerate_restore (true);
		memset (&g_state, 0, sizeof (g_state));
		update_stats ();
		return;
	}

	if (modalias) {
		if (strlist_contains (&g_vdec_blacklist, modalias)) {
			trace (1, "Blacklisted vdec %s, skipping AFR\n", modalias);
			return;
		}

		// save decoder name
		strncpy (g_state.modalias, modalias, sizeof (g_state.modalias));
	}

	if (g_state.restore != restore) {
		g_state.restore = restore;
		g_state.hz = hz;
		// start collecting stats all over again
		memset (&g_state.hz_stat, 0, sizeof (g_state.hz_stat));
	}

	// if refresh rate is going to be restored, and screen is black, do not delay
	if (restore && g_blackened)
		delay = g_switch_delay_on;

	// check for frame_rate_hint via API
	if (!restore &&
	    mstime_running (&g_frame_rate_hint.stamp))
		hz = g_frame_rate_hint.fps;

	// if we known fps, use it
	if (hz && (hz >= HZ_MIN) && (hz < HZ_MAX)) {
		accumulate_fps (hz, SRC_FRH);
		g_state.hz = best_fps (false);
	}

	if (g_state.hz)
		trace (1, "Delaying switch to "HZ_FMT"Hz by %d ms\n",
			HZ_ARGS (g_state.hz), delay);
	else if (restore)
		trace (1, "Delaying refresh rate restoration by %d ms\n", delay);
	else
		trace (1, "Starting framerate detection in %d ms\n", delay);

	mstime_arm (&g_ost_switch, delay);

	if (restore)
		mstime_disable (&g_state.hz_ost);
	else {
		g_state.delayed_switch = true;
		mstime_arm (&g_state.hz_ost, g_switch_timeout);
		// when movie starts, disable screen until we switch to actual frame rate
		if (g_enable && g_switch_blackout &&
		    !g_state.hz && !g_state.orig_mode.name [0])
			mstime_arm (&g_ost_blackout, g_switch_blackout);
	}
}

static void handle_hdmi_switch (int state)
{
	if (state == -1)
		state = sysfs_get_int (g_hdmi_state, NULL);

	if (state <= 0) {
		trace (1, "HDMI not active, clearing video mode list\n");
		hdcp_fini ();
		display_modes_fini ();
		memset (&g_state.orig_mode, 0, sizeof (g_state.orig_mode));
	} else {
		display_modes_init ();
		colorspace_refresh ();
		hdcp_init ();
	}
}

static void handle_uevent (char *msg, ssize_t size)
{
	const char *frame_rate_hint = NULL;
	const char *frame_rate_end_hint = NULL;
	const char *action = NULL;
	const char *modalias = NULL;
	const char *msg_orig = msg;

	uevent_filter_reset (&g_filter_frhint);
	uevent_filter_reset (&g_filter_vdec);
	uevent_filter_reset (&g_filter_hdmi);
	uevent_filter_reset (&g_filter_hdcp);

	for (const char *end = msg + size; msg < end; msg += strlen (msg) + 1) {
		if (msg == msg_orig) {
			trace (2, "Parsing uevent %s\n", msg);
			continue;
		} else
			trace (2, "\t> %s\n", msg);

		char *val = strchr (msg, '=');
		if (val)
			*val++ = 0;
		else
			val = strchr (msg, 0);

		/* look for keywords we are interested */
		if (strcmp (msg, "FRAME_RATE_HINT") == 0)
			frame_rate_hint = val;
		else if (strcmp (msg, "FRAME_RATE_END_HINT") == 0)
			frame_rate_end_hint = val;
		else if (strcmp (msg, "ACTION") == 0)
			action = val;
		else if (strcmp (msg, "MODALIAS") == 0)
			modalias = val + strskip (val, "platform:");

		/* and count matches for every kind of uevent */
		uevent_filter_match (&g_filter_frhint, msg, val);
		uevent_filter_match (&g_filter_vdec, msg, val);
		uevent_filter_match (&g_filter_hdmi, msg, val);
		uevent_filter_match (&g_filter_hdcp, msg, val);
		msg = val;
	}

	if (uevent_filter_matched (&g_filter_frhint)) {
		/* got a framerate hint uevent */
		if (frame_rate_hint) {
			char *end;
			int frh = strtoul (frame_rate_hint, &end, 10);
			if (frh && (!end || !*end)) {
				if (strlist_contains (&g_frhint_vdec_blacklist, g_state.modalias)) {
					trace (1, "Blacklisted vdec %s for FRAME_RATE_HINT, skipping\n",
					       g_state.modalias);
					return;
				}

				int fr_hz = hz_round ((256*96000 + frh / 2) / frh);
				delay_framerate_switch (false, fr_hz, modalias);
			}
		} else if (frame_rate_end_hint)
			delay_framerate_switch (true, 0, modalias);

	} else if (uevent_filter_matched (&g_filter_vdec)) {
		/* got a vdec uevent */
		if (action && (strcmp (action, "add") == 0))
			delay_framerate_switch (false, 0, modalias);
		else if (action && (strcmp (action, "remove") == 0))
			delay_framerate_switch (true, 0, modalias);

	} else if (uevent_filter_matched (&g_filter_hdmi) && g_switch_hdmi) {
		/* hdmi plugged on or off */
		trace (1, "HDMI state changed, will handle in %d ms\n",
			g_switch_hdmi);
		mstime_arm (&g_ost_hdmi, g_switch_hdmi);

	} else if (uevent_filter_matched (&g_filter_hdcp)) {
		// If playing or within a few seconds after a video mode switch
		if (g_state.orig_mode.name [0] ||
		    mstime_running (&g_ost_hdcp)) {
			/* HDCP turned HDMI off, turn it back on */
			trace (1, "HDCP disabled HDMI, re-enable HDCP 1.4\n");
			hdcp_restore (true);
		}

	} else
		trace (2, "\tUnrecognized uevent\n");
}

static void handle_uevents ()
{
	for (;;)
	{
		char msg [4096];
		struct sockaddr_nl addr;
		struct iovec iovec = {
			.iov_base = &msg,
			.iov_len = sizeof (msg) - 1,
		};
		union {
			struct cmsghdr cmsghdr;
			uint8_t buf [CMSG_SPACE (sizeof (struct ucred))];
		} control = {};
		struct msghdr msghdr = {
			.msg_name = &addr,
			.msg_namelen = sizeof (addr),
			.msg_iov = &iovec,
			.msg_iovlen = 1,
			.msg_control = &control,
			.msg_controllen = sizeof (control),
		};

		ssize_t size = recvmsg (g_uevent_sock, &msghdr, MSG_DONTWAIT);
		if (size < 0) {
			if (errno == EAGAIN)
				return;

			continue;
		}

		struct cmsghdr *cmsg;
		struct ucred *ucred = NULL;
		CMSG_FOREACH (cmsg, &msghdr) {
			if (cmsg->cmsg_level == SOL_SOCKET &&
			    cmsg->cmsg_type == SCM_CREDENTIALS &&
			    cmsg->cmsg_len == CMSG_LEN (sizeof (struct ucred)))
				ucred = (struct ucred*) CMSG_DATA (cmsg);
		}

		if (!ucred || ucred->pid != 0 || addr.nl_pid != 0)
			continue;

		msg [size] = 0;
		handle_uevent (msg, size);
	}
}

/* --------- * --------- * --------- * --------- * --------- * --------- */

/* check config file once in 5 seconds */
#define CONFIG_CHECK_PERIOD	5000

static int min_time (int to, mstime_t *ost)
{
	int tost = mstime_left (ost);
	if ((to < 0) || ((tost >= 0) && (tost < to)))
		return tost;
	return to;
}

static time_t mtime (const char *fn)
{
	struct stat st;
	if (stat (fn, &st) == 0)
		return st.st_mtime;
	return 0;
}

static void mstime_adjust (int delta, mstime_t *ost)
{
	if (mstime_enabled (ost))
		*ost += delta;
}

static void safe_mstime_update (int to)
{
	int old_mstime = g_mstime;
	mstime_update ();

	// time difference is substantially larger than timeout?
	int delta_mstime = g_mstime - old_mstime;
	if ((delta_mstime < 0) || (delta_mstime > to + 10000)) {
		delta_mstime -= to;
		trace (1, "System timer changed, adjusting all timers by %d ms\n", delta_mstime);
		mstime_adjust (delta_mstime, &g_ost_switch);
		mstime_adjust (delta_mstime, &g_ost_hdmi);
		mstime_adjust (delta_mstime, &g_ost_blackout);
		mstime_adjust (delta_mstime, &g_ost_config);
	}
}

static time_t g_config_mtime;

/* --------- * --------- * --------- * --------- * --------- * --------- */

/* ---- ATV14: поллинг декодера + измерение fps + backdoor SurfaceFlinger ---- */

/* Таблица режимов, полученная от SurfaceFlinger (заполняется sf_modes_refresh). */
static sf_mode_t g_sf_modes [SF_MODE_MAX];
static int g_sf_modes_n = 0;

/* Сон с оглядкой на завершение: просыпаемся не реже, чем раз в 200 мс. */
static void msleep (int ms)
{
	while (ms > 0 && !g_shutdown)
	{
		int chunk = ms > 200 ? 200 : ms;
		usleep (chunk * 1000);
		ms -= chunk;
	}
}

static void sf_modes_refresh (const platform_t *plat)
{
	char dump [64 * 1024];
	if (exec_capture (plat->sf_dump_cmd, dump, sizeof (dump)) != 0)
	{
		trace (1, "не удалось получить список режимов SurfaceFlinger\n");
		g_sf_modes_n = 0;
		return;
	}

	g_sf_modes_n = sfmodes_parse (dump, g_sf_modes, SF_MODE_MAX);
	if (g_sf_modes_n <= 0)
		/* Уровень 0 — иначе ровно этот случай (дамп усечён >64 КБ или
		 * команда ничего не вернула) оставался только в stdout и AFR
		 * молча не переключал (живая приёмка ATV14). */
		trace (0, "таблица режимов SurfaceFlinger не разобрана (пусто/усечён дамп: %d байт?)\n",
		       (int) strlen (dump));
	trace (1, "режимов SurfaceFlinger: %d\n", g_sf_modes_n);
}

/* Таблица режимов из последнего разобранного дампа. Нужна, чтобы заметить смену
 * нумерации id сразу (рестарт SurfaceFlinger), не дожидаясь проверки PID:
 * иначе устаревший want_sf_id уходил бы в транзакцию 1035. */
static sf_mode_t g_sf_live [SF_MODE_MAX];
static bool g_sf_table_changed = false;

/* Сравнение таблиц без учёта порядка строк (порядок вывода dumpsys не гарантирован). */
static bool sf_modes_equal (const sf_mode_t *a, int na, const sf_mode_t *b, int nb)
{
	if (na != nb)
		return false;

	for (int i = 0; i < na; i++)
	{
		int j;
		for (j = 0; j < nb; j++)
			if ((a [i].id == b [j].id) &&
			    (a [i].width == b [j].width) &&
			    (a [i].height == b [j].height) &&
			    (a [i].hz == b [j].hz))
				break;
		if (j == nb)
			return false;
	}

	return true;
}

/* Следит за таблицей режимов по свежему дампу SurfaceFlinger. Возврат: 1, если
 * таблица сменилась (кэш обновлён). Усечённый/неразобранный дамп игнорируется,
 * а систематическое усечение безвредно: кэш разбирается тем же буфером, что и
 * свежий дамп, поэтому обе таблицы окажутся одинаковыми. Пустой разбор здесь
 * не логируется намеренно (функция вызывается каждую итерацию цикла) — о нём
 * сообщает sf_modes_refresh уровнем 0. */
static int sf_modes_sync (const char *dump)
{
	int n = sfmodes_parse (dump, g_sf_live, SF_MODE_MAX);
	if (n <= 0)
		return 0;

	if (sf_modes_equal (g_sf_live, n, g_sf_modes, g_sf_modes_n))
		return 0;

	memcpy (g_sf_modes, g_sf_live, (size_t) n * sizeof (g_sf_modes [0]));
	g_sf_modes_n = n;
	g_sf_table_changed = true;

	trace (1, "таблица режимов SurfaceFlinger сменилась: %d режимов\n", n);
	return 1;
}

/* Активное разрешение из DisplayManager ("real 1920 x 1080").
 * Нужно независимо от частоты: в списке SurfaceFlinger одна и та же частота
 * встречается на разных разрешениях (напр. 59.94 Гц -> 1080p id 0 и 720p id 8). */
static int active_display_size (const platform_t *plat, int *w, int *h)
{
	static char dump [64 * 1024];

	if (exec_capture (plat->display_dump_cmd, dump, sizeof (dump)) != 0)
		return -1;

	const char *p = strstr (dump, "real ");
	if (!p)
		return -1;

	if (sscanf (p, "real %d x %d", w, h) != 2)
		return -1;

	return 0;
}

/* Активный режим: частота из SurfaceFlinger ("activeMode=23.98 Hz"),
 * разрешение — из DisplayManager. Совпадение по (частота, разрешение)
 * однозначно; если разрешение узнать не удалось, откатываемся на поиск по частоте. */
static int sf_active_mode (const platform_t *plat, sf_mode_t *out)
{
	static char dump [64 * 1024];

	if (exec_capture (plat->sf_dump_cmd, dump, sizeof (dump)) != 0)
		return -1;

	/* дамп всё равно прочитан: заодно следим за сменой таблицы режимов */
	sf_modes_sync (dump);

	const char *p = strstr (dump, "activeMode=");
	if (!p)
		return -1;

	int hz = (int)(atof (p + strlen ("activeMode=")) * 256.0 + 0.5);
	if (g_sf_modes_n <= 0)
		sf_modes_refresh (plat);

	int w = 0, h = 0, id;
	if (active_display_size (plat, &w, &h) == 0)
		id = sfmodes_find (g_sf_modes, g_sf_modes_n, hz, w, h);
	else
	{
		trace (1, "не удалось определить активное разрешение, ищу по частоте\n");
		id = sfmodes_find_hz (g_sf_modes, g_sf_modes_n, hz);
	}

	if (id < 0)
		return -1;

	for (int i = 0; i < g_sf_modes_n; i++)
		if (g_sf_modes [i].id == id)
		{
			*out = g_sf_modes [i];
			return 0;
		}

	return -1;
}

/* Предпочтительный режим пользователя (настройка Android TV).
 * Возврат: mode id или -1, если пользователь его не задавал. */
static int preferred_mode_id (const platform_t *plat)
{
	static char dump [64 * 1024];

	if (exec_capture (plat->display_dump_cmd, dump, sizeof (dump)) != 0)
		return -1;

	const char *p = strstr (dump, "mUserPreferredModeId=");
	if (!p)
		return -1;

	int id = atoi (p + strlen ("mUserPreferredModeId="));
	return (id >= 0) ? id : -1;
}

/* Ожидание событий API (UDP 127.0.0.1:50505) вместо msleep: демон должен
 * принимать подсказки плеера (frame_rate_hint) и afrd -s.
 * Возврат: 0 = ок (в т.ч. EINTR), -1 = poll сорвался. */
static int atv14_wait (int ms)
{
	struct pollfd pfd [4];
	memset (pfd, 0, sizeof (pfd));

	int n = apisock_prep_poll (pfd, ARRAY_SIZE (pfd));
	int rc = poll (pfd, n, ms);
	if (rc < 0)
		return (errno == EINTR) ? 0 : -1;

	if (rc > 0)
		apisock_handle (pfd, n);

	return 0;
}

/* Единственная точка, где id режима уходит в транзакцию 1035 (R44): id обязан
 * присутствовать в ТЕКУЩЕЙ таблице режимов, иначе SurfaceFlinger получил бы
 * чужой или уже не существующий id (сменилась нумерация, дамп не прочитался,
 * таблица ещё не получена). Заодно здесь же логируется провал применения.
 * Возврат: 0 — режим переключён; -1 — id нет в текущей таблице;
 *          -2 — транзакция не удалась. */
static int atv14_apply_mode (const platform_t *plat, int sf_id, const char *skipped)
{
	if (sf_id < 0)
		return -1;

	if ((g_sf_modes_n <= 0) ||
	    (sfmodes_current_hz (g_sf_modes, g_sf_modes_n, sf_id) == 0))
	{
		trace (1, "id %d отсутствует в текущей таблице режимов — %s\n",
		       sf_id, skipped);
		return -1;
	}

	if (sink_apply (plat->sink, g_mode_path, sf_id, NULL) != 0)
	{
		trace (1, "backdoor не применил режим id %d\n", sf_id);
		return -2;
	}

	return 0;
}

/* Отказ от удержания режима: частоту определить не удалось — вернуть исходный
 * режим, снять флаг backdoor и забыть состояние удержания. */
static void atv14_give_up (const platform_t *plat,
                           int *orig_sf_id, int *cur_sf_id, bool *held)
{
	if (!*held)
		return;

	/* устаревший id в 1035 не уходит: atv14_apply_mode проверит его по
	 * текущей таблице, а удержание снимется в любом случае */
	if (atv14_apply_mode (plat, *orig_sf_id,
	                      "возврат пропущен, флаг backdoor снят") == 0)
		*cur_sf_id = *orig_sf_id;

	sink_release_backdoor (plat->sink);
	guard_note_restored (held);
	*orig_sf_id = -1;
}

/* Время последнего успешного чтения, в котором декодер был активен.
 * Обновляется на КАЖДОЙ успешной выборке, в том числе внутри окна замера:
 * цикл на это время блокируется, и таймер «видео потерялось» не должен
 * принимать блокировку за потерю данных. */
static unsigned long long g_vdec_last_ok_us;

static int vdec_poll (const char *path, vdec_status_t *st)
{
	int rc = vdec_read (path, st);
	if ((rc == 0) && st->active)
		g_vdec_last_ok_us = now_us ();
	return rc;
}

/* PID процесса по имени (toybox pidof); -1, если PID определить не удалось. */
static pid_t pidof (const char *name)
{
	char cmd [64], out [64];
	snprintf (cmd, sizeof (cmd), "pidof %s", name);
	if (exec_capture (cmd, out, sizeof (out)) != 0)
		return -1;
	return (pid_t) atoi (out);
}

/* Статистика для GUI/API (afrd -s, status): legacy-структуры в ATV14 не
 * ведутся, поэтому отдаём id текущего режима и id режима возврата. */
static void atv14_stats_sync (int cur_sf_id, int orig_sf_id, bool held)
{
	g_atv14_state.valid = true;
	g_atv14_state.switched = held;
	g_atv14_state.current_hz =
		sfmodes_current_hz (g_sf_modes, g_sf_modes_n, cur_sf_id);
	g_atv14_state.original_hz = sfmodes_current_hz (g_sf_modes, g_sf_modes_n,
		held ? orig_sf_id : cur_sf_id);
}

/* Сброс кэша решений: нумерация id режимов изменилась (рестарт SurfaceFlinger).
 * Новый инстанс SF флаг mode-set-by-backdoor не держит, поэтому удержание
 * снимаем без sink_apply по устаревшему id — цикл замерит частоту и
 * перерезолвит режим заново по свежей таблице. */
static void atv14_invalidate (const platform_t *plat, int *want_sf_id,
                              int *orig_sf_id, int *cur_sf_id, bool *held,
                              bool *measure_failed, int *retries)
{
	*want_sf_id = -1;
	*measure_failed = false;
	*retries = 0;

	if (*held)
	{
		sink_release_backdoor (plat->sink);
		guard_note_restored (held);
		*orig_sf_id = -1;
		*cur_sf_id = -1;
	}
}

static int run_atv14 (const platform_t *plat)
{
	guard_action_t act;
	int orig_sf_id = -1, cur_sf_id = -1, want_sf_id = -1;
	bool held = false;
	bool video = false;
	vdec_status_t v0, v1;
	int retries = 0;
	/* частоту в этом видео определить не удалось — больше не мерить
	 * до его конца (сбрасывается, когда видео закончилось) */
	bool measure_failed = false;
	int preferred_id;

	/* Поведенческие ключи профиля (R28). */
	int prefer_api = cfg_get_int ("fps.prefer.api", 1);
	int pause_keep = cfg_get_int ("pause.keep", 1);
	int restore_on_stop = cfg_get_int ("switch.restore.on.stop", 1);

	/* Отслеживание «паузы»: декодер активен, а счётчик кадров не растёт. */
	unsigned long long prev_frames = 0;
	bool have_prev_frames = false;
	bool paused = false;
	int still = 0;

	int cur_hz = cfg_get_int ("trigger.poll.ms", 300);
	if (cur_hz < 100) cur_hz = 100;

	/* Страховки ATV14-цикла (R34). */
	int timeout_ms = cfg_get_int ("switch.timeout", DEFAULT_SWITCH_TIMEOUT);
	/* столько опросов подряд должен молчать декодер, чтобы отказ чтения
	 * считался серией (тот же порог, что у страховочного таймаута) */
	int max_vdec_fails = timeout_ms / cur_hz;
	if (max_vdec_fails < 1)
		max_vdec_fails = 1;
	/* время последнего успешного чтения с активным декодером —
	 * обновляется внутри vdec_poll() */
	g_vdec_last_ok_us = now_us ();
	int vdec_fails = 0;
	/* инвалидация таблицы режимов по PID SurfaceFlinger */
	int ticks = 0;

	/* Backdoor недоступен — переключать режимы нечем. Пассивный режим
	 * (спека §3.2.6): ни одного sink_apply, только обслуживание API, чтобы
	 * работали frame_rate_hint и afrd -s. */
	if (!plat->sf_backdoor)
	{
		trace (0, "backdoor SurfaceFlinger недоступен — пассивный режим\n");
		while (!g_shutdown)
			if (atv14_wait (cur_hz) != 0)
				msleep (cur_hz);
		return 0;
	}

	preferred_id = preferred_mode_id (plat);

	sf_modes_refresh (plat);

	trace (1, "предпочтительный режим ТВ: %d\n", preferred_id);

	/* страховка: снять «залипший» флаг прошлого запуска */
	sink_release_backdoor (plat->sink);

	/* PID SurfaceFlinger на старте: с ним сравнивает проверка рестарта ниже.
	 * Если pidof не ответил, останется -1 и первая проверка просто запомнит
	 * значение (без ложного «перезапуска»). */
	pid_t last_sf_pid = pidof ("surfaceflinger");
	if (last_sf_pid <= 0)
		last_sf_pid = -1;

	int window_ms = cfg_get_int ("fps.measure.window.ms", 1500);
	/* Предел расширения окна замера: пока fps_classify_window не даст
	 * однозначный ответ, окно растёт шагом window_ms/3 (см. цикл замера) */
	int window_max_ms = cfg_get_int ("fps.measure.window.max.ms", 6000);
	int max_retries = cfg_get_int ("fps.measure.retries", 3);
	int start_deb = cfg_get_int ("trigger.start.debounce.ms", 500);
	int stop_deb = cfg_get_int ("trigger.stop.debounce.ms", 1500);

	while (!g_shutdown)
	{
		/* g_mstime обслуживает mstime_* ; в этом цикле его больше никто
		 * не обновляет, а от него зависит срок жизни fps-подсказки API */
		mstime_update ();

		/* вместо msleep: заодно обслуживаем UDP-сокет API */
		if (atv14_wait (cur_hz) != 0)
			msleep (cur_hz);

		if (g_shutdown)
			break;

		ticks++;

		/* R34б: статистика для GUI и API (afrd -s, status) — в ATV14 её
		 * больше никто не обновляет */
		if (ticks % 3 == 0)
		{
			atv14_stats_sync (cur_sf_id, orig_sf_id, held);
			update_stats ();
		}

		/* Step 2: рестарт SurfaceFlinger. Признак — смена PID (проверяем редко,
		 * pidof — это fork/exec) либо смена самой таблицы режимов (видна сразу
		 * в дампе, который всё равно читается каждый цикл). */
		if (ticks % 16 == 0)
		{
			pid_t sf_pid = pidof ("surfaceflinger");

			if (sf_pid <= 0)
			{
				/* PID не определился — проверка недостоверна */
			}
			else if ((last_sf_pid > 0) && (sf_pid != last_sf_pid))
			{
				last_sf_pid = sf_pid;
				trace (1, "SurfaceFlinger: новый pid %d, перечитываю режимы\n",
				       (int) sf_pid);
				sf_modes_refresh (plat);
				g_sf_table_changed = false;
				preferred_id = preferred_mode_id (plat);
				trace (1, "предпочтительный режим ТВ: %d\n", preferred_id);
				atv14_invalidate (plat, &want_sf_id, &orig_sf_id, &cur_sf_id,
				                  &held, &measure_failed, &retries);
				trace (1, "SF перезапустился: удержание снято, режимы перерезолвятся\n");
			}
			else
				last_sf_pid = sf_pid;	/* первая достоверная проверка */
		}

		if (vdec_poll (plat->vdec_status, &v0) != 0)
		{
			/* R34г: серия отказов чтения — сообщаем один раз, без шторма */
			if (++vdec_fails == max_vdec_fails)
				trace (0, "vdec_status не читается: %d отказов подряд (%d мс)\n",
				       vdec_fails, vdec_fails * cur_hz);

			/* R34а: страховка «видео потерялось». Проверяется только на отказе
			 * чтения: возраст таймера отсчитывается от последней УСПЕШНОЙ
			 * выборки, поэтому медленная не-vdec работа хвоста итерации
			 * (dumpsys SurfaceFlinger, service call) за потерю данных не
			 * принимается. Декодер молчит дольше страховочного таймаута,
			 * а режим всё ещё удержан — снимаем удержание, иначе флаг
			 * backdoor залипнет до перезагрузки. */
			if (held && (now_us () - g_vdec_last_ok_us) > (unsigned long long) timeout_ms * 1000)
			{
				trace (0, "видео потеряно: %d мс без данных декодера — возвращаю режим\n",
				       (int) ((now_us () - g_vdec_last_ok_us) / 1000));
				atv14_give_up (plat, &orig_sf_id, &cur_sf_id, &held);
				video = false;
				want_sf_id = -1;
			}
			continue;
		}
		vdec_fails = 0;

		bool new_video = v0.active;

		if (new_video != video)
		{
			msleep (new_video ? start_deb : stop_deb);

			vdec_status_t again;
			if (vdec_poll (plat->vdec_status, &again) == 0 && again.active == new_video)
				video = new_video;
			else
				continue;	/* дребезг старта/стопа — игнорируем */
		}

		/* «пауза»: счётчик кадров не растёт на двух соседних опросах подряд */
		if (v0.active)
		{
			/* R34в: новое видео началось раньше, чем истёк stop_deb —
			 * декодер поднял новый инстанс, счётчик сбросился. Старое
			 * решение к новому видео не относится: режим уже наш,
			 * а частоту мерим заново. */
			if (have_prev_frames && (v0.frames < prev_frames))
			{
				trace (1, "счётчик кадров сброшен — новое видео, мерю заново\n");
				want_sf_id = -1;
				measure_failed = false;
				retries = 0;
			}

			if (have_prev_frames && (v0.frames == prev_frames))
				still++;
			else
				still = 0;

			paused = (still >= 2);
			prev_frames = v0.frames;
			have_prev_frames = true;
		}
		else
		{
			still = 0;
			paused = false;
			have_prev_frames = false;
		}

		/* pause.keep==0: пауза обрабатывается как стоп (guard вернёт режим) */
		bool video_eff = video;
		if (!pause_keep && paused)
			video_eff = false;

		/* R50: сброс состояния замера висит на video_eff, а не на video.
		 * Пауза при pause.keep==0 для замера равносильна концу видео: пока
		 * счётчик кадров стоит, окно замера бессмысленно (и, исчерпав
		 * fps.measure.retries, оно выставляло measure_failed, который затем
		 * сбрасывался только по концу видео — после возобновления AFR больше
		 * не включался). При pause.keep==1 video_eff == video, поведение
		 * прежнее. */
		if (!video_eff)
		{
			want_sf_id = -1;
			/* видео закончилось — следующее мерим заново: и счётчик попыток,
			 * и признак «частота не определилась» относятся к текущему видео */
			measure_failed = false;
			retries = 0;
		}
		else if (!measure_failed && (want_sf_id < 0))
		{
			int hz = 0;

			/* fps.prefer.api: подсказку плеера (24.8 fixed-point, живёт 1 с)
			 * предпочитаем собственному замеру — она точнее и мгновенна */
			int api_hz = 0;
			if (prefer_api && mstime_running (&g_frame_rate_hint.stamp))
				api_hz = g_frame_rate_hint.fps;

			if (api_hz)
			{
				hz = fps_rate_hz (fps_rate_index_for_hz (api_hz));
				trace (1, "видео: " HZ_FMT " Гц по подсказке плеера, ищу режим\n",
				       HZ_ARGS (hz));
			}
			else
			{
				/* Замер: окно window_ms, разбитое на под-срезы slice_ms,
				 * с расширением до window_max_ms, пока частота не будет
				 * определена однозначно.
				 * Счётчик кадров дискретен (±1 кадр) и метки времени имеют
				 * неопределённость, поэтому короткое окно не различает соседние
				 * частоты: граница 24 -> 25 проходит на 24.49 fps, а один кадр
				 * за 1.5 с — это 0.67 fps. Живой случай: «24.642 fps -> 25 Гц»
				 * (R47), из-за чего 24-фпс поток уводился в 1080p25hz.
				 * Выборка годится, только если декодер активен всё окно и
				 * счётчик строго растёт в каждом под-срезе: если середина
				 * окна попала на паузу/буферизацию, дельта окажется меньше
				 * реальной, и частота «уедет» в чужой стандарт (напр. 50 Гц
				 * при полуокне простоя дадут ровно 25 Гц). v0 читаем здесь
				 * же: к этому моменту мог пройти дебаунс старта, и прочитанное
				 * в начале цикла значение было бы на дебаунс старше.
				 * Метка времени выборки — середина чтения: само чтение не
				 * мгновенно (под нагрузкой — десятки мс), а интервал считают
				 * крайние выборки, и перекос между ними уводит частоту. */
				unsigned long long read_a = now_us ();
				if (vdec_poll (plat->vdec_status, &v0) != 0)
					continue;
				unsigned long long read_b = now_us ();
				unsigned long long t0 = read_a + (read_b - read_a) / 2;
				unsigned long long read0_us = read_b - read_a;

				int slice_ms = window_ms / 3;
				if (slice_ms < 1)
					slice_ms = 1;

				bool grown = v0.active;
				v1 = v0;
				int sliced_ms = 0, decided_ms = 0;
				unsigned long long t1 = t0, dt_us = 0, dt_unc_us = 0;
				unsigned long long frames_delta = 0;
				double fps = 0.0;

				while (grown && (sliced_ms < window_max_ms))
				{
					msleep (slice_ms);
					sliced_ms += slice_ms;

					vdec_status_t cur;
					unsigned long long ca = now_us ();
					if (vdec_poll (plat->vdec_status, &cur) != 0)
					{
						grown = false;
						break;
					}
					unsigned long long cb = now_us ();

					if (!cur.active || (cur.frames <= v1.frames))
					{
						grown = false;
						break;
					}

					v1 = cur;
					t1 = ca + (cb - ca) / 2;

					if (sliced_ms < window_ms)
						continue;

					frames_delta = v1.frames - v0.frames;
					dt_us = (t1 > t0) ? (t1 - t0) : 0;
					/* полусумма длительностей крайних чтений: оценка
					 * сверху для разброса середин чтений */
					dt_unc_us = (read0_us + (cb - ca)) / 2;
					fps = dt_us
					    ? (double)frames_delta * 1000000.0 / (double)dt_us
					    : 0.0;

					if (fps_classify_window (frames_delta, dt_us, dt_unc_us, &hz))
					{
						decided_ms = sliced_ms;
						break;
					}
				}

				if (!hz)
				{
					if (++retries >= max_retries)
					{
						trace (1, "частоту определить не удалось (%d попыток), пропускаю\n", retries);
						measure_failed = true;
						want_sf_id = -1;
						/* как и выше: при сменившейся таблице режимов
						 * orig_sf_id может быть из старой нумерации */
						if (!g_sf_table_changed)
							atv14_give_up (plat, &orig_sf_id, &cur_sf_id, &held);
					}
					continue;
				}

				retries = 0;
				trace (1, "видео: %.3f fps (%s), окно %d мс (ΔF=%llu, ±%.0f мс), ищу режим\n",
				       fps, fps_rate_name (fps_rate_index_for_hz (hz)), decided_ms,
				       frames_delta, (double)dt_unc_us / 1000.0);
			}

			retries = 0;

			/* текущий режим / исходный id */
			if (g_sf_modes_n <= 0)
				sf_modes_refresh (plat);

			sf_mode_t active;
			int w = 1920, h = 1080;
			if (sf_active_mode (plat, &active) == 0)
			{
				w = active.width;
				h = active.height;
			}

			cur_sf_id = sfmodes_find (g_sf_modes, g_sf_modes_n, hz, w, h);
			if (cur_sf_id < 0)
			{
				trace (1, "режим для %d.%02d Гц не найден в списке\n",
				       HZ_ARGS (hz));
				measure_failed = true;
				want_sf_id = -1;

				/* Если таблица режимов сменилась прямо во время замера, id в
				 * orig_sf_id — из старой нумерации, и в транзакцию 1035 его
				 * отправлять нельзя (R41/R43). Удержание снимет блок обработки
				 * g_sf_table_changed на следующей итерации. */
				if (!g_sf_table_changed)
					atv14_give_up (plat, &orig_sf_id, &cur_sf_id, &held);
				continue;
			}
			want_sf_id = cur_sf_id;
		}

		/* актуальный режим перечитываем у SurfaceFlinger: так мы видим и то,
		 * что переключило приложение со своим AFR, и не расходимся с реальностью */
		sf_mode_t active;
		if (sf_active_mode (plat, &active) == 0)
			cur_sf_id = active.id;

		/* Таблица режимов сменилась (рестарт SurfaceFlinger) — нумерация id
		 * уже другая, и кэшированный want_sf_id в транзакцию 1035 пускать
		 * нельзя. Проверка по PID (выше) срабатывает реже, а этот признак
		 * виден сразу: он и закрывает окно до неё. */
		if (g_sf_table_changed)
		{
			g_sf_table_changed = false;

			/* рестарт уже учтён по таблице — чтобы проверка PID не сработала
			 * по тому же событию второй раз */
			pid_t sf_pid = pidof ("surfaceflinger");
			if (sf_pid > 0)
				last_sf_pid = sf_pid;

			preferred_id = preferred_mode_id (plat);
			trace (1, "таблица режимов SurfaceFlinger сменилась: снимаю удержание, перерезолвлю режим\n");
			atv14_invalidate (plat, &want_sf_id, &orig_sf_id, &cur_sf_id,
			                  &held, &measure_failed, &retries);
			continue;
		}

		act = guard_decide (video_eff, want_sf_id, cur_sf_id, &orig_sf_id, &held);

		if (act == GUARD_SWITCH && (preferred_id >= 0))
			orig_sf_id = preferred_id;	/* возврат — в предпочтительный режим ТВ */

		if (act == GUARD_SWITCH)
		{
			if (atv14_apply_mode (plat, want_sf_id, "переключение пропущено") == 0)
				cur_sf_id = want_sf_id;
		}
		else if (act == GUARD_RESTORE)
		{
			/* Возврат по настоящему стопу (декодер исчез) отключается ключом
			 * switch.restore.on.stop; возврат по паузе (pause.keep==0)
			 * выполняется всегда. Флаг backdoor снимаем в любом случае. */
			if (restore_on_stop || v0.active)
			{
				int rc = atv14_apply_mode (plat, orig_sf_id,
				                           "возврат пропущен, флаг backdoor снят");
				if (rc == 0)
				{
					guard_note_restored (&held);
					cur_sf_id = orig_sf_id;
					orig_sf_id = -1;
				}
				else if (rc == -1)
				{
					/* id исчез из таблицы (сменилась нумерация) — возвращать
					 * нечего: удержание снимаем, иначе каждый оборот цикла
					 * повторяли бы эту ветку и лог */
					guard_note_restored (&held);
					orig_sf_id = -1;
				}
			}
			else
			{
				guard_note_restored (&held);
				orig_sf_id = -1;
			}
			sink_release_backdoor (plat->sink);
			want_sf_id = -1;
			continue;
		}
		else if (act == GUARD_NONE && !video && held)
		{
			sink_release_backdoor (plat->sink);
			guard_note_restored (&held);
		}
	}

	/* выход: гарантированно вернуть систему в исходное состояние.
	 * Id из кэша проверяет atv14_apply_mode: если его в текущей таблице уже
	 * нет (или таблица неизвестна), в 1035 он не уходит — снимаем только флаг. */
	if (held)
	{
		atv14_apply_mode (plat, orig_sf_id, "возврат пропущен, флаг backdoor снят");
		sink_release_backdoor (plat->sink);
		guard_note_restored (&held);
	}

	return 0;
}

/* --------- * --------- * --------- * --------- * --------- * --------- */

static platform_t g_plat;

int afrd_run ()
{
	g_plat = platform_detect ();

	if (g_plat.kind == PLAT_ATV14)
		return run_atv14 (&g_plat);

	if (g_uevent_sock == -1)
		return -1;

	int ret = 0;

	trace (1, "afrd running\n");
	mstime_update ();

	struct pollfd pfd [16];
	pfd [0].events = POLLIN;
	pfd [0].fd = g_uevent_sock;

	mstime_disable (&g_ost_switch);
	mstime_disable (&g_ost_hdmi);
	mstime_disable (&g_ost_blackout);
	mstime_disable (&g_ost_off);
	mstime_disable (&g_ost_hdcp);

	// Check config timestamp timer
	mstime_arm (&g_ost_config, 1);
	g_config_mtime = mtime (g_config);

	// Check HDCP status regularily
	mstime_arm (&g_ost_hdcp_check, 8000);

	update_stats ();

	while (!g_shutdown) {
		// flush log to disk
		trace_sync ();

		// update the millisecond timer
		safe_mstime_update (0);

		int to = mstime_left (&g_ost_switch);
		to = min_time (to, &g_ost_hdmi);
		to = min_time (to, &g_ost_blackout);
		to = min_time (to, &g_ost_config);
		to = min_time (to, &g_ost_hdcp_check);

		// this should never happen as g_ost_config is always active, but
		// anyway don't allow to sleep indefinitely 'cause we can't detect
		// system time changes
		if (to < 0)
			to = 60000;

		// wait until either a new uevent comes
		// or the delayed mode switch timer expires
		pfd [0].revents = 0;
		// add API sockets into the pool
		int n_pfd = 1 + apisock_prep_poll (pfd + 1, ARRAY_SIZE (pfd) - 1);
		int rc = poll (pfd, n_pfd, to);

		// catch system time change events, this breaks our timers
		safe_mstime_update (to);

		if (rc > 0) {
			if (pfd [0].revents & POLLIN)
				handle_uevents ();
			apisock_handle (pfd, n_pfd);
		}

		// disable screen at start of playback
		if (mstime_expired (&g_ost_blackout) && !g_state.restore)
			blackout ();

		// if mode switch timer expired, switch the mode finally
		if (mstime_expired (&g_ost_switch)) {
			mstime_disable (&g_ost_switch);
			framerate_switch (false);
		}

		// query supported video modes after HDMI has been plugged on
		if (mstime_expired (&g_ost_hdmi)) {
			mstime_disable (&g_ost_hdmi);
			handle_hdmi_switch (-1);
		}

		// check config timestamp and reload it if so
		if (mstime_expired (&g_ost_config)) {
			// if we're doing other work, don't hog the CPU
			if (!mstime_enabled (&g_ost_blackout) &&
			    !mstime_enabled (&g_ost_switch) &&
			    !mstime_enabled (&g_ost_hdmi)) {
				mstime_arm (&g_ost_config, CONFIG_CHECK_PERIOD);
				time_t cmt = mtime (g_config);
				if ((cmt != 0) && (cmt != g_config_mtime)) {
					trace (1, "config file %s changed, reloading\n", g_config);
					ret = 1;
					break;
				}
			} else
				mstime_arm (&g_ost_config, 1000);
		}

		// check if HDCP is supported but disabled
		if (mstime_expired (&g_ost_hdcp_check)) {
			mstime_arm (&g_ost_hdcp_check, 8000);
			hdcp_check ();
		}
	}

	// restore framerate just in case
	g_state.restore = true;
	framerate_switch (false);
	return ret;
}

/* --------- * --------- * --------- * --------- * --------- * --------- */

void afrd_frame_rate_hint (int hz)
{
	g_frame_rate_hint.fps = hz;
	mstime_arm (&g_frame_rate_hint.stamp, 1000);
}

void afrd_refresh_rate (int hz)
{
	/* в ATV14-режиме принудительный refresh_rate не поддержан: переключением
	 * управляет только run_atv14 (иначе API молча дёргал бы legacy-состояние
	 * g_state/framerate_switch без всякого эффекта) */
	if (g_plat.kind == PLAT_ATV14)
	{
		trace (1, "refresh_rate в ATV14 не поддержан\n");
		return;
	}

	bool ok = (hz && (hz >= HZ_MIN) && (hz < HZ_MAX));
	if (!ok)
		hz = 0;

	g_state.restore = !ok;
	g_state.hz = hz;
	framerate_switch (true);

}

void afrd_reconf ()
{
	mstime_arm (&g_ost_config, 0);
	g_config_mtime = (time_t)-1;
}

/* --------- * --------- * --------- * --------- * --------- * --------- */

static void blacklist_rates_load (const char *kw)
{
	g_mode_blacklist_rates_count = 0;

	const char *str = cfg_get_str (kw, NULL);
	if (!str)
		return;

	trace (1, "\tloading blacklisted rates\n");

	char *tmp = strdup (str);
	char *cur = tmp;
	while (*cur) {
		cur += strspn (cur, spaces);
		char *next = cur + strcspn (cur, spaces);
		if (*next)
			*next++ = 0;

		float rate;
		if ((sscanf (cur, "%f", &rate) == 1) && (rate >= 1) && (rate <= 1000) &&
		    (g_mode_blacklist_rates_count < ARRAY_SIZE (g_mode_blacklist_rates))) {
			int irate = (int)(256.0 * rate + 0.5);
			g_mode_blacklist_rates [g_mode_blacklist_rates_count] = irate;
			g_mode_blacklist_rates_count++;
			trace (2, "\t+ "HZ_FMT"Hz\n", HZ_ARGS (irate));
		}

		cur = next;
	}

	free (tmp);
}

int afrd_init ()
{
	/* load config if not loaded already */
	if (!g_cfg && (load_config (g_config) != 0))
		return -1;

	shmem_init (false);
	androp_init ();

	int log_enable = (cfg_get_int ("log.enable", 1) != 0);
	const char *log_file = cfg_get_str ("log.file", NULL);
	if (log_file && log_enable)
		trace_log (log_file);

	trace (1, "afrd v%s%s built at %s is initializing\n", g_version, g_ver_sfx, g_bdate);

	struct utsname un;
	if (uname (&un) != 0)
		memset (&un, 0, sizeof (un));
	trace (1, "\tlinux kernel %s\n", un.release);
	trace (1, "\tandroid build %s\n", androp_get ("ro.build.description"));
	trace (1, "\tmanufacturer %s board %s device %s model %s\n",
		androp_get ("ro.product.manufacturer"),
		androp_get ("ro.product.board"),
		androp_get ("ro.product.device"),
		androp_get ("ro.product.model"));

	trace (1, "\tActive config file: %s\n", g_config);

	g_enable = (cfg_get_int ("enable", 1) != 0);

	g_hdmi_dev = cfg_get_str ("hdmi.sysfs", DEFAULT_HDMI_DEV);
	g_hdmi_state = cfg_get_str ("hdmi.state", DEFAULT_HDMI_STATE);

	g_mode_path = cfg_get_str ("mode.path", DEFAULT_VIDEO_MODE);
	g_mode_prefer_exact = cfg_get_int ("mode.prefer.exact", DEFAULT_MODE_PREFER_EXACT);
	g_mode_use_fract = cfg_get_int ("mode.use.fract", DEFAULT_MODE_USE_FRACT);
	blacklist_rates_load ("mode.blacklist.rates");

	trace (1, "\trefresh rate selection: use fractional %d, exact %d\n",
		g_mode_use_fract, g_mode_prefer_exact);

	g_switch_delay_on = cfg_get_int ("switch.delay.on", DEFAULT_SWITCH_DELAY_ON);
	g_switch_delay_off = cfg_get_int ("switch.delay.off", DEFAULT_SWITCH_DELAY_OFF);
	g_switch_delay_retry = cfg_get_int ("switch.delay.retry", DEFAULT_SWITCH_DELAY_RETRY);
	g_switch_timeout = cfg_get_int ("switch.timeout", DEFAULT_SWITCH_TIMEOUT);
	g_switch_blackout = cfg_get_int ("switch.blackout", DEFAULT_SWITCH_BLACKOUT);
	g_switch_ignore = cfg_get_int ("switch.ignore", DEFAULT_SWITCH_IGNORE);
	g_switch_hdmi = cfg_get_int ("switch.hdmi", DEFAULT_SWITCH_HDMI);

	trace (1, "\tswitch delays: on %d, off %d, retry %d ms\n",
		g_switch_delay_on, g_switch_delay_off, g_switch_delay_retry);
	trace (1, "\t\ttimeout %d ms, blackout %d ms, ignore %d ms\n",
		g_switch_timeout, g_switch_blackout, g_switch_ignore);

	g_vdec_sysfs = cfg_get_str ("vdec.sysfs", DEFAULT_VDEC_SYSFS);
	strlist_load (&g_vdec_blacklist, "vdec.blacklist", "vdec blacklist");
	strlist_load (&g_frhint_vdec_blacklist, "frhint.vdec.blacklist", "frhint vdec blacklist");
	uevent_filter_load (&g_filter_frhint, "uevent.filter.frhint");
	uevent_filter_load (&g_filter_vdec, "uevent.filter.vdec");
	uevent_filter_load (&g_filter_hdmi, "uevent.filter.hdmi");
	uevent_filter_load (&g_filter_hdcp, "uevent.filter.hdcp");

	if (!uevent_open (16 * 1024)) {
		trace (0, "failed to open uevent socket");
		return EPERM;
	}

	colorspace_init ();
	apisock_init ();
	handle_hdmi_switch (1);

	return 0;
}

void afrd_fini ()
{
	handle_hdmi_switch (0);
	apisock_fini ();
	colorspace_fini ();

	if (g_uevent_sock != -1) {
		close (g_uevent_sock);
		g_uevent_sock = -1;
	}

	uevent_filter_fini (&g_filter_frhint);
	uevent_filter_fini (&g_filter_vdec);
	uevent_filter_fini (&g_filter_hdmi);
	uevent_filter_fini (&g_filter_hdcp);
	strlist_free (&g_vdec_blacklist);
	g_mode_blacklist_rates_count = 0;

	g_enable = false;
	g_hdmi_dev = NULL;
	g_hdmi_state = NULL;
	g_mode_path = NULL;
	g_vdec_sysfs = NULL;

	if (g_cfg) {
		cfg_free (g_cfg);
		g_cfg = NULL;
	}

	shmem_fini ();
	androp_fini ();
	trace_log (NULL);
}

void afrd_emerg ()
{
	shmem_emerg ();
	if (g_state.orig_mode.name [0])
		framerate_restore (false);
}
