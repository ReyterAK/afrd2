/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * Сброс «потолка» частоты у плеера: разбор `wm density` и сборка команды.
 * См. ceiling.h и журнал §47.9.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "ceiling.h"

/* Значение после подписи в строке вида "Physical density: 320". */
static int parse_value (const char *line, const char *label, int *out)
{
	const char *p = strstr (line, label);
	if (!p)
		return -1;

	p += strlen (label);
	while (*p && !isdigit ((unsigned char) *p))
		p++;

	if (!*p)
		return -1;

	char *end = NULL;
	long v = strtol (p, &end, 10);
	if ((end == p) || (v <= 0) || (v > 10000))
		return -1;

	*out = (int) v;
	return 0;
}

int ceiling_density_parse (const char *wm_out, int *cur, int *had_override, int *next)
{
	if (!wm_out || !cur || !had_override || !next)
		return -1;

	int phys = -1, over = -1;
	const char *line = wm_out;

	/* Вывод утилиты `wm density` многострочный; читаем построчно, чтобы
	 * подпись искалась в одной строке (иначе «Physical density» могла бы
	 * склеиться с чужим числом). */
	while (*line)
	{
		const char *eol = strchr (line, '\n');
		size_t len = eol ? (size_t)(eol - line) : strlen (line);

		char buf [128];
		size_t n = (len < sizeof (buf) - 1) ? len : sizeof (buf) - 1;
		memcpy (buf, line, n);
		buf [n] = 0;

		int v;
		if ((over < 0) && (parse_value (buf, "Override density", &v) == 0))
			over = v;
		else if ((phys < 0) && (parse_value (buf, "Physical density", &v) == 0))
			phys = v;

		if (!eol)
			break;
		line = eol + 1;
	}

	int base = (over > 0) ? over : phys;
	if (base <= 0)
		return -1;

	*cur = base;
	*had_override = (over > 0) ? 1 : 0;
	*next = base + CEILING_DENSITY_STEP;
	return 0;
}

int ceiling_build_restore (char *buf, size_t n, int had_override, int prev)
{
	if (!buf || (n == 0))
		return -1;

	int r;
	if (had_override && (prev > 0))
		r = snprintf (buf, n, "wm density %d", prev);
	else
		r = snprintf (buf, n, "wm density reset");

	return ((r > 0) && ((size_t) r < n)) ? 0 : -1;
}

int ceiling_should_reset (int confirmed_hz, int target_hz)
{
	if (target_hz <= 0)
		return 0;

	if (confirmed_hz <= 0)
		return 1;

	return (target_hz > confirmed_hz + CEILING_HZ_TOLERANCE) ? 1 : 0;
}
