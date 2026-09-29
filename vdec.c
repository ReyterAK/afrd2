/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * Чтение состояния декодера из /sys/class/vdec/vdec_status.
 *
 * Вывод парсится своей приватной read_file(), а не upstream sysfs_read():
 * upstream sysfs_read() тянет за собой afrd.h, а trace() определён в main.c
 * (на устройстве) и отсутствует в host-сборке тестов. Свой read_file()
 * держит vdec.c изолированным от main.c и позволяет тестировать парсер
 * без заглушек. Опасные диагностические dump-узлы не читаются принципиально.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#include "vdec.h"

unsigned long long now_us (void)
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}

static char *read_file (const char *path)
{
	int h = open (path, O_RDONLY);
	if (h < 0)
		return NULL;

	char tmp [4096];
	int n = read (h, tmp, sizeof (tmp) - 1);
	close (h);

	if (n < 0)
		return NULL;

	tmp [n] = 0;
	return strdup (tmp);
}

int vdec_parse (const char *text, vdec_status_t *out)
{
	memset (out, 0, sizeof (*out));

	if (!text || strstr (text, "No vdec."))
		return 0;

	const char *p = strstr (text, "device name");
	if (!p)
		return 0;			/* без имени устройства видео не считаем */

	p = strchr (p, ':');
	if (!p)
		return 0;
	p++;
	while (*p == ' ' || *p == '\t')
		p++;

	size_t i = 0;
	while (i + 1 < sizeof (out->device) && p [i] && p [i] != '\n' && p [i] != '\r')
	{
		out->device [i] = p [i];
		i++;
	}
	out->device [i] = 0;

	p = strstr (text, "frame count");
	if (!p)
		return 0;
	p = strchr (p, ':');
	if (!p)
		return 0;

	if (sscanf (p + 1, "%llu", &out->frames) != 1)
		return 0;

	out->active = true;
	return 0;
}

int vdec_read (const char *path, vdec_status_t *out)
{
	char *text = read_file (path);
	if (!text)
	{
		memset (out, 0, sizeof (*out));
		return -1;
	}

	vdec_parse (text, out);
	free (text);
	return 0;
}
