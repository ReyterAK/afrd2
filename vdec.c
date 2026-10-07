/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * Чтение состояния декодера из /sys/class/vdec/vdec_status.
 *
 * vdec_status печатает один блок на канал. Штатно канал один; два и более
 * появляются в переходные моменты и при «залипшем» инстансе декодера
 * (журнал, §18.1/§18.4): старый блок идёт первым, новый — последним. Раньше
 * парсер брал первый "device name"/"frame count", поэтому залипший канал
 * ослеплял демон: он читал замороженный счётчик и считал, что видео стоит.
 * Теперь каналы разбираются все, а активный выбирает vdec_select() по
 * растущему счётчику (вариант 2, см. vdec.h).
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

static const char *skip_ws (const char *p)
{
	while (*p == ' ' || *p == '\t')
		p++;
	return p;
}

/* Найти подпись поля вида "   status : 6" и вернуть указатель сразу после
 * двоеточия. Подпись ищется как отдельное слово (перед ней — начало строки или
 * пробел), поэтому "status" не поймает, например, "VDEC_STATUS" в чужой строке. */
static const char *find_field (const char *start, const char *p, const char *end,
                               const char *label)
{
	size_t len = strlen (label);

	while (p && &p [0] < end)
	{
		p = strstr (p, label);
		if (!p || &p [0] >= end)
			return NULL;

		bool at_word_start = (p == start) || (p [-1] == ' ') ||
		                     (p [-1] == '\t') || (p [-1] == '\n');
		const char *after = p + len;
		const char *colon = skip_ws (after);
		if (at_word_start && (colon [0] == ':'))
			return skip_ws (colon + 1);

		p = after;
	}

	return NULL;
}

static bool parse_uint (const char *p, const char *end, unsigned *out)
{
	char buf [32];
	size_t i = 0;

	while (i + 1 < sizeof (buf) && p [i] && p [i] != '\n' && p [i] != '\r' &&
	       (!end || &p [i] < end))
	{
		buf [i] = p [i];
		i++;
	}
	buf [i] = 0;

	return sscanf (buf, "%u", out) == 1;
}

int vdec_parse_channels (const char *text, vdec_chan_t *out, int max)
{
	if (!text || max <= 0)
		return 0;

	int n = 0;
	const char *p = text;

	while (n < max)
	{
		p = strstr (p, "device name");
		if (!p)
			break;

		const char *next = strstr (p + 11, "device name");
		const char *end = next ? next : (text + strlen (text));

		vdec_chan_t ch;
		memset (&ch, 0, sizeof (ch));

		/* имя устройства (обрезается по концу строки и по концу блока) */
		const char *v = strchr (p, ':');
		if (v && v < end)
		{
			v = skip_ws (v + 1);
			size_t i = 0;
			while (i + 1 < sizeof (ch.device) && v [i] && v [i] != '\n' &&
			       v [i] != '\r' && (!next || &v [i] < next))
			{
				ch.device [i] = v [i];
				i++;
			}
			ch.device [i] = 0;
		}

		/* frame count — обязателен: без него канал не считаем (как и раньше) */
		const char *fc = strstr (p, "frame count");
		if (fc && fc < end && (fc = strchr (fc, ':')) && fc < end &&
		    sscanf (fc + 1, "%llu", &ch.frames) == 1)
		{
			/* frame dur — необязательное уточнение */
			const char *fd = strstr (p, "frame dur");
			if (fd && fd < end && (fd = strchr (fd, ':')))
			{
				unsigned dur = 0;
				if (parse_uint (fd + 1, end, &dur))
					ch.decl_dur = dur;
			}

			/* status — сырое поле драйвера (см. vdec.h): читаем и отдаём в
			 * журнал, решений по нему демон не принимает. Ищем подпись поля
			 * (выровнена пробелами), а не подстроку "status" где попало. */
			const char *st = find_field (text, p, end, "status");
			if (st)
			{
				unsigned sv = 0;
				if (parse_uint (st, end, &sv))
					ch.status = sv;
			}
			out [n++] = ch;
		}

		p += 11;
	}

	return n;
}

int vdec_select (const vdec_chan_t *chans, int n, vdec_sel_t *st)
{
	if (n <= 0)
	{
		st->have_prev = false;
		return -1;
	}

	int idx = -1;
	if (st->have_prev)
	{
		/* Из растущих берём самый свежий (последний): при старте нового
		 * видео старый инстанс может ещё отдавать кадры кратко, а нужен
		 * именно новый. Штатно растёт ровно один канал. */
		for (int i = 0; i < n; i++)
			if (chans [i].frames > st->prev_frames [i])
				idx = i;
	}

	/* Ни один счётчик не вырос (пауза или залипание) — последний блок,
	 * самый свежий инстанс. */
	if (idx < 0)
		idx = n - 1;

	for (int i = 0; i < VDEC_MAX_CHANNELS; i++)
		st->prev_frames [i] = (i < n) ? chans [i].frames : 0;
	st->have_prev = true;

	return idx;
}

int vdec_read (const char *path, vdec_status_t *out, vdec_sel_t *st)
{
	memset (out, 0, sizeof (*out));

	char *text = read_file (path);
	if (!text)
		return -1;

	vdec_chan_t chans [VDEC_MAX_CHANNELS];
	int n = vdec_parse_channels (text, chans, VDEC_MAX_CHANNELS);
	free (text);

	int idx = vdec_select (chans, n, st);
	if (idx < 0)
		return 0;		/* видео нет — active остаётся false */

	out->active = true;
	snprintf (out->device, sizeof (out->device), "%s", chans [idx].device);
	out->frames = chans [idx].frames;
	out->decl_dur = chans [idx].decl_dur;
	out->status = chans [idx].status;
	return 0;
}

int vdec_probe_active (const char *path, bool *active)
{
	char *text = read_file (path);
	if (!text)
	{
		*active = false;
		return -1;
	}

	vdec_chan_t chans [VDEC_MAX_CHANNELS];
	int n = vdec_parse_channels (text, chans, VDEC_MAX_CHANNELS);
	free (text);

	*active = (n > 0);
	return 0;
}
