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

/* --- перезапуск кодека как второй способ сброса потолка (журнал §60) --------
 *
 * Декодер живёт в HAL-процессе (Codec2 на Android 12+, OMX на 8-11), а не в
 * приложении. Перезапуск этого процесса пересоздаёт сессию декодера у плеера:
 * плеер получает ошибку/новый кодек, перечитывает режим дисплея и подача
 * выправляется — с текущей позиции ролика. Замер на ATV14: декодер пропадает
 * на 68 мс, возвращается на 1.1 с (смена плотности — 6.8 с).
 *
 * Имя сервиса зависит от версии Android и прошивки, поэтому оно не
 * зашивается, а опрашивается: init отдаёт состояние в init.svc.<имя>.
 * Порядок списка — приоритет: сначала Codec2 (Android 12+), затем OMX
 * (Android 8-11).
 */
typedef struct
{
	const char *svc;	/* имя сервиса init */
	const char *bin;	/* чем процесс сервиса виден в `ps` (для проверки
				 * перезапуска там, где нет init.svc_debug_pid) */
} ceiling_codec_t;

static const ceiling_codec_t ceiling_codec_services [] =
{
	{ "android-hardware-media-c2-hal-1-2", "media.c2@1.2" },
	{ "android-hardware-media-c2-hal-1-1", "media.c2@1.1" },
	{ "android-hardware-media-c2-hal-1-0", "media.c2@1.0" },
	{ "vendor.media.omx",                  "media.omx" },
	{ "media.omx",                         "media.omx" },
	{ NULL, NULL }
};

int ceiling_codec_probe_cmd (char *buf, size_t n)
{
	if (!buf || (n == 0))
		return -1;

	size_t used = 0;
	int r = snprintf (buf, n, "for s in");
	if (r < 0 || ((size_t) r >= n))
		return -1;
	used = (size_t) r;

	for (int i = 0; ceiling_codec_services [i].svc; i++)
	{
		r = snprintf (buf + used, n - used, " %s", ceiling_codec_services [i].svc);
		if (r < 0 || ((size_t) r >= n - used))
			return -1;
		used += (size_t) r;
	}

	r = snprintf (buf + used, n - used,
	              "; do echo \"$s=$(getprop init.svc.$s)\"; done");
	if (r < 0 || ((size_t) r >= n - used))
		return -1;

	return 0;
}

const char *ceiling_codec_pick (const char *probe_out)
{
	if (!probe_out)
		return NULL;

	/* Вывод опроса — строки «<имя>=running» (getprop пустого свойства печатает
	 * пустую строку, поэтому «=running» без имени тоже возможно). Выбираем
	 * первый кандидат в порядке приоритета, а не первый по порядку строк:
	 * порядок строк задаёт shell, и полагаться на него незачем. */
	for (int i = 0; ceiling_codec_services [i].svc; i++)
	{
		const char *name = ceiling_codec_services [i].svc;
		size_t len = strlen (name);
		const char *p = probe_out;

		while (*p)
		{
			const char *eol = strchr (p, '\n');
			size_t line_len = eol ? (size_t)(eol - p) : strlen (p);

			if ((line_len > len + 1) &&
			    (strncmp (p, name, len) == 0) &&
			    (p [len] == '=') &&
			    (strncmp (p + len + 1, "running", 7) == 0))
				return name;

			if (!eol)
				break;
			p = eol + 1;
		}
	}

	return NULL;
}

int ceiling_build_codec_restart (char *buf, size_t n, const char *svc)
{
	if (!buf || (n == 0) || !svc || !*svc)
		return -1;

	/* Ищем запись: по имени сервиса нужен ещё и признак процесса. */
	const char *bin = NULL;
	for (int i = 0; ceiling_codec_services [i].svc; i++)
		if (strcmp (ceiling_codec_services [i].svc, svc) == 0)
		{
			bin = ceiling_codec_services [i].bin;
			break;
		}

	if (!bin)
		return -1;

	/* Признак успеха — НОВЫЙ pid: `setprop ctl.restart` только выставляет
	 * свойство, а init может и проигнорировать его (нет такого сервиса, нет
	 * прав), вернув 0. Свойства init.svc_debug_pid.* есть на Android 10+;
	 * на 8-9 их нет, хотя перезапуск работает (живой замер на ATV9:
	 * новый pid через 126 мс при state=running всё время, §62.7). Поэтому
	 * запасной признак — смена списка pid процессов, в аргументах которых
	 * виден бинарник сервиса.
	 * Ожидание ограничено, чтобы зависший HAL не держал цикл демона. */
	int r = snprintf (buf, n,
	                  "l() { ps -A -o PID,ARGS 2>/dev/null | grep \"%s\" | grep -v grep "
	                  "| awk '{print $1}' | tr '\\n' ','; }; "
	                  "p0=$(getprop init.svc_debug_pid.%s); b=$(l); setprop ctl.restart %s; "
	                  "i=0; while [ $i -lt 12 ]; do sleep 0.25; "
	                  "p=$(getprop init.svc_debug_pid.%s); "
	                  "if [ -n \"$p\" ] && [ \"$p\" != \"$p0\" ]; then echo \"pid $p\"; exit 0; fi; "
	                  "a=$(l); "
	                  "if [ -z \"$p0\" ] && [ -n \"$a\" ] && [ \"$a\" != \"$b\" ]; then "
	                  "echo \"pid сменились: $b -> $a\"; exit 0; fi; "
	                  "i=$((i+1)); done; echo \"перезапуск не подтверждён\"; exit 1",
	                  bin, svc, svc, svc);

	return ((r > 0) && ((size_t) r < n)) ? 0 : -1;
}
