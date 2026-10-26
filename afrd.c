/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

#include "afrd.h"
#include "uevent_filter.h"
#include "colorspace.h"
#include "modes_sel.h"
#include "androp.h"
#include "platform.h"
#include "vdec.h"
#include "sfmodes.h"
#include "sink.h"
#include "guard.h"
#include "exec.h"
#include "fps_class.h"
#include "fps_env.h"
#include "cfgkeys.h"
#include "reconf.h"
#include "hdrinfo.h"
#include "pkgdet.h"
#include "pause.h"
#include "stuck.h"
#include "ceiling.h"
#include "dispserve.h"

#define __USE_GNU
#include <math.h>
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

/* Выдержка перед повторным чтением HDR/SDR-атрибутов после смены режима:
 * система выставляет их асинхронно, значения «в момент» ещё старые (та же
 * причина, по которой цвета применяются второй раз; см. fps.cadence.fix.ms). */
#define HDRINFO_SETTLE_MS	2500

const char *g_hdmi_dev = NULL;
const char *g_mode_path = NULL;
const char *g_vendor_disp_cli = NULL;
/* Постоянный помощник смены режима (vendor.disp.persist, Android 10/11):
 * убирает из каждой смены разовый запуск app_process (~490 мс, журнал §49.9). */
int g_vendor_disp_persist = 1;
/* Быстрое решение по заявленной частоте кадра (fps.declare.fast): заполняется
 * в afrd_run — умолчание зависит от пути (на Android 10/11 включено). */
int g_fps_declare_fast = 0;

/* Подъём режима до максимума при старте видео (switch.bump). Нужен на
 * Android 11+: плеер фиксирует «потолок» частоты в начале сессии проигрывания,
 * и если в этот момент дисплей медленнее контента, темп не поднимется никогда
 * (журнал §47.6). Значение читается в afrd_run. */
static int g_switch_bump = 0;

/* Сброс «потолка» частоты у плеера (switch.ceiling.reset, Android 12-14).
 * Плеер запоминает частоту дисплея в начале сессии проигрывания и внешнюю
 * смену режима не перечитывает; потолок обновляется только сменой
 * конфигурации экрана — её и делаем изменением плотности на единицу с
 * немедленным возвратом (журнал §47.9). */
static int g_ceiling_reset = 0;
static int g_ceiling_resets = 0;	/* сбросов на текущем видео */
static int g_ceiling_hz = 0;		/* частота, до которой потолок поднят */
static unsigned long long g_ceiling_us = 0;	/* время последней попытки */
static bool g_ceiling_restart_expected = false;	/* ждём перезапуск декодера */
static int g_ceiling_mode_id = -1;	/* режим, при котором сбрасывали потолок */
/* После нашего сброса потолка быстрый путь по заявленной частоте выключен до
 * конца видео: сброс перезапускает декодер, драйвер заявляет 30 fps, и быстрое
 * решение возвращало режим на максимум — получалась петля 50 ↔ 60 каждые
 * ~20 с (живая находка 18.10 на .42). Замеру это не мешает: он и так решает. */
static bool g_ceiling_no_fast = false;

/* Подъём режима при выходе плеера на передний план (switch.bump.on.app).
 * На Android 10/11 плеер фиксирует частоту раньше, чем появляется канал
 * декодера, поэтому подъём «по видео» опаздывает; передний план виден на
 * 260-450 мс от старта ролика — до создания поверхности (журнал §49.13). */
static int g_switch_bump_on_app = 0;

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

/* Минимальная частота режима для конкретных плееров (mode.min.hz.list):
 * некоторые плееры выбирают темп отрисовки в момент старта ролика по текущему
 * режиму экрана и потом его не поднимают, поэтому для них низкие режимы не
 * выбираем (проверено живьём на YouTube TV, см. README, ограничение 8). */
#define MODE_MIN_HZ_MAX		8
#define MODE_MIN_HZ_PKG_LEN	PKG_NAME_LEN
typedef struct
{
	char pkg [MODE_MIN_HZ_PKG_LEN];
	int hz;			/* 24.8 fixed-point */
} mode_min_hz_t;
static mode_min_hz_t g_mode_min_hz [MODE_MIN_HZ_MAX];
static int g_mode_min_hz_count;

static bool g_enable;                 // enabled in config file

static uevent_filter_t g_filter_frhint;
static uevent_filter_t g_filter_vdec;
static uevent_filter_t g_filter_hdmi;
static uevent_filter_t g_filter_hdcp;

static strlist_t g_vdec_blacklist;
static strlist_t g_frhint_vdec_blacklist;

/* Пакеты из switch.bump.pkg: кому применяются механики борьбы с «зажимом»
 * (подъём режима до максимума и сброс «потолка» частоты).
 *
 * Зажим измерен только у штатного YouTube TV (журнал §28.9, §46.6, §47.2);
 * у остальных плееров он не подтверждён, а механики не бесплатны: сброс
 * потолка — это смена конфигурации экрана, и часть плееров на неё
 * перезапускает Activity (живой случай: OTT Navigator возвращался к
 * предыдущему каналу при первом срабатывании AFR). Поэтому механики
 * применяются только перечисленным пакетам; пустой список — никому. */
static strlist_t g_bump_pkgs;
/* Пакеты из switch.skip.pkg: на Android 10-14 демон их мерит и пишет в журнал,
 * но режим не переключает и не удерживает — плеер сам выбирает темп отрисовки
 * под текущий режим экрана, а удержанный низкий режим «зажимал» бы следующий
 * ролик (проверено живьём на YouTube TV, см. README, ограничение 8). На
 * Android 6-9 гейт стоит до запуска измерения: плеер не измеряется, в журнале
 * только факт исключения. */
static strlist_t g_skip_pkgs;

/* Текущий путь работы (заполняется в afrd_run); нужен и старому пути — по нему
 * pkg_name() понимает, откуда брать пакет переднего плана. */
static platform_t g_plat;

/* Список режимов ТВ не прочитан: предупреждаем один раз, а не на каждой
 * итерации цикла. Сбрасывается успешным обновлением списка. */
static bool g_modes_unreadable;

/* «Записка» прошлого экземпляра демона из разделяемой памяти: читается в
 * afrd_init до её затирания и разбирается в ATV14-цикле (atv14_recover_stale). */
static afrd_shmem_t g_prev_stats;
static bool g_prev_stats_valid;

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

/* Признак жизни для сторожа приложения (R57): heartbeat растёт на каждой
 * итерации цикла, поэтому в shmem меняется CRC-штамп. Мёртвый демон его не
 * обновляет — приложение это видит и перезапускает демон (само оно процессы
 * root не наблюдает: /proc смонтирован с hidepid=invisible). Дёшево: 60 байт. */
static void stats_heartbeat ()
{
	g_afrd_stats.heartbeat++;
	shmem_update ();
}

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

/* Минимальная частота режима (24.8) для пакета; 0 — ограничения нет
 * (mode.min.hz.list, определение ниже). */
static int mode_min_hz_for (const char *pkg);

/* Приложение на переднем плане (определение ниже). Нужно гейту по списку
 * switch.bump.pkg: подъём режима случается раньше первого замера, где пакет
 * определяется в обычном порядке. */
static int pkg_probe (const platform_t *plat, char *out, size_t out_sz);

/* Пакет плеера текущего видео и решение по switch.skip.pkg (определения ниже:
 * нужны framerate_switch/delay_framerate_switch, которые идут раньше). */
static const char *pkg_name (void);
static bool pkg_skip_active (void);

/* Сброс кэша пакета на старте нового видео (определение ниже; нужен
 * обработчику vdec-uevent, который идёт раньше). */
static void pkg_cache_invalidate (void);

static bool rate_allowed_cb (int hz, void *ctx);

/* Выбор режима из g_modes под частоту g_state.hz (правила старого пути).
 * floor — минимальная допустимая частота режима в формате 24.8
 * (mode.min.hz.list) или 0.
 * Возврат: true — режим найден и записан в *best.
 *
 * Сами правила живут в modes_sel.c: они нужны и новому пути на Android 10/11
 * (там режим задаётся именем через вендорный SystemControl), а две копии
 * рейтинга разошлись бы. */
static bool select_mode (int floor, display_mode_t *best)
{
	if (modes_select_by_hz (g_modes, g_modes_n, g_state.hz,
	                        g_current_mode.width, g_current_mode.height,
	                        g_current_mode.interlaced, g_mode_prefer_exact, floor,
	                        rate_allowed_cb, NULL, best, NULL, 0, NULL) != 0)
	{
		best->name [0] = 0;
		return false;
	}

	return true;
}

/* Фильтр чёрного списка частот для modes_select_by_hz: true — частота разрешена */
static bool rate_allowed_cb (int hz, void *ctx)
{
	(void)ctx;
	return !rate_is_blacklisted (hz);
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

	/* switch.skip.pkg — вторая точка решения: сюда приходят и по таймеру,
	 * и от API, минуя delay_framerate_switch. */
	if (pkg_skip_active ())
	{
		trace (1, "плеер %s в switch.skip.pkg — режим не переключаю\n", pkg_name ());
		if (g_state.orig_mode.name [0])
			framerate_restore (false);
		else
		{
			memset (&g_state, 0, sizeof (g_state));
			update_stats ();
		}
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

	/* Текущий режим перечитываем ПЕРЕД решением: его мог сменить кто-то
	 * другой — штатная АПЧ Amlogic, плеер со своим автоподбором, пользователь
	 * через sysfs, — а uevent смены режима на ядре 4.9
	 * (vout/extcon/setmode) мы не разбираем («Unrecognized uevent»).
	 * Без перечитывания кэш g_current_mode сообщает режим, которого на экране
	 * уже нет, и решение «режим и так подходящий» пропускает переключение:
	 * живой замер 2026-10-15 на стенде .76 — режим выставлен извне в
	 * 1080p25hz, демон считал текущим 1080p60hz и на 60-фпс ролике ответил
	 * «Skipping mode switch since current refresh is close enough», оставив
	 * видео на 25 Гц. Заодно это влияет на геометрию (width/height/interlaced)
	 * в select_mode: после смены разрешения извне режим выбирался бы для
	 * прежнего. */
	display_mode_get_current ();

	trace (1, "Current mode is "DISPMODE_FMT"\n",
		DISPMODE_ARGS (g_current_mode, display_mode_hz (&g_current_mode)));
	trace (1, "Looking for display mode closest to %dx%d@"HZ_FMT"Hz\n",
		g_current_mode.width, g_current_mode.height, HZ_ARGS (g_state.hz));

	/* Порог частоты для плеера текущего видео (mode.min.hz.list); если под
	 * порогом не осталось ни одного режима, берём обычное правило. */
	int floor = g_mode_min_hz_count ? mode_min_hz_for (pkg_name ()) : 0;
	display_mode_t best_mode;
	if (!select_mode (floor, &best_mode) && floor)
	{
		trace (1, "нет режима не ниже " HZ_FMT " Гц (mode.min.hz.list) — "
		       "выбираю обычным правилом\n", HZ_ARGS (floor));
		select_mode (0, &best_mode);
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

	/* switch.skip.pkg: этому плееру AFR мешает — режим не переключаем.
	 * Если что-то уже удержано (плеер внесли в список на ходу), возвращаем
	 * исходный режим. */
	if (!restore && pkg_skip_active ())
	{
		trace (1, "плеер %s в switch.skip.pkg — режим не переключаю\n", pkg_name ());
		if (g_state.orig_mode.name [0])
			framerate_restore (false);
		update_stats ();
		return;
	}

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

/* Обновление списка режимов ТВ по пути платформы.
 *
 * Ключ определяет платформа, а не «кто первый»: на вендорном пути Android 10/11
 * таблицы режимов SurfaceFlinger нет, и список читается из vendor.disp.cap
 * (спека §5). Прежде на этом пути список попадал из hdmi.sysfs, а vendor.disp.cap
 * был мёртвым ключом (нашло финальное ревью). */
/* Список режимов читается при старте и на событиях HDMI, а не в mode_resolve:
 * иначе нечитаемый ключ давал бы по строке в журнал на каждую итерацию цикла
 * (~300 мс), как это уже было с отказом вендорного помощника (финальное ревью). */
static void modes_refresh_platform (void)
{
	if (g_plat.sink == SINK_VENDOR_MODE)
	{
		int rc = display_modes_init_path (g_plat.disp_cap);

		if ((rc != 0) && !g_modes_unreadable)
			trace (0, "список режимов ТВ не прочитан (%s)"
			          " — переключать не из чего\n",
			       g_plat.disp_cap ? g_plat.disp_cap : "<путь не задан>");

		g_modes_unreadable = (rc != 0) || (g_modes_n <= 0);
		return;
	}

	display_modes_init ();
	g_modes_unreadable = (g_modes_n <= 0);
}

/* Освобождение списка режимов ТВ и всего, что к нему привязано.
 *
 * Вынесено из handle_hdmi_switch: afrd_fini() освобождает список НАМЕРЕННО
 * (перед переинициализацией при перезагрузке конфига), и печатать на этом пути
 * «HDMI not active» нельзя — человек, читающий журнал, делает вывод о сбое HDMI.
 * Ложное сообщение стояло в журнале рядом с «config file … changed, reloading»
 * и уже один раз привело к разбору несуществующей поломки (журнал §42.1).
 * Заодно убран hdcp_fini() при переинициализации: он обнуляет только кэш
 * состояния, который hdcp_init() всё равно читает из sysfs заново. */
static void modes_release (void)
{
	display_modes_fini ();
	memset (&g_state.orig_mode, 0, sizeof (g_state.orig_mode));
}

static void handle_hdmi_switch (int state)
{
	if (state == -1)
		state = sysfs_get_int (g_hdmi_state, NULL);

	if (state <= 0) {
		/* только настоящее событие HPD: «нет HDMI» здесь правда, а не
		 * следствие переинициализации (см. modes_release) */
		trace (1, "HDMI not active, clearing video mode list\n");
		hdcp_fini ();
		modes_release ();
	} else {
		modes_refresh_platform ();
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
		if (action && (strcmp (action, "add") == 0)) {
			/* Новое видео: сбрасываем кэш пакета, чтобы первое решение
			 * по нему шло по плееру этого видео, а не по пакету
			 * предыдущего приложения (кэш живёт 2 с). */
			pkg_cache_invalidate ();
			delay_framerate_switch (false, 0, modalias);
		} else if (action && (strcmp (action, "remove") == 0))
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

/* Запрос перезагрузки конфига из API (команда reconf), см. afrd_reconf и
 * atv14_reconf_pending. На legacy-пути достаточно mtime=-1, ATV14-цикл
 * обслуживает явный флаг. */
static bool g_reconf_requested;

/* --------- * --------- * --------- * --------- * --------- * --------- */

/* ---- ATV14: поллинг декодера + измерение fps + backdoor SurfaceFlinger ---- */

/* Через сколько молчания счётчика кадров сообщить, что частота неизвестна
 * (декодер при этом числится активным). Логируем один раз на видео. */
#define COUNTER_STALL_MS	10000ULL

/* Таблица режимов, полученная от SurfaceFlinger (заполняется sf_modes_refresh). */
static sf_mode_t g_sf_modes [SF_MODE_MAX];
static int g_sf_modes_n = 0;

/* Фильтр частот для sfmodes_select: на ATV14-пути действует тот же
 * mode.blacklist.rates, что и в legacy-ветке (раньше ключ молча игнорировался). */
static bool atv14_rate_allowed (int hz, void *ctx)
{
	/* ctx — минимальная частота (24.8) для текущего плеера либо NULL */
	int floor = ctx ? *(const int *) ctx : 0;

	if (floor && (hz < floor))
		return false;

	return !rate_is_blacklisted (hz);
}

/* Пакет на переднем плане (определение ниже, нужно для mode.min.hz.list). */
static char g_cur_pkg [PKG_NAME_LEN];

/* true — этому пакету применяем механики борьбы с «зажимом» (switch.bump.pkg).
 * Пустой список — не применяем никому: механики стоят перезапуска декодера
 * и смены конфигурации экрана, а зажим измерен только у штатного YouTube.
 *
 * Плеер из switch.skip.pkg («AFR мешает») механики не получает: обещание
 * этого списка — «режим не переключаем и не удерживаем», и подъём режима ему
 * противоречит. Иначе на Android 10/11, где штатный YouTube по умолчанию стоит
 * в обоих списках, подъём по переднему плану всё равно трогал бы режим. */
static bool bump_pkg_allows_pkg (const char *pkg)
{
	if (g_bump_pkgs.size == 0)
		return false;

	if (!pkg || !pkg [0])
		return false;

	if ((g_skip_pkgs.size > 0) && strlist_contains (&g_skip_pkgs, pkg))
		return false;

	return strlist_contains (&g_bump_pkgs, pkg);
}

/* То же с определением пакета на переднем плане. Пакет опрашивается каждый
 * раз, без кэша: подъём случается раньше первого замера (где пакет иначе
 * определяется), а кэш от прошлого приложения отправил бы механики не тому
 * плееру. Опрос стоит 60-70 мс, а подъёмов на ролик один-два.
 *
 * pkg_out (может быть NULL) получает определённый пакет — для журнала: без
 * него отказ не отличить от «передний план не определился». */
static bool bump_pkg_allows (const platform_t *plat, char *pkg_out, size_t sz)
{
	if (pkg_out && sz)
		pkg_out [0] = 0;

	if (g_bump_pkgs.size == 0)
		return false;

	char pkg [PKG_NAME_LEN] = { 0 };
	int rc = pkg_probe (plat, pkg, sizeof (pkg));

	if (pkg_out && sz)
	{
		snprintf (pkg_out, sz, "%s", (rc == 0) ? pkg
		          : (rc == -1) ? "(передний план не прочитан)"
		                       : "(передний план не разобран)");
	}

	return (rc == 0) && bump_pkg_allows_pkg (pkg);
}

/* Сброс «потолка» частоты плеера (switch.ceiling.reset); определение ниже.
 * Нужен в atv14_bump: на вендорном пути режим меняется дольше, чем плеер
 * успевает зафиксировать частоту. */
static int ceiling_reset_now (void);

/* ATV14-путь: подъём режима до максимума (switch.bump); определение ниже. */
static int atv14_bump (const platform_t *plat, int *orig_sf_id, int *cur_sf_id,
                       int *want_sf_id, bool *held);
/* Зависимости atv14_bump — определения ниже в этом файле. */
static int atv_current_target (const platform_t *plat, int *w, int *h);
static int atv14_apply_mode (const platform_t *plat, int sf_id, const char *skipped);
static int atv14_mode_hz (const platform_t *plat, int id);

/* Максимальная частота, которую ТВ поддерживает для этой геометрии, — цель
 * подъёма при старте видео (switch.bump). Частоты из mode.blacklist.rates не
 * рассматриваются: поднимать режим в заведомо запрещённый нельзя.
 *
 * Возврат: id режима (ATV14) или индекс в списке ТВ (Android 10/11), -1 — нет. */
static int bump_target_mode (const platform_t *plat, int w, int h)
{
	int best = -1, best_hz = 0;

	if (plat->sink == SINK_VENDOR_MODE)
	{
		for (int i = 0; i < g_modes_n; i++)
		{
			display_mode_t *m = &g_modes [i];
			if ((m->width != w) || (m->height != h))
				continue;

			/* Чересстрочные режимы не поднимаем: 1080i60 и 1080p60 имеют
			 * одну частоту, и без этой проверки «максимум» выбирал 1080i
			 * (живая находка 2026-10-18 на ATV11: ролик 60 к/с уходил в
			 * 1080i60hz). Чересстрочность для видео непригодна. */
			if (m->interlaced)
				continue;

			int hz = display_mode_hz (m);
			if (rate_is_blacklisted (hz))
				continue;

			if (hz > best_hz)
			{
				best_hz = hz;
				best = i;
			}
		}

		return best;
	}

	for (int i = 0; i < g_sf_modes_n; i++)
	{
		const sf_mode_t *m = &g_sf_modes [i];
		if ((m->width != w) || (m->height != h))
			continue;
		if (rate_is_blacklisted (m->hz))
			continue;

		if (m->hz > best_hz)
		{
			best_hz = m->hz;
			best = m->id;
		}
	}

	return best;
}

/* Подъём режима до максимума, поддержанного ТВ (switch.bump).
 *
 * Зачем. На Android 11+ плеер фиксирует «потолок» частоты в начале сессии
 * проигрывания: если в этот момент дисплей медленнее контента, темп не
 * поднимется уже никогда (журнал §47.6). Поэтому при каждом начале видео —
 * и когда декодер только появился, и когда он перезапустился под следующим
 * роликом (счётчик сброшен) — режим поднимается до максимума; обычный замер
 * затем опускает его до нужной частоты (вниз плеер следует сам).
 *
 * Возврат: 0 — режим поднят, -1 — поднимать нечего/не удалось. */
static int atv14_bump (const platform_t *plat, int *orig_sf_id, int *cur_sf_id,
                       int *want_sf_id, bool *held)
{
	if (!g_switch_bump || (plat->sink == SINK_LEGACY))
		return -1;

	/* Механики зажима — только плеерам из switch.bump.pkg (журнал §55.9):
	 * зажим измерен у штатного YouTube, а у плееров, которые на смену
	 * конфигурации экрана перезапускают Activity, сброс потолка ломает
	 * воспроизведение (OTT Navigator возвращался к предыдущему каналу). */
	char bump_pkg [PKG_NAME_LEN] = { 0 };
	if (!bump_pkg_allows (plat, bump_pkg, sizeof (bump_pkg)))
	{
		if (g_bump_pkgs.size == 0)
			trace (1, "bump: switch.bump.pkg пуст — режим не поднимаю\n");
		else if ((g_skip_pkgs.size > 0) && strlist_contains (&g_skip_pkgs, bump_pkg))
			trace (1, "bump: %s в switch.skip.pkg (AFR мешает) — режим не поднимаю\n",
			       bump_pkg);
		else
			trace (1, "bump: %s не в switch.bump.pkg — режим не поднимаю\n",
			       bump_pkg);
		return -1;
	}

	int bw = 0, bh = 0;
	int cur = atv_current_target (plat, &bw, &bh);
	int bump = bump_target_mode (plat, bw, bh);

	if ((cur < 0) || (bump < 0) || (bump == cur))
		return -1;		/* уже на максимуме — молча */

	bool took_holding = false;
	if (!*held && (*orig_sf_id < 0))
	{
		*orig_sf_id = cur;
		*held = true;
		took_holding = true;
	}

	if (atv14_apply_mode (plat, bump, "bump до максимума") != 0)
	{
		/* Не поднялось — ведём себя как раньше: удержание, взятое только
		 * ради bump, снимаем. */
		if (took_holding)
		{
			*held = false;
			*orig_sf_id = -1;
		}
		trace (1, "bump: режим не применился — работаю по замеру\n");
		return -1;
	}

	/* Цели нет: режим уже применён, а замер частоты запускается только при
	 * want_sf_id < 0 (иначе он пропускался и ролик оставался в максимуме). */
	*want_sf_id = -1;
	*cur_sf_id = bump;

	/* Вендорный путь (Android 10/11): сброс потолка здесь НЕ делается.
	 * Живой замер 18.10: смена плотности перезапускает декодер, демон видит
	 * «новое видео» и заново применяет быстрое решение по заявленной частоте —
	 * режим начинает мигать 50 ↔ 60 каждые ~20 с (проверено на .42 + Samsung:
	 * без сброса режим стоит ровно, со сбросом — петля). Штатный путь там и так
	 * возвращает режим на максимум по окончании видео, поэтому зажим возможен
	 * только для первого ролика после запуска демона — это дешевле петли.
	 * Сброс по замеру (ceiling_maybe_reset) при явно включённом
	 * switch.ceiling.reset остаётся: он одноразовый и петлю не заводит. */
	/* Вендорный путь (Android 10/11): плеер фиксирует «потолок» раньше, чем
	 * режим успевает смениться (смена 0.34-1.09 с с постоянным помощником), и
	 * ролик играет зажатым до конца замера (14-20 с). Поэтому сразу после
	 * успешной смены сбрасываем потолок; быстрый путь по заявленной частоте
	 * на этом видео выключается (см. g_ceiling_no_fast), иначе перезапуск
	 * декодера заводил петлю 50 ↔ 60.
	 * На backdoor-пути смена ~0.45 с и bump обычно успевает — там не нужно. */
	if ((plat->sink == SINK_VENDOR_MODE) && g_ceiling_reset && (g_ceiling_resets == 0))
	{
		g_ceiling_us = now_us ();
		g_ceiling_resets++;
		if (ceiling_reset_now () == 0)
		{
			g_ceiling_hz = atv14_mode_hz (plat, bump);
			g_ceiling_mode_id = bump;
			g_ceiling_no_fast = true;
			g_ceiling_restart_expected = true;
		}
	}

	/* Возврат после видео — на максимум, а не на прежний режим: иначе
	 * следующий ролик начнётся с низкого «потолка», и зажим вернётся. */
	*orig_sf_id = bump;
	trace (0, "bump: режим поднят до максимума (" HZ_FMT " Гц → " HZ_FMT
	       " Гц) до первого кадра плеера\n",
	       HZ_ARGS (atv14_mode_hz (plat, cur)), HZ_ARGS (atv14_mode_hz (plat, bump)));
	return 0;
}

/* Самый быстрый режим с нужной геометрией и частотой не ниже floor; -1 — нет.
 * Нужен для плееров с mode.min.hz.list: если ровного кратного для частоты
 * контента нет, берём самый быстрый режим (24 fps -> 60 Гц, а не 50: в 50 Гц
 * получаются редкие, но заметные рывки по 20 мс, в 60 Гц — распределённое 3:2
 * с отклонением 8 мс; проверено владельцем на глаз). */
static int atv14_fastest_mode (int w, int h, int floor)
{
	int best = -1, best_hz = 0;

	for (int i = 0; i < g_sf_modes_n; i++)
	{
		const sf_mode_t *m = &g_sf_modes [i];
		if ((m->width != w) || (m->height != h))
			continue;
		if (floor && (m->hz < floor))
			continue;
		if (m->hz > best_hz)
		{
			best_hz = m->hz;
			best = m->id;
		}
	}

	return best;
}

/* Ровное кратное (x2 или x3) частоты контента не ниже floor, ошибка до 1 %
 * (то же правило «кратности ≤ 3», что и в sfmodes_select, но без его допуска
 * 4.3 %); -1 — нет. */
static int atv14_multiple_mode (int hz, int w, int h, int floor, int *mult)
{
	int best = -1, best_hz = 0, best_k = 0;

	if (mult)
		*mult = 0;

	for (int k = 2; k <= 3; k++)
	{
		int want = hz * k;

		for (int i = 0; i < g_sf_modes_n; i++)
		{
			const sf_mode_t *m = &g_sf_modes [i];
			if ((m->width != w) || (m->height != h))
				continue;
			if (floor && (m->hz < floor))
				continue;

			int diff = m->hz - want;
			if (diff < 0)
				diff = -diff;
			if (diff * 100 > want)
				continue;		/* ошибка больше 1 % */

			if ((best < 0) || (m->hz < best_hz))
			{
				best = m->id;
				best_hz = m->hz;
				best_k = k;
			}
		}

		if (best >= 0)
			break;				/* меньшая кратность лучше */
	}

	if (mult)
		*mult = best_k;

	return best;
}

/* Выбор режима под частоту контента: кратные частоты (mode.prefer.exact) и
 * чёрный список частот (mode.blacklist.rates) — правила legacy-ветки.
 * floor (mode.min.hz.list, 24.8) — минимальная частота режима для текущего
 * плеера: ниже неё режимы не выбираются (см. README, ограничение 8). */
static int atv14_select_mode (int hz, int w, int h, int floor)
{
	sf_choice_t ch;

	/* Частота контента ниже порога плеера: 24/23.976 и подобные низкие режимы
	 * не подходят вообще, поэтому берём ровное кратное (25 -> 50), а если
	 * ровного кратного нет — самый быстрый режим (24 -> 60). */
	if (floor && (hz < floor))
	{
		int k = 0;
		int id = atv14_multiple_mode (hz, w, h, floor, &k);
		if (id < 0)
			id = atv14_fastest_mode (w, h, floor);

		if (id < 0)
			return -1;

		trace (1, "частота контента " HZ_FMT " Гц ниже минимума " HZ_FMT " Гц (%s) — беру " HZ_FMT " Гц%s\n",
		       HZ_ARGS (hz), HZ_ARGS (floor), g_cur_pkg,
		       HZ_ARGS (sfmodes_current_hz (g_sf_modes, g_sf_modes_n, id)),
		       k ? " как кратное" : " как самый быстрый");

		return id;
	}

	int fl = floor;
	if (sfmodes_select (g_sf_modes, g_sf_modes_n, hz, w, h,
	                    g_mode_prefer_exact, atv14_rate_allowed,
	                    fl ? &fl : NULL, &ch) != 0)
		return -1;

	if (ch.rate_n > 1)
		trace (1, "режим " HZ_FMT " Гц выбран как кратный (x%d) для " HZ_FMT " Гц\n",
		       HZ_ARGS (ch.hz), ch.rate_n, HZ_ARGS (hz));

	return ch.id;
}

static int sf_active_mode (const platform_t *plat, sf_mode_t *out);

/* Текущий режим для решений цикла, в тех же единицах, что mode_resolve:
 * ATV14 — id из таблицы SurfaceFlinger; ATV11 — частота (24.8) режима из узла
 * /sys/class/display/mode (таблицы SF на 10/11 нет, а guard_decide сравнивает
 * только равенство). Заодно отдаёт геометрию текущего режима.
 * Возврат: текущее значение или -1, если прочитать не удалось. */
static int atv_current_target (const platform_t *plat, int *w, int *h)
{
	if (plat->sink == SINK_VENDOR_MODE)
	{
		display_mode_get_current ();
		if (w) *w = g_current_mode.width  ? g_current_mode.width  : 1920;
		if (h) *h = g_current_mode.height ? g_current_mode.height : 1080;

		char *cm = sysfs_read (g_mode_path);
		if (!cm)
			return -1;

		char nm [64];
		size_t i = 0;
		while (((i + 1) < sizeof (nm)) && cm [i] && cm [i] != '\n' && cm [i] != '\r')
		{
			nm [i] = cm [i];
			i++;
		}
		nm [i] = 0;
		free (cm);

		/* Для ATV11 ключом режима служит ИНДЕКС в списке disp_cap, а не
		 * частота: имя режима не различает 23.976 и 24.000, и частота дала бы
		 * ложное расхождение с целью выбора (живая находка: демон применял
		 * режим по кругу — хотел 6138, видел 6144). */
		for (int j = 0; j < g_modes_n; j++)
			if (strcmp (g_modes [j].name, nm) == 0)
				return j;

		return -1;
	}

	sf_mode_t active;
	if (sf_active_mode (plat, &active) != 0)
		return -1;

	if (w) *w = active.width;
	if (h) *h = active.height;
	return active.id;
}

/* Целевой режим для решения о переключении.
 *
 * ATV14 возвращает id из таблицы SurfaceFlinger (он уходит в транзакцию 1035).
 * ATV11 (Android 10/11) не имеет такой таблицы: режим выбирается по ИМЕНИ из
 * disp_cap, а идентификатором для проверки «режим уже нужный» служит его
 * ЧАСТОТА (24.8) — guard_decide сравнивает только равенство, ему безразлично,
 * что это за число. Имя по частоте резолвит atv14_apply_mode перед вызовом
 * вендорного помощника.
 *
 * Возврат: id/частота цели, -1 — подходящего режима нет. */
static int mode_resolve (const platform_t *plat, int hz, int w, int h, int floor)
{
	if (plat->sink != SINK_VENDOR_MODE)
		return atv14_select_mode (hz, w, h, floor);

	if (g_modes_n <= 0)
	{
		/* Пустой список означает, что выбирать не из чего: об этом уже сказано
		 * при старте (modes_refresh_platform, спека §5). Возврат без решения —
		 * замер ниже отработает как обычно. */
		(void) plat;
		return -1;
	}

	display_mode_t best;
	char nm [32];
	int mhz = 0;
	if (modes_select_by_hz (g_modes, g_modes_n, hz, w, h,
	                        g_current_mode.interlaced, g_mode_prefer_exact,
	                        floor, rate_allowed_cb, NULL,
	                        &best, nm, sizeof (nm), &mhz) != 0)
		return -1;

	/* Идентификатор для решений цикла — индекс в списке (см. atv_current_target) */
	int idx = -1;
	for (int j = 0; j < g_modes_n; j++)
		if (strcmp (g_modes [j].name, best.name) == 0)
		{
			idx = j;
			break;
		}

	trace (1, "режим " HZ_FMT " Гц выбран как %s\n", HZ_ARGS (mhz), nm);
	return idx;
}

/* Пакет плеера текущего видео. Пустая строка — не удалось определить.
 * (Объявление — выше, в выборе режима: mode.min.hz.list.) */
static char g_cur_pkg [PKG_NAME_LEN];

/* Приложение на переднем плане (общее для обоих путей работы демона).
 * Возврат: 0 — разобрано (*out заполнен), -1 — команда не удалась,
 * -2 — вывод не разобран. */
static int pkg_probe (const platform_t *plat, char *out, size_t out_sz)
{
	if (out && out_sz)
		out [0] = 0;

	if (!plat->activity_dump_cmd)
		return -1;

	static char dump [4096];
	if (exec_capture (plat->activity_dump_cmd, dump, sizeof (dump)) != 0)
		return -1;

	return pkgdet_parse (dump, out, out_sz) ? 0 : -2;
}

/* Пакет для fps.correct.list / mode.min.hz.list / switch.skip.pkg:
 * разобрать и, если не вышло, сказать об этом в журнал. */
static void pkg_probe_logged (const platform_t *plat)
{
	int rc = pkg_probe (plat, g_cur_pkg, sizeof (g_cur_pkg));
	if (rc == -1)
		trace (1, "не удалось определить приложение на переднем плане\n");
	else if (rc == -2)
		trace (1, "приложение на переднем плане не разобрано\n");
}

/* Кэш пакета для старого пути: `dumpsys` — это fork/exec, поэтому не чаще
 * раза в PKG_CACHE_MS. Пока ни одного из списков по пакетам нет, не делается
 * ни одного вызова (у существующих пользователей Android 6-9 поведение
 * остаётся прежним). */
#define PKG_CACHE_MS	2000

/* Таймер кэша пакета: взведён, пока кэш свежий (см. pkg_name); сбрасывается
 * на старте нового видео через pkg_cache_invalidate(). */
static mstime_t g_pkg_cache_stamp;

static const char *pkg_name (void)
{
	if (g_plat.kind == PLAT_ATV14)
		return g_cur_pkg;

	static char pkg_buf [PKG_NAME_LEN];

	if ((g_skip_pkgs.size == 0) && (g_mode_min_hz_count == 0))
		return "";

	if (!mstime_running (&g_pkg_cache_stamp))
	{
		mstime_arm (&g_pkg_cache_stamp, PKG_CACHE_MS);
		if (pkg_probe (&g_plat, pkg_buf, sizeof (pkg_buf)) != 0)
			pkg_buf [0] = 0;
	}

	return pkg_buf;
}

/* Сброс кэша пакета — зовётся на старте нового видео (ветка ACTION=add
 * vdec-uevent). Кэш живёт PKG_CACHE_MS, и без сброса первое решение по новому
 * видео ушло бы по пакету предыдущего приложения: например, вышли из плеера из
 * switch.skip.pkg и тут же запустили другое видео — гейт сработал бы по
 * устаревшему пакету и переключение для нового видео пропало бы. Ограничение
 * скорости запросов (не чаще PKG_CACHE_MS) на время воспроизведения сохраняется. */
static void pkg_cache_invalidate (void)
{
	mstime_disable (&g_pkg_cache_stamp);
}

/* true — текущему видео AFR мешать не надо (плеер в switch.skip.pkg). */
static bool pkg_skip_active (void)
{
	if (g_skip_pkgs.size == 0)
		return false;

	const char *pkg = pkg_name ();
	return (pkg [0] != 0) && strlist_contains (&g_skip_pkgs, pkg);
}

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
	/* На вендорном пути Android 10/11 таблицы режимов SurfaceFlinger нет
	 * (транзакция 1035 значит другое, а дамп без activeMode) — режимы там
	 * переключаются вендорным SystemControl, и сообщение «переключать режимы
	 * нечем» было бы ложью. Плюс каждый опрос — это dumpsys SurfaceFlinger
	 * (~150 КБ) на каждой итерации цикла, а результат не нужен вовсе.
	 * Проверка живёт ЗДЕСЬ, а не только на старте: помимо стартового блока
	 * функция зовётся из цикла решения, и первая правка не покрывала этот
	 * вызов (живая находка прогона приёмки 2026-10-08: строка всплыла в
	 * момент решения о режиме). */
	if (plat->sink == SINK_VENDOR_MODE)
	{
		g_sf_modes_n = 0;
		return;
	}

	char dump [64 * 1024];
	if (exec_capture (plat->sf_dump_cmd, dump, sizeof (dump)) != 0)
	{
		trace (1, "не удалось получить список режимов SurfaceFlinger\n");
		g_sf_modes_n = 0;
		return;
	}

	g_sf_modes_n = sfmodes_parse (dump, g_sf_modes, SF_MODE_MAX);
	if (g_sf_modes_n <= 0)
	{
		/* Различаем «дамп пуст/усечён» и «дамп в формате другой версии
		 * Android»: на 10/11 нет ни activeMode, ни строк {id=…}, и прежнее
		 * сообщение про «0 байт» путало (живой случай 2026-10-08). Уровень 0
		 * — иначе AFR молча не переключал бы. */
		if (strstr (dump, "refresh-rate") && !strstr (dump, "{id="))
			trace (0, "режимов SurfaceFlinger нет: дамп в формате Android 10/11"
			       " (нет activeMode) — переключать режимы нечем\n");
		else
			trace (0, "таблица режимов SurfaceFlinger не разобрана"
			       " (пусто/усечён дамп: %d байт?)\n", (int) strlen (dump));
	}
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
	/* Таблицу режимов SurfaceFlinger читаем только на ATV14: на вендорном пути
	 * её нет, а сообщение о ней («переключать режимы нечем») было бы ложью —
	 * там режимы переключаются (нашло финальное ревью). */
	if ((plat->sink != SINK_VENDOR_MODE) && (g_sf_modes_n <= 0))
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
	/* вызывается раз за итерацию во всех ветках run_atv14 — удобная точка
	 * для «сердцебиения» (признак жизни демона для сторожа приложения) */
	stats_heartbeat ();

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

	if (plat->sink == SINK_VENDOR_MODE)
	{
		/* Android 10/11: режим задаётся ИМЕНЕМ вендорным SystemControl, а
		 * «id» здесь — индекс режима в списке disp_cap (см. mode_resolve):
		 * таблицы SurfaceFlinger на этих версиях нет. */
		if ((sf_id < 0) || (sf_id >= g_modes_n))
		{
			trace (1, "режима с индексом %d нет в списке режимов ТВ — %s\n",
			       sf_id, skipped);
			return -1;
		}

		const char *name = g_modes [sf_id].name;
		if (sink_apply (plat->sink, g_mode_path, -1, name) != 0)
		{
			trace (1, "вендорный SystemControl не применил режим %s\n", name);
			return -2;
		}
	}
	else
	{
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
	}

	/* Цветовое пространство по правилам cs.select — как делал legacy-путь при
	 * каждой смене режима (modes.c: switch_mode). Правила сопоставляются с
	 * именем режима, поэтому читаем его из узла (на ATV14 это отражение
	 * состояния, которое выставил фреймворк). */
	char *cm = sysfs_read (g_mode_path);
	if (cm)
	{
		char nm [64];
		size_t i = 0;
		while (((i + 1) < sizeof (nm)) && cm [i] && cm [i] != '\n' && cm [i] != '\r')
		{
			nm [i] = cm [i];
			i++;
		}
		nm [i] = 0;
		free (cm);

		if (nm [0])
			colorspace_apply (nm);
	}

	return 0;
}

/* Проверка подачи видео после смены режима (журнал §21.9).
 *
 * Если режим переключить при живом видео, в ~40 % случаев подача слоя
 * становится вдвое реже периода дисплея (интервалы ~2 периода, видео «как
 * 13 fps») — HWC/SF не перенастроили vsync-путь, а переключение режима его не
 * пересчитывает. Повторная выдача того же режима не помогает; помогает смена
 * режима «туда-обратно» (проверено на стенде).
 *
 * Сравнивать с периодом дисплея нельзя: у 24-фпс видео на 60 Гц подача и должна
 * быть ~2.5 периода. Поэтому порог — максимум из периода дисплея и ожидаемого
 * интервала контента (expect_ms, из частоты выбранного режима).
 *
 * Возврат true — подача вдвое реже ожидаемого; period_ms и avg_ms — для лога.
 * Разбор делает awk на устройстве (`dumpsys SurfaceFlinger --latency`):
 * первая строка — период дисплея в нс, дальше интервалы фактической подачи. */
static bool atv14_layer_stats (double *period_ms, double *avg_ms, int *count)
{
	*period_ms = *avg_ms = 0.0;
	if (count)
		*count = 0;

	char out [4096];
	const char *cmd =
		"L=$(dumpsys SurfaceFlinger --list 2>/dev/null | grep 'SurfaceView\\[' | grep -i blast | head -1); "
		"[ -z \"$L\" ] && exit 0; "
		"dumpsys SurfaceFlinger --latency \"$L\" 2>/dev/null | awk '"
		"NR==1 { p=$1/1e6; next } "
		"NF==3 && $2+0>0 && $2+0<1e17 { n++; t[n]=$2+0 } "
		"END { if (n<3) exit; m=0; s=0; "
		"for (i=2;i<=n;i++) { d=(t[i]-t[i-1])/1e6; if (d>0) { m++; s+=d } } "
		"if (m>0) printf \"%.2f %.2f %d\", p, s/m, m }'";

	if (exec_capture (cmd, out, sizeof (out)) != 0)
		return false;

	double p = 0, a = 0;
	int n = 0;
	if ((sscanf (out, "%lf %lf %d", &p, &a, &n) != 3) || (a <= 0) || (p <= 0))
		return false;

	*period_ms = p;
	*avg_ms = a;
	if (count)
		*count = n;

	return true;
}

static bool atv14_cadence_halved (int expect_ms, int *period_ms, int *avg_ms)
{
	double p = 0, a = 0;
	if (!atv14_layer_stats (&p, &a, NULL))
		return false;

	*period_ms = (int) (p + 0.5);
	*avg_ms = (int) (a + 0.5);

	double base = ((double) expect_ms > p) ? (double) expect_ms : p;
	return (a > 1.5 * base);
}

/* Запасной источник частоты: средний интервал подачи видео-слоя. Он не зависит
 * от счётчика кадров декодера (тот на ATV14 бывает завышен, а иногда и вовсе
 * замирает), поэтому по нему можно судить о частоте контента. Если слой
 * обновляется на частоте дисплея (кадры идут «как есть»), о контенте это ничего
 * не говорит — тогда 0. Возврат: fixed-point 24.8 Гц или 0. */
static int atv14_layer_fps_hz (void)
{
	double p = 0, a = 0;
	int n = 0;
	if (!atv14_layer_stats (&p, &a, &n))
		return 0;

	if ((n < 60) || (a <= 0.0))
		return 0;			/* слишком мало данных */

	if (a <= p * 1.05)
		return 0;			/* слой обновляется на частоте дисплея */

	unsigned long long frames = (unsigned long long) (n - 1);
	unsigned long long dt_us = (unsigned long long) (a * 1000.0 * (double) frames + 0.5);
	int hz = 0;
	if (!fps_classify_window_corrected (frames, dt_us, 2000, 1.0, 0.0, &hz))
		return 0;			/* интервал не уложился в класс */

	return hz;
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

static int vdec_poll (const char *path, vdec_status_t *st, vdec_sel_t *sel)
{
	int rc = vdec_read (path, st, sel);
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

/* Частота режима по его идентификатору для статистики.
 *
 * Идентификатор различается по пути: ATV14 — id из таблицы SurfaceFlinger,
 * ATV11 — ИНДЕКС режима в списке disp_cap (см. mode_resolve). На Android 10/11
 * таблицы SF нет вовсе (g_sf_modes_n = 0), поэтому прежний поиск по ней отдавал
 * 0 Гц и switched=false — тот же класс дефекта, что чинили для пассивного режима
 * (§33.17); нашло финальное ревью. */
static int atv14_mode_hz (const platform_t *plat, int id)
{
	if (plat->sink == SINK_VENDOR_MODE)
		return ((id >= 0) && (id < g_modes_n)) ? display_mode_hz (&g_modes [id]) : 0;

	return sfmodes_current_hz (g_sf_modes, g_sf_modes_n, id);
}

/* Сброс «потолка» частоты у плеера (switch.ceiling.reset, журнал §47.9).
 *
 * Плеер на Android 11+ фиксирует частоту дисплея в начале сессии проигрывания
 * и планирует кадры по min (потолок, текущий режим). Внешняя смена режима
 * (backdoor 1035, SystemControl) потолок не поднимает: если ролик начался под
 * 50 Гц, то после подъёма до 60 он продолжает отдавать 50 к/с. Потолок
 * перечитывается при смене КОНФИГУРАЦИИ экрана; самое дешёвое такое событие —
 * изменение плотности на единицу с немедленным возвратом.
 *
 * Замер (ATV14, .50, клип 60.000 к/с): подача 50.34 -> 60.00 к/с, провал
 * подачи 83 мс (три прогона). На ATV11 (.42) та же операция стоит 1.5-2.5 с,
 * поэтому умолчание зависит от приёмника: на backdoor-пути включено, на
 * вендорном — выключено.
 *
 * Возврат: 0 — потолок сброшен, -1 — нет (журнал уже написан). */
/* Вернуть прежнюю плотность. Повтор обязателен: exec по таймауту убивает всю
 * группу процессов, и второй шаг мог не выполниться (нашло ревью). Возврат:
 * 0 — плотность восстановлена, -1 — нет (журнал уже написан). */
static int ceiling_restore (int had_override, int prev)
{
	char cmd [64];
	if (ceiling_build_restore (cmd, sizeof (cmd), had_override, prev) != 0)
		return -1;

	char resp [128];
	int rc = exec_status (cmd, resp, sizeof (resp));
	if (rc != 0)
	{
		trace (0, "сброс потолка: плотность не вернулась (код %d) — повторяю\n", rc);
		rc = exec_status (cmd, resp, sizeof (resp));
	}

	if (rc != 0)
	{
		trace (0, "сброс потолка: плотность осталась изменённой (код %d); "
		       "вернуть можно командой `%s`\n", rc, cmd);
		return -1;
	}

	return 0;
}

static int ceiling_reset_now (void)
{
	char resp [256];

	int rc = exec_status ("wm density", resp, sizeof (resp));
	if (rc != 0)
	{
		trace (1, "сброс потолка: `wm density` не выполнилась (код %d)\n", rc);
		return -1;
	}

	int cur = 0, had = 0, next = 0;
	if (ceiling_density_parse (resp, &cur, &had, &next) != 0)
	{
		trace (1, "сброс потолка: не разобрать вывод `wm density`\n");
		return -1;
	}

	/* Два шага отдельными командами: код возврата составной команды — это код
	 * ПОСЛЕДНЕЙ (возврата плотности), и отказ первого шага прошёл бы как
	 * успех: демон счёл бы потолок подтверждённым, а плеер режим не перечитал
	 * (нашло ревью). */
	char cmd [64];
	snprintf (cmd, sizeof (cmd), "wm density %d", next);
	rc = exec_status (cmd, resp, sizeof (resp));
	if (rc != 0)
	{
		trace (1, "сброс потолка: плотность %d не применилась (код %d)\n", next, rc);
		ceiling_restore (had, cur);	/* первый шаг мог примениться частично */
		return -1;
	}

	/* Возврат плотности обязателен и после успешного первого шага: иначе на
	 * экране останется изменённая плотность. */
	if (ceiling_restore (had, cur) != 0)
		return -1;

	trace (0, "потолок частоты сброшен сменой плотности %d->%d: плеер перечитает режим\n",
	       cur, next);
	return 0;
}

/* Пределы на сбросы, чтобы смена ролика/рекламы не превратилась в серию
 * перезапусков декодера: не чаще раза в 1.5 с и не больше четырёх на видео. */
#define CEILING_RESETS_MAX	4
#define CEILING_MIN_GAP_US	1500000ULL

/* Сбросить потолок, если цель выше уже подтверждённого. target_id — режим,
 * который демон считает нужным; playing — видео идёт. */
static void ceiling_maybe_reset (const platform_t *plat, int target_id, bool playing)
{
	if (!g_ceiling_reset || (plat->sink == SINK_LEGACY))
		return;

	/* Сброс — смена конфигурации экрана: часть плееров перезапускает на ней
	 * Activity и теряет состояние (живой случай: OTT Navigator вернулся к
	 * предыдущему каналу). Поэтому только плеерам из switch.bump.pkg. */
	if (!bump_pkg_allows (plat, NULL, 0))
		return;

	if (!playing || (target_id < 0))
		return;
	if (g_ceiling_resets >= CEILING_RESETS_MAX)
		return;
	if (g_ceiling_us && ((now_us () - g_ceiling_us) < CEILING_MIN_GAP_US))
		return;

	int hz = atv14_mode_hz (plat, target_id);
	if (hz <= 0)
		return;

	/* Потолок уже не ниже цели: min(потолок, текущий режим) и так даёт
	 * нужную частоту, лишний перезапуск декодера не нужен. */
	if (!ceiling_should_reset (g_ceiling_hz, hz))
		return;

	/* Время попытки фиксируем и при отказе: иначе на устройстве, где `wm` не
	 * работает или вывод не разобрался, демон молотил бы fork/exec каждые
	 * trigger.poll.ms до конца ролика (нашло ревью). */
	g_ceiling_us = now_us ();
	g_ceiling_resets++;

	if (ceiling_reset_now () == 0)
	{
		g_ceiling_hz = hz;
		g_ceiling_mode_id = target_id;
		g_ceiling_no_fast = true;
		/* Перезапуск декодера от смены плотности — не новый ролик; признак
		 * одноразовый, чтобы настоящий новый ролик в том же окне его не унаследовал
		 * (нашло ревью). */
		g_ceiling_restart_expected = true;
	}
}

/* Новое видео: подтверждённый потолок прежнего ролика к нему не относится. */
static void ceiling_forget (void)
{
	g_ceiling_resets = 0;
	g_ceiling_hz = 0;
}

/* Сброс потолка только что произошёл: перезапуск декодера, который он вызвал,
 * не должен считаться новым видео (иначе демон поднимет режим до максимума и
 * получится лишний переход). */
/* Окно «это перезапуск от нашего сброса». Декодер после смены плотности
 * перезапускается не сразу: сброс -> перезапуск -> сброс счётчика занимает
 * 6-9 с (живые замеры 18.10 на .50: сброс -> видимый перезапуск через 7.7-8.0 с,
 * на .41 — через 2.5-3.5 с). Поэтому окно 12 с, а не 3: короткое окно давало
 * «мигание» 50 -> 60 сразу после переключения. Цена: настоящий новый ролик,
 * начатый в эти 12 с, не поднимется — но режим и так был выставлен по контенту,
 * а следующий замер его поправит. */
#define CEILING_RESTART_WINDOW_US	12000000ULL

static bool ceiling_recent_within (unsigned long long us)
{
	return g_ceiling_us && ((now_us () - g_ceiling_us) < us);
}

/* Потреблять одноразовый признак «декодер перезапустился от нашего сброса».
 * Возврат true — этот перезапуск наш (и признак снят). */
static bool ceiling_take_restart (int cur_id)
{
	/* Одного времени мало: настоящий новый ролик может начаться в том же окне.
	 * Наш перезапуск приходит при ТОМ ЖЕ режиме, который был при сбросе; если
	 * демон успел переключить режим (замер нового ролика), это не наш
	 * перезапуск (нашло ревью). Окно шире, чем у bump: перезапуск рендерера
	 * после смены плотности наблюдался и через 2, и через 9 с — время зависит
	 * от плеера и буферизации (живой прогон 2026-10-18). */
	bool own = g_ceiling_restart_expected &&
	           (now_us () - g_ceiling_us) < CEILING_RESTART_WINDOW_US &&
	           (cur_id >= 0) && (cur_id == g_ceiling_mode_id);
	g_ceiling_restart_expected = false;
	return own;
}

/* Статистика для GUI/API (afrd -s, status): legacy-структуры в ATV14 не
 * ведутся, поэтому отдаём id текущего режима и id режима возврата. */
static void atv14_stats_sync (const platform_t *plat, int cur_id, int orig_id, bool held)
{
	g_atv14_state.valid = true;
	g_atv14_state.switched = held;
	g_atv14_state.current_hz = atv14_mode_hz (plat, cur_id);
	g_atv14_state.original_hz = atv14_mode_hz (plat, held ? orig_id : cur_id);
}

/* Пассивный режим (нет backdoor или enable=0): переключать нечем/нельзя, но
 * статистику API заполняем один раз при входе — иначе `status`/`afrd -s` и GUI
 * показывают «демон выключен» и нулевые частоты (README обещает: «в пассивном
 * режиме работают API и статистика»). Периодически её обновлять не стоим: каждый
 * опрос — это dumpsys SurfaceFlinger (~150 КБ), а режим в пассивном режиме меняет
 * не демон.
 *
 * Частоту берём прямо из дампа (sfmodes_active_hz), а не через id режима: на
 * Android 10/11 таблицы режимов в привычном виде нет (транзакция 1035 там значит
 * другое), и поиск по id заведомо не сработал бы — живой случай 2026-10-08 на
 * стенде Android 11: enabled=no и 0.00 Гц при enable=1 в конфиге. */
static void atv14_stats_sync_passive (const platform_t *plat)
{
	static char dump [64 * 1024];

	if (g_sf_modes_n <= 0)
		sf_modes_refresh (plat);

	int hz = -1;
	if (exec_capture (plat->sf_dump_cmd, dump, sizeof (dump)) == 0)
	{
		sf_modes_sync (dump);
		hz = sfmodes_active_hz (dump);
	}

	g_atv14_state.valid = true;
	g_atv14_state.switched = false;
	g_atv14_state.current_hz = (hz > 0) ? hz : 0;
	g_atv14_state.original_hz = g_atv14_state.current_hz;

	/* g_afrd_stats (то, что читает API `status`) копирует update_stats(),
	 * а он живёт в главном цикле — в пассивном режиме до него не дойдёт,
	 * поэтому копируем здесь же */
	update_stats ();
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

/* Пора ли выйти из run_atv14 на перезагрузку конфига: main.c при ret>0
 * заново вызовет afrd_init/afrd_run, то есть конфиг будет перечитан
 * (afrd_fini освобождает g_cfg, afrd_init его загружает).
 *
 * Два источника:
 *  - API reconf (g_reconf_requested) — срабатывает сразу, не ожидая периода;
 *  - правка файла конфига — проверяется не чаще CONFIG_CHECK_PERIOD по mtime.
 *
 * config_timer принадлежит вызывающему циклу: legacy-таймер g_ost_config здесь
 * не тикает (его обслуживает только старый цикл).
 *
 * Зачем явный флаг, если afrd_reconf и так ставит g_config_mtime=-1: на
 * legacy-пути этого достаточно (цикл сравнивает mtime), а ATV14-цикл узнал бы о
 * reconf только из периодической проверки mtime — до 5 с задержки. Флаг делает
 * reconf немедленным; mtime=-1 в afrd_reconf оставлен ради legacy-ветки. */
static bool atv14_reconf_pending (mstime_t *config_timer)
{
	if (g_reconf_requested)
	{
		g_reconf_requested = false;
		trace (1, "запрошена перезагрузка конфига (reconf)\n");
		return true;
	}

	if (mstime_expired (config_timer))
	{
		mstime_arm (config_timer, CONFIG_CHECK_PERIOD);
		if (config_mtime_changed (mtime (g_config), g_config_mtime))
		{
			trace (1, "config file %s changed, reloading\n", g_config);
			return true;
		}
	}

	return false;
}

/* Возврат режима, который прошлый экземпляр демона оставил удержанным: его
 * убили (kill -9, паника ядра), и вернуть режим больше некому — SurfaceFlinger
 * сам режим не сбрасывает, рабочий стол остаётся в низкой частоте, пока режим
 * не запросит кто-нибудь другой (живая проверка 2026-10-08). Флаг backdoor для
 * опознания не годится: система гасит его за секунды. */
static void atv14_recover_stale (const platform_t *plat)
{
	int hz = guard_recover_hz (g_prev_stats.switched, g_prev_stats.original_hz);
	if (!hz)
		return;

	/* Режим ищем по частоте, а не по номеру: нумерация id меняется при
	 * рестарте SurfaceFlinger, а частоты исходного режима (59.94 и т.п.)
	 * различают записи точно. */
	int id = sfmodes_find_hz (g_sf_modes, g_sf_modes_n, hz);
	if (id < 0)
	{
		trace (0, "прошлый экземпляр оставлен с удержанным режимом ("HZ_FMT" Гц), "
		       "но такого режима нет в таблице SurfaceFlinger — вернуть не могу\n",
		       HZ_ARGS (hz));
		return;
	}

	if (atv14_apply_mode (plat, id, "восстановление режима прошлого запуска") == 0)
		trace (0, "прошлый экземпляр был убит с удержанным режимом — вернул "HZ_FMT
		       " Гц (id %d)\n", HZ_ARGS (hz), id);
	else
		trace (0, "прошлый экземпляр был убит с удержанным режимом, "
		       "но вернуть "HZ_FMT" Гц не удалось\n", HZ_ARGS (hz));
}

/* Рабочий стол, наши экраны и системный UI: их появление на переднем плане не
 * повод поднимать режим. Иначе демон возвращал бы 60 Гц рабочему столу, который
 * пользователь сознательно поставил на 50 Гц (живая находка 18.10: подъём
 * срабатывал на com.google.android.tvlauncher). Пакет лаунчера определяется один
 * раз при старте цикла. */
static bool fg_is_desktop (const char *pkg)
{
	static char home [PKG_NAME_LEN] = { 0 };
	static bool home_probed = false;

	if (!home_probed)
	{
		home_probed = true;

		char out [256];
		if (exec_capture ("cmd package resolve-activity --brief -a android.intent.action.MAIN "
		                  "-c android.intent.category.HOME", out, sizeof (out)) == 0)
		{
			/* Убираем хвостовые переводы строк: последняя НЕПУСТАЯ строка
			 * и есть компонент, а наивный strrchr попал бы в пустую
			 * (живая находка 18.10 — лаунчер не распознавался). */
			size_t olen = strlen (out);
			while (olen > 0 && (out [olen - 1] == '\n' || out [olen - 1] == '\r' ||
			                    out [olen - 1] == ' '))
				out [--olen] = 0;

			char *p = strrchr (out, '\n');
			p = p ? p + 1 : out;
			char *slash = strchr (p, '/');
			size_t n = slash ? (size_t)(slash - p) : strlen (p);
			if ((n > 0) && (n < sizeof (home)))
			{
				memcpy (home, p, n);
				home [n] = 0;
				trace (1, "пакет рабочего стола: %s\n", home);
			}
		}
	}

	if (home [0] && (strcmp (pkg, home) == 0))
		return true;

	/* Наши собственные экраны и системный UI */
	if (strcmp (pkg, "ru.reyter.afrd2") == 0)
		return true;
	if (strcmp (pkg, "com.android.systemui") == 0)
		return true;

	return false;
}

static int run_atv14 (const platform_t *plat)
{
	guard_action_t act;
	int orig_sf_id = -1, cur_sf_id = -1, want_sf_id = -1;
	bool held = false;
	bool video = false;
	vdec_status_t v0;
	/* R61: состояние выбора канала vdec_status (растущий счётчик) */
	vdec_sel_t vdec_sel = { 0 };
	int retries = 0;
	/* частоту в этом видео определить не удалось — больше не мерить
	 * до его конца (сбрасывается, когда видео закончилось) */
	bool measure_failed = false;
	/* метка уже применённой подсказки плеера (R53): чтобы одну подсказку не
	 * применять на каждой итерации цикла, пока она жива (1 с) */
	mstime_t hint_seen = 0;
	/* R56: одноразовая перепроверка замера после смены режима */
	mstime_t recheck_timer = 0;
	bool recheck_armed = false;
	/* Залипший канал: состояние сторожа (пакет плеера, счётчик несовпадений,
	 * флаг «про отказ пробы уже сказали») — см. stuck.h */
	stuck_state_t stuck;
	stuck_reset (&stuck);
	/* признак «пакет в списке switch.skip.pkg» — считается на каждой итерации
	 * и логируется один раз на видео */
	bool skip_pkg = false;
	bool skip_logged = false;
	unsigned long long pkg_probe_us = 0;
	/* Сказали ли уже про «счётчик стоит» (status декодера) — один раз на
	 * эпизод; используется только для журнала (§31.18) */
	bool pause_logged = false;

	/* Заморозка счётчика кадров (AV1 и т.п.): сколько живём без роста и
	 * сообщили ли уже об этом (логируем один раз на видео) */
	unsigned long long counter_last_grow_us = 0;

	/* Вариант 3: последний опрос переднего плана и уже увиденный пакет */
	unsigned long long fg_probe_us = 0;
	char fg_seen [PKG_NAME_LEN] = { 0 };
	bool counter_stall_logged = false;

	/* §21.9: проверка, что после смены режима кадры реально идут на подачу */
	mstime_t cadence_timer = 0;
	bool cadence_armed = false;
	int cadence_fixes = 0;
	/* ожидаемый интервал подачи контента (мс) — из измеренной частоты */
	int cadence_expect_ms = 0;
	/* HDR/SDR-атрибуты: перечитать, когда система уже выставила свои значения
	 * после смены режима (не зависит от fps.cadence.fix.ms) */
	mstime_t hdrinfo_timer = 0;
	bool hdrinfo_armed = false;
	int preferred_id;
	/* поправка частоты для плеера текущего видео (fps.correct.list):
	 * пакет определяется один раз на видео, при первом замере */
	bool pkg_probed = false;
	double video_corr = 1.0;

	/* Наблюдение частоты: кольцо выборок счётчика (см. fps_env.h). Решение
	 * принимается по нижней огибающей, а не по одному окну. */
	fps_env_t env;
	/* счётчик рос на прошлой выборке — о простое сообщаем только на переходе
	 * «рос → стоит», а не на каждой итерации паузы */
	bool env_growing = false;
	fps_env_reset (&env);

	/* Быстрое решение по заявленной частоте (fps.declare.fast): применён ли
	 * уже режим по ней и для какого класса — чтобы подтвердить замером или
	 * поправить (см. блок ниже). */
	bool fast_applied = false;
	int fast_class = 0;

	/* Поведенческие ключи профиля (R28). */
	int prefer_api = cfg_get_int ("fps.prefer.api", 1);
	int pause_keep = cfg_get_int ("pause.keep", 1);
	int restore_on_stop = cfg_get_int ("switch.restore.on.stop", 1);

	/* Отслеживание «паузы»: декодер активен, а счётчик кадров не растёт. */
	unsigned long long prev_frames = 0;
	bool have_prev_frames = false;
	bool paused = false;
	int still = 0;
	/* Момент последнего роста счётчика кадров: от него отсчитывается
	 * «терпение к паузе» (switch.delay.off на новом пути, см. pause.h).
	 * Ноль в начале — счётчик ещё ни разу не рос, предела не применяем. */
	unsigned long long still_since_us = 0;
	/* Память о режиме, удержанном на время паузы (pause.c) */
	pause_state_t pause_mem;
	pause_forget (&pause_mem);

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

	/* Перезагрузка конфига (reconf / правка файла): период проверки mtime
	 * и признак «выходим на перезагрузку» (return 1). См. atv14_reconf_pending. */
	mstime_t config_timer = 0;
	bool reconf = false;
	mstime_update ();
	mstime_arm (&config_timer, 1);

	/* Атрибуты дисплея (HDR/SDR-режимы) — только чтение и показ: применяют их
	 * система и плеер (см. README). Читаем до развилки на пассивные режимы,
	 * чтобы значения были и на пути без backdoor SurfaceFlinger. */
	hdrinfo_refresh (plat->vendor_cli);
	hdrinfo_log ("атрибуты дисплея при старте");

	/* Backdoor недоступен — переключать режимы нечем. Пассивный режим
	 * (спека §3.2.6): ни одного sink_apply, только обслуживание API, чтобы
	 * работали frame_rate_hint и afrd -s. Перезагрузка конфига работает и
	 * здесь: enable=0 снимается правкой файла, без перезапуска процесса.
	 * Только для ATV14: на ATV11 (Android 10/11) переключаем вендорным
	 * SystemControl, там пассивный режим не нужен (см. platform_detect). */
	if ((plat->kind == PLAT_ATV14) && !plat->sf_backdoor)
	{
		trace (0, "backdoor SurfaceFlinger недоступен — пассивный режим\n");
		atv14_stats_sync_passive (plat);
		while (!g_shutdown)
		{
			/* без этого не тикает config_timer, а от него зависит
			 * проверка правки файла (флаг reconf работает и так) */
			mstime_update ();
			if (atv14_wait (cur_hz) != 0)
				msleep (cur_hz);
			if (atv14_reconf_pending (&config_timer))
				return 1;
		}
		return 0;
	}

	preferred_id = preferred_mode_id (plat);

	/* Таблицу режимов SurfaceFlinger читаем только на ATV14: на вендорном пути
	 * Android 10/11 её нет, и прежнее сообщение «переключать режимы нечем»
	 * было бы ложью — там режимы переключаются (нашло финальное ревью). */
	if (plat->sink != SINK_VENDOR_MODE)
	{
		sf_modes_refresh (plat);
		trace (1, "предпочтительный режим ТВ: %d\n", preferred_id);
	}

	/* Прошлый экземпляр могли убить с удержанным режимом (см.
	 * atv14_recover_stale): тогда возвращаем его до снятия флага. */
	if (g_prev_stats_valid)
		atv14_recover_stale (plat);

	/* страховка: снять «залипший» флаг прошлого запуска */
	sink_release_backdoor (plat->sink);

	/* enable=0 в конфиге — ключ задокументирован как рабочий, но новая ветка
	 * его раньше игнорировала: демон продолжал измерять и слать 1035, а API
	 * при этом отдавал `enabled: no`. Поведение как в пассивном режиме
	 * (залипший флаг уже снят строкой выше). */
	if (!g_enable)
	{
		trace (0, "enable=0 — переключение частоты выключено, только API\n");
		atv14_stats_sync_passive (plat);
		while (!g_shutdown)
		{
			mstime_update ();
			if (atv14_wait (cur_hz) != 0)
				msleep (cur_hz);
			/* enable=1 из новой версии файла подхватится на перезагрузке */
			if (atv14_reconf_pending (&config_timer))
				return 1;
		}
		return 0;
	}

	/* PID SurfaceFlinger на старте: с ним сравнивает проверка рестарта ниже.
	 * Если pidof не ответил, останется -1 и первая проверка просто запомнит
	 * значение (без ложного «перезапуска»). */
	pid_t last_sf_pid = pidof ("surfaceflinger");
	if (last_sf_pid <= 0)
		last_sf_pid = -1;

	int max_retries = cfg_get_int ("fps.measure.retries", 3);
	/* Уточнение замера заявленной длительностью кадра (vdec_status: frame dur),
	 * %: на IPTV счётчик кадров завышен на 2–4% (см. fps_refine_declared) */
	int declared_pct = cfg_get_int ("fps.declared.crosscheck.pct", 5);
	/* mode.use.fract: на новом пути ключ уважается (раньше игнорировался).
	 * По умолчанию для ATV14-профиля — дробные режимы: контент почти всегда
	 * идёт с 23.976/29.97/59.94, а 0 даёт точные 24.000/30.000/60.000. */
	int use_fract = cfg_get_int ("mode.use.fract", 1);
	/* R56: через сколько мс после переключения перемерить частоту и поправить
	 * режим, если класс однозначно другой (первое окно у некоторых плееров
	 * смещено набором буфера — 60-фпс канал давал 50.3 fps и режим 50 Гц).
	 * 0 — перепроверка выключена. Знание о плеерах не нужно. */
	int recheck_ms = cfg_get_int ("fps.recheck.delay.ms", 8000);
	/* §21.9: через сколько после переключения проверить подачу (0 — не проверять) */
	int cadence_fix_ms = cfg_get_int ("fps.cadence.fix.ms", 2500);

	/* Огибающая: горизонт наблюдения, предел расширения, подокно, прогрев
	 * (см. fps_env.h и config/afrd2.ini). envelope.ms=0 — решение по первому
	 * подокну (быстрое переключение, как было до 01.10); фактически 0 всё
	 * равно поднимается до skip + sub — раньше этого подокна просто нет. */
	int env_ms = cfg_get_int ("fps.measure.envelope.ms", 20000);
	if (env_ms < 0) env_ms = 0;
	int env_max_ms = cfg_get_int ("fps.measure.envelope.max.ms", 30000);
	if (env_max_ms < env_ms) env_max_ms = env_ms;
	int env_sub_ms = cfg_get_int ("fps.measure.envelope.sub.ms", 5000);
	if (env_sub_ms < 200) env_sub_ms = 200;
	int env_skip_ms = cfg_get_int ("fps.measure.envelope.skip.ms", 2000);
	if (env_skip_ms < 0) env_skip_ms = 0;

	/* Раннее решение огибающей (§21.15): не ждать полного горизонта, если два
	 * разнесённых решаемых подокна согласны по классу (см. fps_env.h).
	 * 0 — выключено (решение только по полному горизонту). */
	int env_early_ms = cfg_get_int ("fps.measure.envelope.early.ms", 0);
	unsigned long long env_early_us = (unsigned long long)env_early_ms * 1000;

	unsigned long long env_us = (unsigned long long)env_ms * 1000;
	unsigned long long env_max_us = (unsigned long long)env_max_ms * 1000;
	unsigned long long env_sub_us = (unsigned long long)env_sub_ms * 1000;
	unsigned long long env_skip_us = (unsigned long long)env_skip_ms * 1000;
	/* Подокно должно начаться не раньше skip и уложиться в горизонт, поэтому
	 * горизонт короче skip + sub не даёт ни одного окна: поднимаем до минимума
	 * (иначе envelope.ms=0 или 5000 навсегда оставлял бы демон без решения). */
	/* Раннему решению нужны ДВА разнесённых подокна после прогрева; плюс
	 * запас ~1 с на дискретность опроса, иначе подтверждающее окно не
	 * помещается между концом опорного и концом выборки */
	if (env_early_us > 0 && env_early_us < (env_skip_us + 2 * env_sub_us + 1000000ULL))
		env_early_us = env_skip_us + 2 * env_sub_us + 1000000ULL;

	unsigned long long env_min_us = env_sub_us + env_skip_us;
	if (env_us < env_min_us) env_us = env_min_us;
	if (env_max_us < env_us) env_max_us = env_us;

	/* Горизонт ограничен ёмкостью кольца наблюдения (FPS_ENV_MAX выборок):
	 * больше него данных не накопить — старые выборки вытесняются. Молчаливое
	 * усечение запрещено: это тот же класс, что «молча выпавший ключ». */
	unsigned long long env_ring_us = (unsigned long long)(FPS_ENV_MAX - 1) * (unsigned long long)cur_hz * 1000;
	if (env_max_us > env_ring_us)
	{
		trace (0, "fps.measure.envelope.max.ms=%d больше ёмкости кольца наблюдения (%llu мс при trigger.poll.ms=%d) — ограничиваю\n",
		       env_max_ms, env_ring_us / 1000, cur_hz);
		env_max_us = env_ring_us;
		if (env_us > env_max_us)
			env_us = env_max_us;
	}

	int start_deb = cfg_get_int ("trigger.start.debounce.ms", 500);
	int stop_deb = cfg_get_int ("trigger.stop.debounce.ms", 1500);
	/* Поправка на систематическое завышение счётчика конкретным плеером
	 * (по умолчанию выключена — см. fps.correct.* в fps_class.h). */
	int correct_enable = cfg_get_int ("fps.correct.enable", 0);
	if (correct_enable)
	{
		const char *clist = cfg_get_str ("fps.correct.list", NULL);
		fps_correct_load (clist);
		trace (1, "поправка частоты по плеерам: %s\n",
		       (clist && *clist) ? clist : "(список пуст)");

		/* Поправка AV1 задаётся отдельным ключом и ставится после
		 * fps_correct_load: load() начинается с fps_correct_clear(), который
		 * сбрасывает и её (см. fps_class.h). */
		const char *av1_pct_val = cfg_get_str ("fps.correct.av1.pct", NULL);
		if (av1_pct_val && av1_pct_val [0])
			fps_correct_set_av1_pct (atof (av1_pct_val),
			                         cfg_get_str ("fps.correct.av1.pkg", NULL));

		const char *av1_val = cfg_get_str ("fps.correct.av1.offset", NULL);
		const char *av1_pkg = cfg_get_str ("fps.correct.av1.pkg", NULL);
		double av1_off_cfg = av1_val ? atof (av1_val) : 0.0;
		fps_correct_set_av1 (av1_off_cfg, av1_pkg);
		if (av1_off_cfg > 0.0)
		{
			int p = (int)(av1_off_cfg * 100.0 + 0.5);
			trace (1, "поправка AV1: минус %d.%02d кадра/с%s%s\n", p / 100, p % 100,
			       (av1_pkg && *av1_pkg) ? ", только для " : "",
			       (av1_pkg && *av1_pkg) ? av1_pkg : "");
		}
	}
	else
	{
		fps_correct_clear ();

		/* R5: выключенная калибровка не должна молча съедать настроенную
		 * поправку AV1 — она действует только при fps.correct.enable=1. */
		const char *av1_off_str = cfg_get_str ("fps.correct.av1.offset", NULL);
		if (av1_off_str && (atof (av1_off_str) > 0.0))
			trace (0, "fps.correct.av1.offset=%s, но fps.correct.enable=0 — поправка AV1 не применена\n", av1_off_str);
	}

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

		/* Перезагрузка конфига: API reconf — сразу, правка файла — раз в
		 * CONFIG_CHECK_PERIOD. Выходим штатным путём (ниже снимается
		 * удержание и флаг backdoor), main.c перечитает конфиг. */
		if (atv14_reconf_pending (&config_timer))
		{
			reconf = true;
			break;
		}

		ticks++;

		/* R34б: статистика для GUI и API (afrd -s, status) — в ATV14 её
		 * больше никто не обновляет */
		if (ticks % 3 == 0)
		{
			atv14_stats_sync (plat, cur_sf_id, orig_sf_id, held);
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

		/* Метка времени выборки — середина чтения (само чтение под нагрузкой
		 * занимает миллисекунды, а окна считаются по крайним выборкам) и его
		 * длительность как неопределённость интервала. */
		unsigned long long read_a = now_us ();
		int poll_rc = vdec_poll (plat->vdec_status, &v0, &vdec_sel);
		unsigned long long read_b = now_us ();
		unsigned long long sample_t = read_a + (read_b - read_a) / 2;
		unsigned long long sample_unc = read_b - read_a;

		if (poll_rc != 0)
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

		/* Провал декодера при смене ролика длится ~650 мс (замер §47.7), а
		 * смена режима — ~380 мс. Дебаунс старта (500 мс) съедал это окно,
		 * поэтому при включённом switch.bump подъём до максимума делается
		 * СРАЗУ, не дожидаясь подтверждения «видео кончилось»: поднять режим
		 * безопасно даже при ложном срабатывании — следующий замер вернёт
		 * нужную частоту. Без этого смена ролика (в т.ч. автовоспроизведение)
		 * наследовала низкий «потолок» предыдущего ролика. */
		if (!new_video && g_switch_bump && (plat->sink != SINK_LEGACY) && video &&
		    !ceiling_recent_within (CEILING_RESTART_WINDOW_US))
			atv14_bump (plat, &orig_sf_id, &cur_sf_id, &want_sf_id, &held);

		/* Наша же смена плотности перезапускает декодер, и канал на время
		 * пропадает. Если принять это за остановку видео, демон сразу вернёт
		 * максимум — именно так матрица приёмки и «поймала» ложный стоп
		 * (живая находка 18.10 на .50: переключение на 50 Гц в 18:32:56 и
		 * возврат на 60 Гц в 18:32:59). Пока сброс свежий, «видео кончилось»
		 * игнорируем: настоящая остановка придёт позже и будет обработана. */
		bool lost_after_reset = !new_video &&
		                        ceiling_recent_within (CEILING_RESTART_WINDOW_US);

		if ((new_video != video) && !lost_after_reset)
		{
			msleep (new_video ? start_deb : stop_deb);

			/* дебаунс не должен сдвигать состояние выбора канала (R61) */
			bool again_active = false;
			if (vdec_probe_active (plat->vdec_status, &again_active) == 0 &&
			    again_active == new_video)
			{
				video = new_video;

				/* Память о режиме, удержанном на паузе, относится к текущему
				 * видео: на новом видео (или после настоящего конца) она
				 * вернула бы режим, не измерив его частоту. */
				pause_forget (&pause_mem);

				/* Провал канала сразу после нашей смены плотности —
				 * следствие сброса потолка, а не новый ролик: режим уже на
				 * максимуме, повторный bump дал бы лишний переход. Признак
				 * одноразовый: настоящий новый ролик его не наследует
				 * (нашло ревью). */
				/* Два сигнала перезапуска (сброс счётчика и провал канала)
				 * приходят на одно событие, поэтому одного флага мало: вторая
				 * проверка увидела бы false и подняла режим, запустив петлю
				 * 24 <-> 60 (живая находка 18.10 на .41). Кроме флага смотрим
				 * и на недавний сброс плотности. */
				bool own_restart = ceiling_take_restart (cur_sf_id) ||
				                   ceiling_recent_within (CEILING_RESTART_WINDOW_US);

				/* Подтверждённый «потолок» прежнего ролика к новому не
				 * относится: его надо сбросить заново (журнал §47.9). */
				if (!own_restart)
					ceiling_forget ();

				/* Запоминаем приложение, которое ведёт это видео: по нему
				 * ловим «залипший» инстанс декодера (плеер ушёл, а канал
				 * остался и счётчик стоит) */
				if (new_video)
				{
					if (!own_restart)
						atv14_bump (plat, &orig_sf_id, &cur_sf_id, &want_sf_id, &held);
					else
						trace (1, "перезапуск декодера после нашего сброса — режим не трогаю\n");

					stuck_reset (&stuck);
					pkg_probe_us = 0;
					pause_logged = false;
					{
						char video_pkg [PKG_NAME_LEN] = { 0 };
						if (pkg_probe (plat, video_pkg, sizeof (video_pkg)) == 0)
						{
							stuck_set_pkg (&stuck, video_pkg);
							trace (2, "видео ведёт приложение %s\n", video_pkg);
						}
					}

					/* Сырое поле status декодера на старте видео: вместе с
					 * записями при паузе и залипании это даёт материал для
					 * решения «отличает ли драйвер конец видео от паузы»
					 * (журнал §31.18). Решение демон по нему не принимает. */
					trace (2, "декодер %s: status %u, frame dur %u\n",
					       v0.device, v0.status, v0.decl_dur);
				}
			}
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
				/* Сброс счётчика сразу после нашей смены плотности — это
				 * не новое видео: конфигурация экрана перезапустила декодер
				 * (журнал §47.9). Потолок уже сброшен, поднимать режим и
				 * обнулять состояние видео не нужно; замер начинается
				 * заново, чтобы решение опиралось на новый счётчик. */
				/* Перезапуск декодера от нашего сброса плотности приходит
				 * позже трёх секунд: сброс -> перезапуск -> счётчик с нуля
				 * занимает 2.5-3.5 с. Окно здесь шире, иначе демон принимал
				 * это за новый ролик и поднимал режим — режим мигал 50 -> 60
				 * сразу после переключения (живая находка 18.10 на .50). */
				bool own_reset = ceiling_take_restart (cur_sf_id) ||
				                 ceiling_recent_within (8000000ULL);

				trace (1, own_reset
				       ? "счётчик кадров сброшен нашей сменой плотности — мерю заново\n"
				       : "счётчик кадров сброшен — новое видео, мерю заново\n");

				/* Следующий ролик в том же плеере начинается с перезапуска
				 * декодера, а не с паузы: поднимаем режим до максимума и
				 * здесь, иначе он унаследует низкий «потолок» предыдущего
				 * ролика (журнал §47.6). */
				if (!own_reset)
				{
					atv14_bump (plat, &orig_sf_id, &cur_sf_id, &want_sf_id, &held);
					ceiling_forget ();
				}

				want_sf_id = -1;
				measure_failed = false;
				retries = 0;
				fast_applied = false;	/* новое видео — быстрое решение заново */
				g_ceiling_no_fast = false;
				fast_class = 0;
				recheck_armed = false;	/* R56: перепроверка — на текущее видео */
				mstime_disable (&recheck_timer);
				cadence_armed = false;
				mstime_disable (&cadence_timer);
				cadence_fixes = 0;
				hdrinfo_armed = false;
				mstime_disable (&hdrinfo_timer);
				/* плеер нового видео может быть другим — поправку
				 * (fps.correct.list) берём заново */
				pkg_probed = false;
				video_corr = 1.0;
				/* кадры двух видео не должны попасть в одну огибающую */
				fps_env_reset (&env);
				env_growing = false;
				/* режим, удержанный на паузе прежнего видео, к новому
				 * не относится (и отсчёт терпения начинается заново) */
				pause_forget (&pause_mem);
				still_since_us = 0;
			}

			if (have_prev_frames && (v0.frames == prev_frames))
				still++;
			else
				still = 0;

			/* Отсчёт терпения к паузе (switch.delay.off на новом пути) идёт
			 * от последнего РОСТА счётчика: замороженный с самого старта
			 * счётчик (AV1, сбой плеера) паузой не считается — у него свой
			 * путь через подачу видео-слоя. */
			if (have_prev_frames && (v0.frames > prev_frames))
				still_since_us = sample_t;

			paused = (still >= 2);
			prev_frames = v0.frames;
			have_prev_frames = true;

			/* Пауза или залипание: пишем status декодера один раз на эпизод —
			 * это точка замера, по которой на стенде видно, различает ли
			 * драйвер паузу и залипший канал (§31.18). Поведение не зависит. */
			if (paused && !pause_logged)
			{
				pause_logged = true;
				trace (1, "счётчик кадров стоит: %s, status %u — пауза или залипший канал\n",
				       v0.device, v0.status);
			}
			else if (!paused)
				pause_logged = false;

			/* Залипший канал (§25.1): счётчик стоит, режим удержан, но
			 * приложение, которое вело это видео, ушло с переднего плана —
			 * значит в системе остался инстанс декодера без плеера. Настоящая
			 * пауза так не выглядит (плеер остаётся на экране), а при
			 * pause.keep=1 режим с флагом backdoor иначе остался бы навсегда.
			 * Проверяем не чаще раза в 5 с; состояние и переходы — в stuck.c
			 * (покрыты tests/test_stuck.c: живьём это состояние воспроизводится
			 * только руками).
			 *
			 * Проба нужна и при пустом пакете: если она не удалась на старте
			 * видео, сторож иначе не работает вообще (был условный выход). */
			if (paused && held && (now_us () - pkg_probe_us) > 5000000ULL)
			{
				char top_pkg [PKG_NAME_LEN] = { 0 };
				pkg_probe_us = now_us ();
				int probe_rc = pkg_probe (plat, top_pkg, sizeof (top_pkg));

				switch (stuck_step (&stuck, probe_rc, top_pkg))
				{
				case STUCK_BLIND:
					/* Отказ пробы: без строки в журнале он неотличим от
					 * «всё в порядке» */
					trace (0, "передний план не определён (код %d), "
					       "а счётчик кадров стоит — залипший канал проверить не могу\n",
					       probe_rc);
					break;

				case STUCK_FILL_PKG:
					/* Проба на старте видео сорвалась — пакет запомнен сейчас */
					trace (1, "видео ведёт приложение %s (проба на старте не удалась)\n",
					       stuck.pkg);
					break;

				case STUCK_GIVE_UP:
					trace (0, "плеер %s ушёл с переднего плана (сейчас %s), "
					       "а счётчик кадров стоит (%s status %u) — снимаю удержание режима\n",
					       stuck.pkg, top_pkg, v0.device, v0.status);
					atv14_give_up (plat, &orig_sf_id, &cur_sf_id, &held);
					video = false;
					want_sf_id = -1;
					stuck_reset (&stuck);
					fps_env_reset (&env);
					env_growing = false;
					break;

				case STUCK_NONE:
					/* Либо пауза, либо ещё одно несовпадение — состояние
					 * уже обновлено в stuck_step */
					break;
				}
			}
		}
		else
		{
			still = 0;
			paused = false;
			have_prev_frames = false;
			stuck_reset (&stuck);
		}

		/* Терпение к паузе (switch.delay.off на новом пути): пауза дольше
		 * предела считается концом видео, а на возобновлении удержанный режим
		 * возвращается без нового замера. При switch.restore.on.stop=0 режим
		 * с паузы не возвращается вовсе — тогда предел не нужен (решение
		 * принимает pause.c, тест tests/test_pause.c). */
		int pause_patience_ms = restore_on_stop ? g_switch_delay_off : 0;
		unsigned long long stall_us = still_since_us ? (sample_t - still_since_us) : 0;
		pause_action_t pact = pause_step (paused, held, video, cur_sf_id,
		                                  cur_sf_id, stall_us,
		                                  pause_patience_ms, &pause_mem);
		if (pact == PAUSE_END)
			trace (0, "пауза дольше %d мс (switch.delay.off) — считаю видео "
			       "законченным, возвращаю режим; на возобновлении вернусь "
			       "к удержанному без замера\n", pause_patience_ms);
		else if (pact == PAUSE_RESUME)
		{
			/* Режим ставим целью до развилки ниже, чтобы сработал обычный
			 * путь удержания (идемпотентен: cur_sf_id уже проверен в pause.c) */
			want_sf_id = pause_mem.sf_id;
			trace (1, "счётчик кадров снова растёт — возвращаю удержанный режим (%d) "
			       "без нового замера\n", pause_mem.sf_id);
		}

		/* pause.keep==0: пауза обрабатывается как стоп (guard вернёт режим);
		 * предел паузы (PAUSE_END) — то же самое, но по таймеру */
		bool video_eff = video;
		if ((!pause_keep && paused) || (pact == PAUSE_END))
			video_eff = false;

		/* Пакет в списке switch.skip.pkg: режим не переключаем и не удерживаем
		 * (плеер сам выбирает темп отрисовки под текущий режим экрана, а
		 * удержанный низкий режим «зажимал» бы следующий ролик — проверено
		 * живьём на YouTube TV, см. README, ограничение 8). */
		skip_pkg = (g_skip_pkgs.size > 0) && video_eff && (stuck.pkg [0] != 0) &&
		           strlist_contains (&g_skip_pkgs, stuck.pkg);

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
			fast_applied = false;	/* видео кончилось — решение не переносим */
			fast_class = 0;
			/* R56: перепроверка — тоже на текущее видео */
			recheck_armed = false;
			mstime_disable (&recheck_timer);
			/* поправка плеера относится к текущему видео */
			pkg_probed = false;
			video_corr = 1.0;
			/* видео закончилось/пауза — наблюдение начинается заново */
			fps_env_reset (&env);
			env_growing = false;
			cadence_armed = false;
			mstime_disable (&cadence_timer);
			cadence_fixes = 0;
			hdrinfo_armed = false;
			mstime_disable (&hdrinfo_timer);
			counter_last_grow_us = 0;
			counter_stall_logged = false;
		}
		else if (hdrinfo_armed && mstime_expired (&hdrinfo_timer))
		{
			/* Режим уже устоялся — система выставила свои HDR/SDR-значения.
			 * Проверка не зависит от fps.cadence.fix.ms. */
			hdrinfo_armed = false;
			hdrinfo_refresh (plat->vendor_cli);
			hdrinfo_log ("атрибуты дисплея после переключения");
		}
		else if (cadence_armed && mstime_expired (&cadence_timer))
		{
			/* §21.9: после смены режима подача иногда становится вдвое реже
			 * периода дисплея (видео «как 13 fps»). Лечение — переключить
			 * режим «туда-обратно»: повторная выдача того же режима не
			 * помогает, а смена его пересобирает vsync-путь HWC/SF. */
			cadence_armed = false;

			/* Цветовое пространство: применяем ещё раз, когда переключение
			 * режима уже устоялось. Фреймворк переустанавливает цветовое
			 * пространство сам — асинхронно после смены режима, и применение
			 * «в момент» переключения им же и перебивалось (проверено
			 * живьём: через пару секунд значение возвращалось к исходному). */
			{
				char *cm2 = sysfs_read (g_mode_path);
				if (cm2)
				{
					char nm2 [64];
					size_t i2 = 0;
					while (((i2 + 1) < sizeof (nm2)) && cm2 [i2] &&
					       cm2 [i2] != '\n' && cm2 [i2] != '\r')
					{
						nm2 [i2] = cm2 [i2];
						i2++;
					}
					nm2 [i2] = 0;
					free (cm2);

					if (nm2 [0])
						colorspace_apply (nm2);
				}
			}

			int pm = 0, am = 0;
			int expect_ms = cadence_expect_ms;
			if (atv14_cadence_halved (expect_ms, &pm, &am))
			{
				/* preferred_id — id из пространства DisplayManager: как
				 * цель транзакции 1035 он годится, только если есть в
				 * таблице SF (иначе «лечение» билось бы о пропуск). */
				int back = (orig_sf_id >= 0) ? orig_sf_id
				           : sfmodes_restore_target (g_sf_modes, g_sf_modes_n,
				                                     preferred_id, -1);
				/* want_sf_id >= 0 обязателен: перепроверка замера (R56) сбрасывает
				 * цель в -1, а «лечение» возвращает режим «туда» и не может
				 * вернуть «обратно» — устройство оставалось в исходном 60 Гц при
				 * удержании и поднятом флаге backdoor до нового замера (~14 с).
				 * Живая находка 2026-10-08 (прогон матрицы, клип long_50). */
				if ((cadence_fixes < 2) && (back >= 0) &&
				    (want_sf_id >= 0) && (back != want_sf_id))
				{
					cadence_fixes++;
					trace (0, "подача вдвое реже ожидаемого (%d мс при периоде %d мс, ожидание %d мс) — переключаю режим туда-обратно (лечение %d)\n",
					       am, pm, expect_ms, cadence_fixes);
					if (atv14_apply_mode (plat, back, "лечение подачи: возврат") == 0)
					{
						msleep (200);
						if (atv14_apply_mode (plat, want_sf_id, "лечение подачи: повтор") == 0)
							cur_sf_id = want_sf_id;
					}
					mstime_update ();
					mstime_arm (&cadence_timer, 2000);
					cadence_armed = true;	/* проверить результат лечения */
				}
				else
					trace (0, "подача вдвое реже ожидаемого (%d мс при периоде %d мс, ожидание %d мс) — лечение не применяю%s\n",
					       am, pm, expect_ms,
					       (want_sf_id < 0)
					       ? " (цель переключения сброшена — жду нового замера)"
					       : "");
			}
			else if (am > 0)
			{
				int base = (expect_ms > pm) ? expect_ms : pm;
				/* Смешанная картина (часть интервалов в норме, часть
				 * растянута) — это не дисплейная «половинная подача»,
				 * а плеер, подающий кадры реже: переключение режима
				 * ему безразлично, лечит только перезапуск ролика.
				 * Порог 1.125 подобран по живому случаю: ролик 60 fps,
				 * стартовавший под 50 Гц, давал 50.28 к/с (20 мс при
				 * ожидании 17, то есть 1.18×) и раньше проходил как
				 * «подача в порядке». */
				if (am * 8 > base * 9)
					trace (0, "подача неровная: %d мс при периоде %d мс, ожидание %d мс — плеер, вероятно, выбрал темп при старте под другим режимом; помогает перезапуск ролика\n",
					       am, pm, expect_ms);
				else
					trace (2, "подача в порядке: %d мс при периоде %d мс, ожидание %d мс\n",
					       am, pm, expect_ms);
			}
		}
		else if (recheck_armed && mstime_expired (&recheck_timer))
		{
			/* R56: прошло fps.recheck.delay.ms после переключения — меряем
			 * ещё раз (та же логика окна и уточнения). Если класс тот же,
			 * guard_decide не тронет режим и мигания не будет; если другой
			 * (например, стартовое окно занизило 60 fps до 50) — поправим. */
			trace (1, "перепроверка замера после переключения\n");
			want_sf_id = -1;
			measure_failed = false;
			retries = 0;
			/* Метку быстрого решения НЕ снимаем: заявленная частота от
			 * драйвера может «плавать» (на синтетическом клипе она то 24, то
			 * 30), и повторное быстрое решение каждый раз переставляло бы
			 * режим — живая находка 2026-10-08: 1080p24hz -> 1080p60hz ->
			 * снова 24. Быстрое решение принимается один раз на видео, дальше
			 * источник истины — замер (он же и поправит режим при расхождении). */
		}
		/* Замер идёт и после БЫСТРОГО решения: оно предварительное, и его
		 * обязана проверить огибающая (иначе заявленная частота от драйвера
		 * осталась бы непроверенной — живая находка 2026-10-08: на клипе, где
		 * драйвер заявляет 30 fps при фактических 23.976, замер не запускался
		 * вовсе). Условие «нужно новое решение» — это `want_sf_id < 0` ИЛИ
		 * быстрое решение, ждущее проверки. */
		else if (!measure_failed && ((want_sf_id < 0) || fast_applied))
		{
			int hz = 0;

			/* Поправка частоты конкретного плеера: пакет на переднем плане
			 * определяется один раз на видео (fps.correct.list). */
			if (!pkg_probed)
			{
				pkg_probed = true;
				if (correct_enable || (g_mode_min_hz_count > 0) || (g_skip_pkgs.size > 0))
				{
					pkg_probe_logged (plat);
					video_corr = fps_correct_factor (g_cur_pkg);
					if (video_corr != 1.0)
					{
						int p = (int)(fabs (video_corr - 1.0) * 10000.0 + 0.5);
						trace (1, "плеер %s: поправка замера %s%d.%02d %% (fps.correct.list)\n",
						       g_cur_pkg, (video_corr < 1.0) ? "-" : "+", p / 100, p % 100);
					}
				}
			}

			/* fps.prefer.api: подсказку плеера (24.8 fixed-point, живёт 1 с)
			 * предпочитаем собственному замеру — она точнее и мгновенна */
			int api_hz = 0;
			if (prefer_api && mstime_running (&g_frame_rate_hint.stamp))
				api_hz = g_frame_rate_hint.fps;

			if (api_hz)
			{
				hint_seen = g_frame_rate_hint.stamp;
				hz = fps_rate_hz (fps_rate_index_for_hz (api_hz));
				trace (1, "видео: " HZ_FMT " Гц по подсказке плеера, ищу режим\n",
				       HZ_ARGS (hz));
			}
			else if (v0.active)
			{
				/* Наблюдение (fps_env): счётчик кадров на ATV14 завышен
				 * односторонне, поэтому решение берётся по нижней огибающей
				 * подокон. Пока горизонт не набран — режим не трогаем. */
				bool env_grew = fps_env_push (&env, sample_t, v0.frames, sample_unc);
				if (env_grew)
				{
					env_growing = true;
					counter_last_grow_us = sample_t;
					counter_stall_logged = false;
				}
				else if (env_growing)
				{
					/* только переход «рос → стоит» (пауза/сброс счётчика):
					 * иначе строка печаталась бы на каждой итерации паузы */
					trace (1, "счётчик кадров не растёт — наблюдение начато заново\n");
					env_growing = false;
				}

				/* Заморозка счётчика: декодер числится активным, но кадры не
				 * идут. Частоту так не определить, и демон молча ничего не
				 * делает до конца видео — сообщаем один раз и громко. */
				if (counter_last_grow_us &&
				    (sample_t - counter_last_grow_us) > COUNTER_STALL_MS * 1000ULL &&
				    !counter_stall_logged)
				{
					counter_stall_logged = true;
					trace (0, "счётчик кадров декодера не растёт %llu с (%s status %u, "
					       "декодер активен, частота неизвестна) — режим не переключаю\n",
					       (unsigned long long)((sample_t - counter_last_grow_us) / 1000000ULL),
					       v0.device, v0.status);

					/* Запасной источник — подача видео-слоя: она от счётчика
					 * не зависит, а картинка идёт. Пробуем один раз за эпизод
					 * (флаг снимется, когда счётчик снова пойдёт). */
					int lhz = atv14_layer_fps_hz ();
					if (lhz)
					{
						sf_mode_t act;
						int w = 1920, h = 1080;
						if (sf_active_mode (plat, &act) == 0)
						{
							w = act.width;
							h = act.height;
						}

						int id = mode_resolve (plat, lhz, w, h, mode_min_hz_for (g_cur_pkg));
						if (id >= 0)
						{
							want_sf_id = id;
							/* Подача кадров есть — это не пауза: отсчёт
							 * терпения к паузе начинаем заново, иначе
							 * «терпение» сняло бы только что выбранный режим
							 * у играющего видео. */
							still_since_us = sample_t;
							trace (0, "частота по подаче видео-слоя: " HZ_FMT
							       " Гц (счётчик кадров молчит) — переключаю режим\n",
							       HZ_ARGS (lhz));
						}
					}
					else
						trace (1, "подача видео-слоя частоту не дала\n");
				}

				/* Быстрое решение по заявленной частоте: штатный системный
				 * AFR Amlogic берёт её из vdec_status и потому переключает за
				 * 2–4 с (журнал §33.19). Делаем так же, но предварительно:
				 * замер продолжает работать и ниже поправит режим, если
				 * заявленная частота расходится с фактической (наш синтетический
				 * клип: драйвер рапортует 30 fps при реальных 23.976). */
				if (g_fps_declare_fast && !g_ceiling_no_fast && !fast_applied && !held &&
				    (v0.decl_fps > 0))
				{
					int dclass = fps_declared_class ((int)v0.decl_fps);
					if (dclass)
					{
						int w = 1920, h = 1080;
						int cur = atv_current_target (plat, &w, &h);

						int did = mode_resolve (plat, dclass, w, h, mode_min_hz_for (g_cur_pkg));
						if (did < 0)
						{
							/* Решения нет — повторять его на каждой итерации
							 * цикла (~300 мс) незачем: набор режимов ТВ за
							 * время ролика не меняется, а журнал забивался
							 * десятками одинаковых строк (нашло финальное
							 * ревью). Источник истины ниже — замер. */
							fast_applied = true;
							fast_class = 0;
							trace (1, "заявлено %u fps, но режима под эту частоту нет\n",
							       v0.decl_fps);
						}
						else if (did == cur)
						{
							/* Режим уже нужный — sink не зовём (лишнее
							 * переключение = мигание), но решение принимаем:
						 * замер ниже проверит заявленную частоту. В журнал
						 * идёт выбранный режим, а не частота класса: класс 30
						 * уходит в 1080p60hz удвоением, и строка «режим уже
						 * 30 Гц» читалась бы как противоречие (живая находка
						 * прогона приёмки). */
							fast_applied = true;
							fast_class = dclass;
							trace (1, "быстрое решение: заявлено %u fps — "
							       "режим " HZ_FMT " Гц уже стоит, не трогаю\n",
							       v0.decl_fps, HZ_ARGS (atv14_mode_hz (plat, did)));
						}
						else
						{
							/* Запоминаем исходный режим ДО применения: guard_decide
							 * заполняет orig_sf_id только когда want != cur, а здесь
							 * мы применяем сами — без этой записи возврат на
							 * остановке не сработал бы (нашло финальное ревью:
							 * на честном 25 fps рабочий стол оставался в 50 Гц). */
							if (!held && (orig_sf_id < 0) && (cur >= 0))
							{
								orig_sf_id = cur;
								held = true;
							}

							if (atv14_apply_mode (plat, did, "быстрое решение") == 0)
							{
								fast_applied = true;
								fast_class = dclass;
								want_sf_id = did;
								cur_sf_id = did;
								trace (0, "быстрое решение по заявленной частоте: %u fps — "
								       "режим применён сразу, замер проверит\n", v0.decl_fps);
							}
							else
							{
								/* применить не удалось — удержание не начинаем,
								 * иначе демон считал бы, что режим уведён, и
								 * снимал бы удержание с чужого режима */
								if (held && (orig_sf_id == cur))
								{
									held = false;
									orig_sf_id = -1;
								}

								/* И больше не пробуем до конца видео: повтор на
								 * каждой итерации цикла (~300 мс) — это fork/exec
								 * sh + app_process и строки в журнале на весь
								 * ролик (нашло финальное ревью). Новое видео
								 * сбросит признак. */
								fast_applied = true;
								fast_class = 0;
								measure_failed = true;
								trace (0, "быстрое решение: режим не применился — "
								       "больше не пробую до конца видео\n");
							}
						}
					}
				}

				bool env_full = fps_env_ready (&env, env_us);
				/* Раннее решение: горизонт ещё не набран, но если два
				 * разнесённых решаемых подокна согласны по классу — решаем
				 * уже сейчас (проверка не ждёт горизонта). */
				bool env_early = !env_full && (env_early_us > 0) &&
				                 fps_env_ready (&env, env_early_us);

				if (!env_full && !env_early)
					continue;

				double av1_off = correct_enable
				                 ? fps_correct_av1_offset (v0.device, g_cur_pkg) : 0.0;

				/* Относительная поправка AV1 (fps.correct.av1.pct) умножается
				 * на поправку плеера: обе — делители измеренной частоты. */
				double av1_fac = correct_enable
				                 ? fps_correct_av1_factor (v0.device, g_cur_pkg) : 1.0;
				double env_corr = video_corr * av1_fac;

				/* Раннее решение: горизонтом для отбора подокон служит вся
				 * накопленная выборка (иначе подтверждающее подокно длиной
				 * sub не помещается между концом опорного окна и «ранним
				 * горизонтом» и согласия не находится никогда) */
				unsigned long long env_span_us = (env.n > 1)
					? (env.s [env.n - 1].t_us - env.s [0].t_us) : env_us;

				int env_hz = 0;
				unsigned long long env_used_us = env_early ? env_early_us : env_us;
				bool env_decided = env_early
					? fps_env_decide_early (&env, env_span_us, env_skip_us, env_sub_us,
					                        env_corr, av1_off, &env_hz)
					: fps_env_decide (&env, env_us, env_skip_us, env_sub_us,
					                  env_corr, av1_off, &env_hz);

				if (env_early && env_decided)
					trace (1, "раннее решение огибающей: подокна согласны (%d мс наблюдения)\n",
					       (int)(env_early_us / 1000));

				/* базовый горизонт неоднозначен — решаем на расширенном
				 * (fps.measure.envelope.max.ms), но только когда он набран */
				if (!env_decided && env_full && fps_env_ready (&env, env_max_us))
				{
					env_decided = fps_env_decide (&env, env_max_us, env_skip_us, env_sub_us,
					                              video_corr, av1_off, &env_hz);
					env_used_us = env_max_us;
				}

				if (!env_decided)
				{
					/* решения нет ни на одном горизонте: наблюдаем дальше
					 * (попытку не тратим), но не дольше предела */
					if (!fps_env_ready (&env, env_max_us))
						continue;

					trace (1, "огибающая не дала решения за %d с наблюдения\n",
					       (int)(env_max_us / 1000000));
					if (++retries >= max_retries)
					{
						trace (1, "частоту определить не удалось (%d попыток), пропускаю\n", retries);
						measure_failed = true;
						want_sf_id = -1;
						if (!g_sf_table_changed)
							atv14_give_up (plat, &orig_sf_id, &cur_sf_id, &held);
					}

					fps_env_reset (&env);
					env_growing = false;
					continue;
				}

				hz = env_hz;

				if (av1_off > 0.0)
					trace (1, "решение уточнено поправкой AV1: минус %.2f кадра/с\n", av1_off);

				/* заявленная длительность кадра (frame dur) уточняет решение —
				 * как и раньше, но теперь уточняется решение огибающей */
				if (v0.decl_dur)
				{
					int refined = 0;
					if (fps_refine_declared (hz, 96000.0 / (double)v0.decl_dur,
					                         (double)declared_pct, &refined))
					{
						if (refined != hz)
							trace (1, "частота уточнена по frame dur (%u): "
							       HZ_FMT " Гц вместо " HZ_FMT " Гц\n",
							       v0.decl_dur, HZ_ARGS (refined), HZ_ARGS (hz));

						hz = refined;
					}
				}

				/* Быстрое решение было — сверяем его с замером: расхождение
				 * по ГРУППЕ частот значит, что драйвер заявил неверно (23.976 и
				 * 24 — одна группа, их ложным расхождением не считаем). */
				if (fast_applied && fps_group_differs (fast_class, hz))
					trace (0, "заявлено %d.%02d Гц, замер дал " HZ_FMT
					       " Гц — правлю режим\n",
					       fast_class >> 8, (100 * (fast_class & 255) + 128) >> 8,
					       HZ_ARGS (hz));

				trace (1, "видео: " HZ_FMT " Гц по огибающей (%d с наблюдения, "
				       "горизонт %d мс), ищу режим\n",
				       HZ_ARGS (hz),
				       (int)((env.s [env.n - 1].t_us - env.s [0].t_us) / 1000000),
				       (int)(env_used_us / 1000));

				/* следующее измерение (перепроверка, новое видео) — заново */
				fps_env_reset (&env);
				env_growing = false;
			}
			else
				continue;

			retries = 0;

			/* mode.use.fract: 1 — предпочитать дробные (по умолчанию в профиле),
			 * 2 — целые (24.000/30.000/60.000), 0 — «как уместно»: для нашего
			 * класса это дробный представитель, потому что счётчик пару 0.1 %
			 * не различает. Если точного режима в таблице SurfaceFlinger нет
			 * (ТВ не поддерживает), resolver подберёт ближайший. */
			if (use_fract == 2)
			{
				int exact = fps_rate_exact_member (hz);
				if (exact)
				{
					trace (1, "mode.use.fract=0: режим " HZ_FMT " Гц вместо " HZ_FMT " Гц\n",
					       HZ_ARGS (exact), HZ_ARGS (hz));
					hz = exact;
				}
			}

			/* ожидаемый интервал подачи контента (мс): период кадра по
			 * измеренной частоте — именно с ним сравнивает проверка подачи
			 * (период дисплея не годится: 24-фпс видео на 60 Гц даёт 2.5
			 * периода, и это норма) */
			cadence_expect_ms = (hz > 0) ? (256000 + hz / 2) / hz : 0;

			/* Для плеера из switch.skip.pkg переключения не будет, но проверку
			 * подачи всё равно делаем — чтобы в журнале был ответ, если плеер
			 * сам подаёт кадры неровно (переключение режима ему безразлично,
			 * лечится перезапуском ролика). */
			if (skip_pkg && (cadence_fix_ms > 0) && !cadence_armed)
			{
				mstime_update ();
				mstime_arm (&cadence_timer, (uint32_t)cadence_fix_ms);
				cadence_armed = true;
			}

			/* текущий режим / исходный id */
			if (g_sf_modes_n <= 0)
				sf_modes_refresh (plat);

			int w = 1920, h = 1080;
			(void) atv_current_target (plat, &w, &h);

			cur_sf_id = mode_resolve (plat, hz, w, h, mode_min_hz_for (g_cur_pkg));
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
		/* Подсказка плеера — прямой запрос точной частоты (24.8 fixed-point,
		 * живёт 1 с). Эта ветка ловит случай, когда режим уже выбран
		 * (`want_sf_id >= 0`) или замер не удался: раньше подсказка читалась
		 * только до первого решения, и смена эпизода/канала в том же плеере
		 * оставляла прежний режим (R53). Подсказка применяется один раз. */
		else if (prefer_api && video_eff &&
		         mstime_running (&g_frame_rate_hint.stamp) &&
		         (g_frame_rate_hint.stamp != hint_seen))
		{
			int hint_hz = fps_rate_hz (fps_rate_index_for_hz (g_frame_rate_hint.fps));

			if (g_sf_modes_n <= 0)
				sf_modes_refresh (plat);

			int w = 1920, h = 1080;
			(void) atv_current_target (plat, &w, &h);

			int hint_id = mode_resolve (plat, hint_hz, w, h, mode_min_hz_for (g_cur_pkg));
			if (hint_id < 0)
			{
				/* режима нет в текущей таблице (напр. частота есть только для
				 * другого разрешения): сказать об этом в лог — иначе это
				 * неотличимо от «подсказка потеряна» (симптом R53), и не
				 * повторять резолв на каждой итерации, пока подсказка жива */
				hint_seen = g_frame_rate_hint.stamp;
				trace (1, "режим для " HZ_FMT " Гц (подсказка плеера) не найден в списке\n",
				       HZ_ARGS (hint_hz));
			}
			else
			{
				hint_seen = g_frame_rate_hint.stamp;
				measure_failed = false;
				want_sf_id = hint_id;
				trace (1, "видео: " HZ_FMT " Гц по подсказке плеера, режим id %d\n",
				       HZ_ARGS (hint_hz), hint_id);
			}
		}

		/* актуальный режим перечитываем у системы: так мы видим и то, что
		 * переключило приложение со своим AFR, и не расходимся с реальностью.
		 * На ATV11 источник — узел режима (таблицы SurfaceFlinger нет). */
		{
			int cur = atv_current_target (plat, NULL, NULL);
			if (cur >= 0)
				cur_sf_id = cur;
		}

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

		/* Плеер из switch.skip.pkg: ничего не переключаем и не удерживаем.
		 * Если что-то было удержано от предыдущего видео (плеер сменился или
		 * пакет добавили в список на ходу) — возвращаем исходный режим и
		 * снимаем флаг backdoor, иначе он остался бы поднятым. */
		if (skip_pkg)
		{
			if (!skip_logged)
			{
				skip_logged = true;
				trace (0, "пакет %s в списке switch.skip.pkg — режим не переключаю и не удерживаю\n",
				       stuck.pkg);
			}

			want_sf_id = -1;

			if (held)
			{
				int rc = atv14_apply_mode (plat, orig_sf_id,
				                           "switch.skip.pkg: возврат, флаг backdoor снят");
				if (rc == 0)
				{
					cur_sf_id = orig_sf_id;
					orig_sf_id = -1;
				}
				guard_note_restored (&held);
			}
		}
		else
			skip_logged = false;

		/* Вариант 3: пока видео не идёт, следим за передним планом. Если
		 * режим ниже максимума и на экран вышел новый пакет (обычно плеер),
		 * поднимаем режим заранее — это окно до того, как плеер создаст
		 * поверхность и запомнит частоту (журнал §49.13).
		 *
		 * Опрос стоит 60-70 мс на боксе, поэтому делается только когда он
		 * может что-то изменить: режим ниже максимума, видео не идёт, и не
		 * чаще раза в секунду. */
		if (g_switch_bump && g_switch_bump_on_app && !video &&
		    (plat->sink != SINK_LEGACY) && (g_bump_pkgs.size > 0))
		{
			int bw = 0, bh = 0;
			int bcur = atv_current_target (plat, &bw, &bh);
			int bmax = bump_target_mode (plat, bw, bh);

			if ((bcur >= 0) && (bmax >= 0) && (bmax != bcur) &&
			    ((now_us () - fg_probe_us) > 1000000ULL))
			{
				fg_probe_us = now_us ();

				char fgp [PKG_NAME_LEN] = { 0 };
				if (pkg_probe (plat, fgp, sizeof (fgp)) == 0 && fgp [0] &&
				    strcmp (fgp, fg_seen) != 0 &&
				    (fgp [0] != 0) && !fg_is_desktop (fgp))
				{
					memcpy (fg_seen, fgp, sizeof (fg_seen));

					/* Подъём — только плеерам из switch.bump.pkg: у
					 * остальных зажим не подтверждён, а лишний переход
					 * режима они видят как «щелчок» (журнал §55.9). */
					if (!bump_pkg_allows_pkg (fgp))
						trace (1, "передний план: %s — не в switch.bump.pkg, "
						       "режим не поднимаю\n", fgp);
					else
					{
						trace (1, "передний план: %s — поднимаю режим заранее\n", fgp);

						if (atv14_bump (plat, &orig_sf_id, &cur_sf_id, &want_sf_id, &held) == 0)
						{
							/* Удержания здесь нет: цель подъёма — чтобы рабочий
							 * стол (и следующий ролик) начинались с максимума.
							 * Иначе guard_decide сразу вернёт «исходный» режим,
							 * то есть повторно применит тот же режим и потратит
							 * до секунды (живая находка 18.10). */
							held = false;
							orig_sf_id = -1;
						}
					}
				}
			}
		}

		act = guard_decide (video_eff, want_sf_id, cur_sf_id, &orig_sf_id, &held);

		/* Цель возврата после видео: id, который был на экране до нашего
		 * переключения (его запомнил guard_decide в orig_sf_id), а не
		 * preferred_id из dumpsys display. preferred_id — нумерация
		 * DisplayManager, транзакция 1035 требует id из таблицы SurfaceFlinger,
		 * и номера могут совпасть, означая разные режимы: на стенде ATV14
		 * preferred=1, а id 1 в таблице SF — 60.000 Гц при исходном 59.94
		 * (живая находка 2026-10-08 — возвращались в 60.000). preferred
		 * остаётся запасным: если запомненный id в таблице отсутствует
		 * (нумерация сменилась после рестарта SF) или неизвестен (видео уже
		 * играло на старте демона), sfmodes_restore_target возьмёт его. */
		if (act == GUARD_SWITCH)
		{
			int back = sfmodes_restore_target (g_sf_modes, g_sf_modes_n,
			                                   preferred_id, orig_sf_id);
			/* Ни preferred, и запомненный cur отсутствуют в таблице SF: когда
			 * видео кончится, guard_decide() вернёт GUARD_NONE и удержание
			 * снимется без строки «id N отсутствует в текущей таблице режимов»
			 * (atv14_apply_mode при sf_id < 0 её не пишет) — фиксируем причину
			 * здесь, состояние экрана от этого не меняется. */
			if ((back < 0) && (preferred_id >= 0))
				trace (1, "цель возврата (preferred id %d) отсутствует в таблице режимов — возврат невозможен\n",
				       preferred_id);
			orig_sf_id = back;
		}

		if (act == GUARD_SWITCH)
		{
			int apply_rc = atv14_apply_mode (plat, want_sf_id, "переключение пропущено");
			if (apply_rc != 0)
			{
				/* Отказ применения (в том числе ненулевой код вендорного
				 * помощника): не повторять на каждой итерации цикла (~300 мс) —
				 * это fork/exec sh + app_process и строки в журнале на весь
				 * ролик. Признак сбрасывается новым видео. */
				if ((apply_rc == -2) && (plat->sink == SINK_VENDOR_MODE))
				{
					measure_failed = true;
					trace (0, "вендорный SystemControl не применил режим — "
					       "больше не пробую до конца видео\n");
				}
			}

			if (apply_rc == 0)
			{
				cur_sf_id = want_sf_id;

				/* Возврат режима после паузы (PAUSE_RESUME) — не новое
				 * решение: частота этого видео проверена ещё до паузы.
				 * Перепроверка замера и проверка подачи здесь вредны: их
				 * окна захватывают паузу, когда кадры не шли, и проверка
				 * подачи ложно решает «подача вдвое реже» — режим уходил
				 * в переключение туда-обратно, а после него следом возврат
				 * (живая находка 2026-10-08, журнал §32.9). */
				bool resume_switch = (pact == PAUSE_RESUME);

				/* R56: через recheck_ms перемерить и поправить режим, если
				 * стартовое окно было смещено (один раз на видео) */
				if ((recheck_ms > 0) && !recheck_armed && !resume_switch)
				{
					/* g_mstime обновляется в начале итерации, а с тех пор
					 * прошло окно замера (до 6 с) и dumpsys — берём свежее
					 * время, иначе перепроверка отсчитывается от старта
					 * итерации и срабатывает сразу после переключения */
					mstime_update ();
					mstime_arm (&recheck_timer, (uint32_t)recheck_ms);
					recheck_armed = true;
					trace (1, "перепроверка замера через %d мс\n", recheck_ms);
				}

				/* §21.9: проверить, что кадры реально идут на подачу
				 * (кроме возврата после паузы — см. resume_switch выше) */
				if ((cadence_fix_ms > 0) && !resume_switch)
				{
					mstime_update ();
					mstime_arm (&cadence_timer, (uint32_t)cadence_fix_ms);
					cadence_armed = true;
				}

				/* HDR/SDR-атрибуты: перечитать, когда система выставит свои
				 * значения (асинхронно после смены режима) */
				mstime_update ();
				mstime_arm (&hdrinfo_timer, HDRINFO_SETTLE_MS);
				hdrinfo_armed = true;

				/* Режим применён на живом видео: если он выше прежнего
				 * «потолка» плеера, тот потолок уже не даст нужной подачи
				 * (журнал §47.9). Сбрасываем его сменой плотности. */
				if (!resume_switch)
					ceiling_maybe_reset (plat, want_sf_id, video_eff);
			}
		}
		else if ((act == GUARD_NONE) && video_eff && (want_sf_id >= 0))
		{
			/* Решение есть, а режим уже стоит: ровно случай зажима — плеер
			 * начал сессию под низкой частотой, и повторная выдача того же
			 * режима его не расклинит (журнал §47.9). */
			ceiling_maybe_reset (plat, want_sf_id, video_eff);
		}
		else if (act == GUARD_RESTORE)
		{
			/* Возврат по настоящему стопу (декодер исчез) отключается ключом
			 * switch.restore.on.stop; возврат по паузе (pause.keep==0)
			 * выполняется всегда. Флаг backdoor снимаем в любом случае.
			 * При switch.bump=1 возврат делается всегда: цель в этом режиме —
			 * максимальная частота, с которой следующий ролик начнётся без
			 * низкого «потолка» (журнал §47.6). */
			if (restore_on_stop || v0.active || g_switch_bump)
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

	/* reconf/правка конфига: ret>0 — main.c перечитает конфиг и продолжит. */
	return reconf ? 1 : 0;
}

/* --------- * --------- * --------- * --------- * --------- * --------- */

/* Вывод предупреждений аудита ключей конфига — в лог демона. */
static void cfg_audit_warn (const char *msg, void *ctx)
{
	(void)ctx;
	trace (0, "%s\n", msg);
}

int afrd_run ()
{
	/* Статистику публикуем ДО платформенной разведки: shmem заводится пустым
	 * (shmem_init в afrd_init), а `dumpsys SurfaceFlinger` внутри platform_detect
	 * занимает секунды — и всё это время `afrd -s`, API `status` и GUI показывали
	 * «демон выключен: нет» и 0.00 Гц у работающего демона. Приёмка 09.10 ловила
	 * это сразу после перезагрузки конфига (пункт «помощник завис»: enabled=no,
	 * тогда как демон жив и работал). Частоты здесь берутся из текущего режима
	 * (/sys/class/display/mode); ATV14-цикл ниже уточняет их из таблицы
	 * SurfaceFlinger. */
	update_stats ();

	g_plat = platform_detect ();

	/* Платформа определяется здесь, а список режимов читается ещё в afrd_init
	 * (там g_plat пуст) — и на вендорном пути он попадал из hdmi.sysfs, из-за
	 * чего ключ vendor.disp.cap оставался мёртвым (нашло финальное ревью).
	 * Перечитываем по ключу платформы: пока список ещё никем не использован,
	 * это одна операция чтения. */
	if (g_plat.sink == SINK_VENDOR_MODE)
		modes_refresh_platform ();

	/* switch.skip.pkg: умолчание зависит от пути (журнал §48.10).
	 *
	 * На Android 12-14 зажим частоты закрыт подъёмом режима и сбросом потолка,
	 * поэтому штатный YouTube обрабатывается как все плееры. На Android 10/11
	 * (вендорный SystemControl) холодный старт 60-фпс ролика под 50 Гц остаётся
	 * зажатым — живой замер 2026-10-18: 49.81 к/с, — а сброс потолка там стоит
	 * 1.5-2.5 с заминки, поэтому плеер по умолчанию остаётся в списке.
	 *
	 * Явно заданный ключ не трогаем, в том числе пустой `switch.skip.pkg=`
	 * (пустой список — законное значение: «исключений нет»). */
	if (!cfg_get (g_cfg, "switch.skip.pkg"))
	{
		if (g_plat.kind == PLAT_ATV14)
		{
			strlist_free (&g_skip_pkgs);
			trace (1, "switch.skip.pkg не задан — на этом пути список пуст "
			       "(зажим закрыт bump и сбросом потолка)\n");
		}
		else if (g_plat.kind == PLAT_ATV11)
		{
			strlist_free (&g_skip_pkgs);
			strlist_add (&g_skip_pkgs, "com.google.android.youtube.tv");
			trace (1, "switch.skip.pkg не задан — на этом пути исключаю штатный YouTube: "
			       "холодный старт 60-фпс ролика под 50 Гц остаётся зажатым\n");
		}
	}

	/* switch.delay.off на новом пути значит другое (предел терпения к паузе,
	 * см. pause.h) и имеет своё умолчание: конфиг читается в afrd_init до
	 * определения платформы, поэтому уточняем значение здесь. Явно заданный
	 * ключ не трогаем — на этой ветке важно лишь то, чего в файле нет. */
	if ((g_plat.kind != PLAT_LEGACY) && !cfg_get (g_cfg, "switch.delay.off"))
	{
		g_switch_delay_off = DEFAULT_SWITCH_DELAY_OFF_ATV14;
		trace (1, "switch.delay.off не задан — на новом пути беру умолчание %d мс "
		       "(предел удержания режима на паузе)\n", g_switch_delay_off);
	}

	/* Быстрое решение по заявленной частоте кадра: умолчание зависит от пути.
	 * На Android 10/11 включено (так решает штатный системный AFR — отсюда его
	 * 2–4 с), на ATV14 выключено: путь проверен живьём, а быстрое решение
	 * меняет поведение, поэтому включается осознанно. Явно заданный ключ не
	 * трогаем — важно лишь то, чего в файле нет. */
	{
		int def = (g_plat.kind == PLAT_ATV11) ? DEFAULT_FPS_DECLARE_FAST_ATV11
		                                      : DEFAULT_FPS_DECLARE_FAST_ATV14;
		if (cfg_get (g_cfg, "fps.declare.fast"))
			g_fps_declare_fast = cfg_get_int ("fps.declare.fast", def);
		else
		{
			g_fps_declare_fast = def;
			if (def)
				trace (1, "fps.declare.fast не задан — на этом пути беру %d "
				       "(быстрое решение по заявленной частоте)\n", def);
		}
	}

	/* Подъём режима до максимума при старте видео: на Android 11+ это
	 * единственный способ не дать плееру зафиксировать низкий «потолок»
	 * частоты (журнал §47.6). На старом пути зажима нет — там ключ не
	 * действует (CFGKEY_ATV14), умолчание 0. */
	g_switch_bump = cfg_get_int ("switch.bump", (g_plat.kind == PLAT_LEGACY) ? 0 : 1);

	/* Кому применяются механики зажима (switch.bump.pkg). Умолчание — штатный
	 * YouTube: только у него зажим измерен (журнал §28.9, §46.6, §47.2). У
	 * остальных плееров он не подтверждён, а сброс потолка — смена
	 * конфигурации экрана, на которой часть плееров перезапускает Activity
	 * (живой случай: OTT Navigator возвращался к предыдущему каналу).
	 * Явно заданный список не трогаем, в том числе пустой — «никому». */
	if ((g_plat.kind != PLAT_LEGACY) && !cfg_get (g_cfg, "switch.bump.pkg"))
	{
		strlist_free (&g_bump_pkgs);
		strlist_add (&g_bump_pkgs, "com.google.android.youtube.tv");
		trace (1, "switch.bump.pkg не задан — механики зажима только для штатного YouTube\n");
	}

	/* Подъём режима по выходу плеера на передний план: на Android 10/11 это
	 * единственный способ успеть до того, как плеер запомнит частоту. */
	g_switch_bump_on_app = cfg_get_int ("switch.bump.on.app",
	                                    (g_plat.kind == PLAT_LEGACY) ? 0 : 1);

	/* Сброс «потолка» частоты у плеера. Умолчание зависит от приёмника:
	 * на backdoor-пути (Android 12-14) смена плотности стоит ~80 мс подачи,
	 * на вендорном (Android 10/11) — 1.5-2.5 с, поэтому там выключено.
	 * На backdoor-пути умолчание следует за switch.bump: в интерфейсе это один
	 * пункт «Устранять зажим частоты», и выключение тумблера должно снимать
	 * обе механики, а не оставлять половину (журнал §47.9). */
	g_ceiling_reset = cfg_get_int ("switch.ceiling.reset",
	                               (g_plat.sink == SINK_SF_BACKDOOR) ? g_switch_bump : 0);

	/* «Громкие ключи»: настройка, которая на выбранном пути не действует,
	 * должна быть названа в логе, а не молча проигнорирована (именно так
	 * потерялось удвоение частоты, mode.prefer.exact). */
	cfgkeys_audit (g_config,
	               (g_plat.kind == PLAT_LEGACY) ? CFGKEY_LEGACY : CFGKEY_ATV14,
	               cfg_audit_warn, NULL);

	if (g_plat.kind != PLAT_LEGACY)
	{
		/* mtime нужен и ATV14-циклу: по нему ловится правка файла конфига
		 * (legacy-ветка ставит его ниже, в своём блоке). */
		g_config_mtime = mtime (g_config);
		return run_atv14 (&g_plat);
	}

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

	while (!g_shutdown) {
		/* Признак жизни для сторожа приложения (R57). Раньше штамп обновлялся
		 * только в atv14_wait(), поэтому на старом пути сторож не мог
		 * отличить живой демон от мёртвого (живая находка 2026-10-09 на
		 * стенде .76: после kill -9 демон не поднимался, а GUI показывал
		 * «Демон включён: ДА» по статусу мёртвого процесса).
		 * Цикл просыпается не реже, чем раз в CONFIG_CHECK_PERIOD (5 с) —
		 * это заметно чаще порога сторожа (4 * 3 с). */
		stats_heartbeat ();

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
				if (config_mtime_changed (cmt, g_config_mtime)) {
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
	/* legacy: взведённый таймер + mtime=-1 заставляют legacy-цикл вернуть 1
	 * не позднее периода проверки. ATV14-цикл таймер g_ost_config не
	 * обслуживает, поэтому запрос дублируется явным флагом: по API он
	 * срабатывает на ближайшей итерации, не ожидая периода mtime
	 * (см. atv14_reconf_pending). */
	g_reconf_requested = true;
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

/* mode.min.hz.list: «<пакет>=<частота Гц>», например
 * `com.google.android.youtube.tv=50` — для этого плеера режимы ниже 50 Гц не
 * выбираются (он «зажимает» темп отрисовки, если ролик стартовал под низким
 * режимом). Формат — как у fps.correct.list. */
static void mode_min_hz_load (const char *kw)
{
	g_mode_min_hz_count = 0;

	const char *str = cfg_get_str (kw, NULL);
	if (!str)
		return;

	trace (1, "\tloading minimal mode rates per package\n");

	char *tmp = strdup (str);
	char *cur = tmp;
	while (*cur) {
		cur += strspn (cur, spaces);
		char *next = cur + strcspn (cur, spaces);
		if (*next)
			*next++ = 0;

		char *eq = strchr (cur, '=');
		if (eq && cur [0]) {
			*eq++ = 0;
			float hz;
			if ((sscanf (eq, "%f", &hz) == 1) && (hz >= 1) && (hz <= 1000) &&
			    (g_mode_min_hz_count < ARRAY_SIZE (g_mode_min_hz))) {
				snprintf (g_mode_min_hz [g_mode_min_hz_count].pkg,
				          sizeof (g_mode_min_hz [0].pkg), "%s", cur);
				g_mode_min_hz [g_mode_min_hz_count].hz = (int) (256.0 * hz + 0.5);
				trace (2, "\t+ %s: " HZ_FMT " Гц или выше\n",
				       g_mode_min_hz [g_mode_min_hz_count].pkg,
				       HZ_ARGS (g_mode_min_hz [g_mode_min_hz_count].hz));
				g_mode_min_hz_count++;
			}
		}

		cur = next;
	}

	free (tmp);
}

/* Минимальная частота режима (24.8) для пакета; 0 — ограничения нет. */
static int mode_min_hz_for (const char *pkg)
{
	if (!pkg || !pkg [0])
		return 0;

	for (int i = 0; i < g_mode_min_hz_count; i++)
		if (!strcmp (g_mode_min_hz [i].pkg, pkg))
			return g_mode_min_hz [i].hz;

	return 0;
}

int afrd_init ()
{
	/* load config if not loaded already */
	if (!g_cfg && (load_config (g_config) != 0))
		return -1;

	/* «Записка» прошлого экземпляра читается ДО shmem_init (false), который
	 * затирает файл. Нужна она на случай, когда демона убили с удержанным
	 * режимом: вернуть режим больше некому — SurfaceFlinger сам его не
	 * сбрасывает, а флаг backdoor система гасит за секунды, так что по флагу
	 * удержание уже не опознать (живая проверка 2026-10-08). Разбор — в
	 * atv14_recover_stale (там уже включён журнал). */
	g_prev_stats_valid = shmem_peek (&g_prev_stats);
	shmem_init (false);
	androp_init ();

	int log_enable = (cfg_get_int ("log.enable", 1) != 0);
	const char *log_file = cfg_get_str ("log.file", NULL);
	if (log_file && log_enable)
		trace_log (log_file);

	trace (1, "AFRd2 v%s%s built at %s is initializing\n", g_version, g_ver_sfx, g_bdate);

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
	/* Путь к вендорному помощнику смены режима (Android 10/11). Пишут его
	 * приложение и Magisk-модуль, раскладывая помощник в свои каталоги. */
	g_vendor_disp_cli = cfg_get_str ("vendor.disp.cli", DEFAULT_VENDOR_DISP_CLI);
	g_vendor_disp_persist = cfg_get_int ("vendor.disp.persist", 1);
	g_mode_prefer_exact = cfg_get_int ("mode.prefer.exact", DEFAULT_MODE_PREFER_EXACT);
	g_mode_use_fract = cfg_get_int ("mode.use.fract", DEFAULT_MODE_USE_FRACT);
	blacklist_rates_load ("mode.blacklist.rates");
	mode_min_hz_load ("mode.min.hz.list");
	strlist_load (&g_skip_pkgs, "switch.skip.pkg", "packages: no mode switching");
	strlist_load (&g_bump_pkgs, "switch.bump.pkg", "packages: clamp fix");

	trace (1, "\trefresh rate selection: use fractional %d, exact %d\n",
		g_mode_use_fract, g_mode_prefer_exact);

	g_switch_delay_on = cfg_get_int ("switch.delay.on", DEFAULT_SWITCH_DELAY_ON);
	/* switch.delay.off: на старом пути — задержка возврата режима после
	 * события «видео кончилось»; на новом — предел удержания режима на паузе
	 * (см. pause.h). Здесь ключ читается до определения платформы, поэтому
	 * умолчание нового пути подставляется позже, в afrd_run. */
	g_switch_delay_off = cfg_get_int ("switch.delay.off", DEFAULT_SWITCH_DELAY_OFF);
	g_switch_delay_retry = cfg_get_int ("switch.delay.retry", DEFAULT_SWITCH_DELAY_RETRY);
	g_switch_timeout = cfg_get_int ("switch.timeout", DEFAULT_SWITCH_TIMEOUT);

	/* Предел времени на внешние команды задан умолчанием exec.c (10 с) и НЕ
	 * берётся из switch.timeout: у того другой смысл (сколько молчание
	 * vdec_status считать залипанием), и на 3 с в дедлайн упирались бы честные
	 * команды нового пути — dumpsys SurfaceFlinger под нагрузкой. Ноль в
	 * дедлайне обнулил бы таблицу режимов и оставил ATV14 без переключения,
	 * то есть страховка от зависания превратилась бы в источник отказов.
	 * Дедлайн здесь только антизависательный: раньше команда могла встать
	 * навсегда и остановить цикл. */
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
	/* Освобождение намеренное (переинициализация), поэтому без сообщения
	 * «HDMI not active» — см. modes_release(). */
	modes_release ();
	apisock_fini ();
	colorspace_fini ();
	/* Постоянный помощник режима: его процесс держит HIDL-вызовы, поэтому при
	 * переинициализации и выходе его надо снять — иначе останется сирота с
	 * загруженной VM (журнал §49.9). */
	dispserve_stop ();

	if (g_uevent_sock != -1) {
		close (g_uevent_sock);
		g_uevent_sock = -1;
	}

	uevent_filter_fini (&g_filter_frhint);
	uevent_filter_fini (&g_filter_vdec);
	uevent_filter_fini (&g_filter_hdmi);
	uevent_filter_fini (&g_filter_hdcp);
	/* оба списка: g_vdec_blacklist и g_frhint_vdec_blacklist загружаются
	 * в afrd_init() заново на каждой перезагрузке конфига */
	strlist_free (&g_vdec_blacklist);
	strlist_free (&g_frhint_vdec_blacklist);
	strlist_free (&g_skip_pkgs);
	strlist_free (&g_bump_pkgs);
	g_mode_blacklist_rates_count = 0;

	g_enable = false;
	g_hdmi_dev = NULL;
	g_hdmi_state = NULL;
	g_mode_path = NULL;
	g_vdec_sysfs = NULL;

	/* g_plat хранит указатели ВНУТРЬ образа конфига (disp_cap, vendor_mode_cli),
	 * а cfg_free ниже этот образ освобождает. Без сброса следующая инициализация
	 * успевала воспользоваться ими до platform_detect (afrd_init →
	 * handle_hdmi_switch → modes_refresh_platform) и читала список режимов по
	 * адресу освобождённой строки: в журнал попадал разбор чужого файла вместо
	 * списка режимов (живая находка приёмки 09.10 на стенде .45 — сначала файл
	 * помощника, затем dc_cap). Нулевая структура даёт ровно тот же путь, что и
	 * на первом старте: legacy-ветка читает список из g_hdmi_dev, а afrd_run
	 * перечитывает его по ключу платформы.
	 *
	 * Там же сбрасываем состояние ATV14-цикла: оно относится к экземпляру, а не
	 * к конфигу, и после перезагрузки конфига публиковалось в API/GUI как
	 * свежее (те же сутки: enabled и частоты от прошлого экземпляра). */
	memset (&g_plat, 0, sizeof (g_plat));
	memset (&g_atv14_state, 0, sizeof (g_atv14_state));

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
