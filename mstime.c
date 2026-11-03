/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * Millisecond timers
 */

#include "mstime.h"
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

mstime_t g_mstime;

mstime_t mstime_get ()
{
	struct timeval tv;
	gettimeofday (&tv, NULL);
	return tv.tv_sec * 1000 + (tv.tv_usec / 1000);
}

long long mstime_mono (void)
{
	struct timespec ts;
	if (clock_gettime (CLOCK_MONOTONIC, &ts) != 0)
		return 0;

	return (long long) ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

int mstime_jump (int wall_delta, long long mono_delta)
{
	/* Скачок = насколько настенные часы ушли от реального времени за ту же
	 * итерацию. Ограничиваем int: mstime_t всё равно 32-битный. */
	long long jump = (long long) wall_delta - mono_delta;

	if (jump > 2147483647LL)
		return 2147483647;
	if (jump < -2147483647LL)
		return -2147483647;

	return (int) jump;
}
