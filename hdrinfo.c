/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * Атрибуты дисплея вендорского сервиса (Android 10-14): HDR/SDR-режимы.
 * Только чтение: значения применяют система и плеер, демон их показывает
 * в журнале и в ответе API `status` (политику HDR не трогает).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "afrd.h"
#include "exec.h"
#include "hdrinfo.h"

static hdrinfo_t g_hdrinfo;

/* Значение атрибута в выводе `-r dumpDisplayAttribute` выглядит так:
 *
 *	"HDR Mode" :
 *	{
 *		"id" : 11,
 *		"value" : "2"
 *	},
 *
 * Значение может занимать несколько строк (так отдаётся HDR CAP), поэтому
 * берём ровно то, что стоит между кавычками после "value". Ключ ищется
 * вместе с кавычками: так "HDR Mode" не совпадает с "FORCE HDR Mode",
 * а "HDR Policy" — с "Dolby Vision HDR 10 Policy". */
static bool find_attr (const char *text, const char *key, char *buf, size_t buf_sz)
{
	char quoted [64];
	if (snprintf (quoted, sizeof (quoted), "\"%s\"", key) >= (int) sizeof (quoted))
		return false;

	const char *p = strstr (text, quoted);
	if (!p)
		return false;

	/* от ключа ищем слово value, затем пару кавычек со значением */
	p = strstr (p, "\"value\"");
	if (!p)
		return false;

	const char *q = strchr (p + 1, '"');	/* закрывающая кавычка слова value */
	if (!q)
		return false;

	const char *v1 = strchr (q + 1, '"');	/* открывающая кавычка значения */
	if (!v1)
		return false;

	const char *v2 = strchr (v1 + 1, '"');	/* закрывающая кавычка значения */
	if (!v2)
		return false;

	size_t n = (size_t) (v2 - v1 - 1);
	if (n >= buf_sz)
		n = buf_sz - 1;

	memcpy (buf, v1 + 1, n);
	buf [n] = 0;
	return true;
}

/* Числовое значение атрибута; пустое/нечисловое — не разобрано. */
static bool attr_int (const char *text, const char *key, int *out)
{
	char buf [32];
	if (!find_attr (text, key, buf, sizeof (buf)))
		return false;

	char *end = NULL;
	long v = strtol (buf, &end, 10);
	if (end == buf)
		return false;

	while ((*end == ' ') || (*end == '\t') || (*end == '\r') || (*end == '\n'))
		end++;

	if (*end)
		return false;

	*out = (int) v;
	return true;
}

bool hdrinfo_parse (const char *text, hdrinfo_t *info)
{
	memset (info, 0, sizeof (*info));

	if (!text || !text [0])
		return false;

	bool any = false;

	if (attr_int (text, "HDR Mode", &info->mode))
		any = true;
	if (attr_int (text, "HDR Policy", &info->policy))
		any = true;
	if (attr_int (text, "HDR Priority", &info->priority))
		any = true;
	if (attr_int (text, "SDR Mode", &info->sdr_mode))
		any = true;
	if (attr_int (text, "FORCE HDR Mode", &info->force_hdr))
		any = true;

	info->valid = any;
	return any;
}

bool hdrinfo_refresh (const char *cli)
{
	hdrinfo_t info;
	memset (&info, 0, sizeof (info));

	if (cli && cli [0])
	{
		char cmd [EXEC_CMD_MAX];
		/* запас по размеру: полный дамп атрибутов — около 2.5 КБ, но
		 * HDR CAP отдаётся многострочным блоком */
		char out [8192];

		if (snprintf (cmd, sizeof (cmd), "%s -r dumpDisplayAttribute", cli) < (int) sizeof (cmd))
			if (exec_capture (cmd, out, sizeof (out)) == 0)
				hdrinfo_parse (out, &info);
	}

	g_hdrinfo = info;
	return info.valid;
}

const hdrinfo_t *hdrinfo_get (void)
{
	return &g_hdrinfo;
}

void hdrinfo_log (const char *prefix)
{
	const hdrinfo_t *hi = &g_hdrinfo;

	if (!hi->valid)
	{
		trace (1, "%s: атрибуты дисплея недоступны\n", prefix);
		return;
	}

	trace (1, "%s: HDR Mode=%d, HDR Policy=%d, HDR Priority=%d, SDR Mode=%d, FORCE HDR Mode=%d\n",
	       prefix, hi->mode, hi->policy, hi->priority, hi->sdr_mode, hi->force_hdr);
}
