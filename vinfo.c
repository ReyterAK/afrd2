/*
 * AFRd2: фактическая частота дисплея из /sys/class/display/vinfo.
 *
 * Нужна как независимая проверка после смены режима: имя режима одно на пару
 * 23.976/24.000, а реальную частоту задают тайминги (фреймворк на ATV14,
 * frac_rate_policy на вендорном пути). Поле meas_frame_rate в том же узле
 * бесполезно — оно нулевое (проверено на S905X5M, журнал §4), а
 * sync_duration_num/den отдаёт точную дробь: 23976/1000.
 */

#include <stdlib.h>
#include <string.h>

#include "afrd.h"
#include "vinfo.h"

/* Найти число после "ключ:" в начале строки. Строки в vinfo выровнены
 * пробелами ("    sync_duration_num:     23976"), поэтому допускаем любое
 * количество пробелов до и после двоеточия. */
static int field_int (const char *dump, const char *key, int *out)
{
	size_t klen = strlen (key);
	const char *cur = dump;

	while (cur && *cur)
	{
		const char *eol = strchr (cur, '\n');
		const char *p = cur;

		while ((*p == ' ') || (*p == '\t'))
			p++;

		if (!strncmp (p, key, klen))
		{
			p += klen;
			while ((*p == ' ') || (*p == '\t'))
				p++;
			if (*p == ':')
			{
				p++;
				while ((*p == ' ') || (*p == '\t'))
					p++;

				char *end = NULL;
				long v = strtol (p, &end, 10);
				if ((end == p) || (v < 0) || (v > 1000000))
					return 0;

				*out = (int)v;
				return 1;
			}
		}

		if (!eol)
			break;
		cur = eol + 1;
	}

	return 0;
}

int vinfo_parse_sync (const char *dump, int *num, int *den)
{
	int n = 0, d = 0;

	if (!dump)
		return 0;
	if (!field_int (dump, "sync_duration_num", &n))
		return 0;
	if (!field_int (dump, "sync_duration_den", &d))
		return 0;
	if ((n <= 0) || (d <= 0))
		return 0;

	if (num)
		*num = n;
	if (den)
		*den = d;
	return 1;
}

int vinfo_hz (int num, int den)
{
	if ((num <= 0) || (den <= 0))
		return 0;

	return (int)(((double)num * 256.0 / (double)den) + 0.5);
}

int vinfo_sync_hz (const char *path, int *num, int *den)
{
	if (num)
		*num = 0;
	if (den)
		*den = 0;

	char *dump = sysfs_read (path);
	if (!dump)
		return 0;

	int n = 0, d = 0;
	int ok = vinfo_parse_sync (dump, &n, &d);
	free (dump);

	if (!ok)
		return 0;

	if (num)
		*num = n;
	if (den)
		*den = d;
	return vinfo_hz (n, d);
}
