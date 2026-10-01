#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sfmodes.h"

/* Допуск по частоте при поиске режима: 0.125 Гц в fixed-point 24.8.
 * Частота нужного режима должна совпасть, иначе это не наш режим
 * (например, 50 Гц в списке есть только для 1080p). */
#define SF_HZ_TOL	32

/* Правила выбора кратного режима (sfmodes_select): допуск ошибки частоты
 * 11/256 = 4.3 % и максимальная кратность 3 — как в legacy-рейтинге режимов. */
#define SF_RATE_TOL	11
#define SF_MULT_MAX	3

static int parse_line (const char *line, sf_mode_t *out)
{
	/* формат: "{id=6, hwcId=6, resolution=1920x1080, refreshRate=23.98 Hz, ...}" */
	int id, w, h;
	float hz;
	if (sscanf (line, "{id=%d, hwcId=%*d, resolution=%dx%d, refreshRate=%f",
	            &id, &w, &h, &hz) != 4)
		return 0;

	out->id = id;
	out->width = w;
	out->height = h;
	out->hz = (int)(hz * 256.0 + 0.5);
	return 1;
}

int sfmodes_parse (const char *dump, sf_mode_t *out, int max)
{
	int n = 0;
	const char *cur = dump;

	while (cur && *cur && n < max)
	{
		const char *eol = strchr (cur, '\n');
		const char *brace = strchr (cur, '{');
		if (!eol)
			eol = cur + strlen (cur);

		if (brace && brace < eol && parse_line (brace, &out [n]))
			n++;

		cur = (*eol) ? eol + 1 : eol;
	}

	return n;
}

int sfmodes_find (const sf_mode_t *modes, int n, int hz, int width, int height)
{
	int best = -1, best_d = 1 << 30;

	for (int i = 0; i < n; i++)
	{
		if (modes [i].width != width || modes [i].height != height)
			continue;

		int d = modes [i].hz > hz ? modes [i].hz - hz : hz - modes [i].hz;
		if (d < best_d)
		{
			best_d = d;
			best = modes [i].id;
		}
	}

	return (best_d <= SF_HZ_TOL) ? best : -1;
}

int sfmodes_find_hz (const sf_mode_t *modes, int n, int hz)
{
	int best = -1, best_d = 1 << 30;

	for (int i = 0; i < n; i++)
	{
		int d = modes [i].hz > hz ? modes [i].hz - hz : hz - modes [i].hz;
		if (d < best_d)
		{
			best_d = d;
			best = modes [i].id;
		}
	}

	/* допуск 0.125 Гц: частота активного режима должна совпасть точно,
	 * иначе это не наш режим (например, 100 Гц в списке отсутствует) */
	return (best_d <= SF_HZ_TOL) ? best : -1;
}

int sfmodes_select (const sf_mode_t *modes, int n, int hz, int width, int height,
                    int prefer_exact, sf_hz_filter_t allowed, void *ctx,
                    sf_choice_t *out)
{
	/* Те же правила, что в legacy-рейтинге (afrd.c): допуск ошибки частоты
	 * 11/256 = 4.3 %, кратность не выше 3 (rate_n > 3 даёт тот же бонус). */
	if (hz <= 0)
		return -1;

	/* Как в legacy: начальный рейтинг 0, поэтому кандидат с нулевым рейтингом
	 * (ошибка частоты на самой границе допуска) не выбирается вовсе. */
	int best_id = -1, best_rating = 0, best_dist = 0;
	int best_rate_n = 1, best_hz = 0;

	for (int i = 0; i < n; i++)
	{
		const sf_mode_t *mode = &modes [i];
		if ((mode->width != width) || (mode->height != height))
			continue;
		if (allowed && !allowed (mode->hz, ctx))
			continue;

		/* кратность mode_hz / hz в единицах 1/256; шаг прекращается, когда
		 * отношение опустилось до 1.5 (как в legacy) */
		unsigned rate_n = 1;
		unsigned rate = (unsigned)mode->hz * 256 / (unsigned)hz;
		while ((rate > 0x180) && (rate_n < 32))
		{
			rate_n++;
			rate = (unsigned)mode->hz * 256 / ((unsigned)hz * rate_n);
		}
		if (rate > 0x180)	/* отношение слишком велико — не наш режим */
			continue;

		unsigned delta = (rate > 0x100) ? (rate - 0x100) : (0x100 - rate);
		if (delta > SF_RATE_TOL)
			continue;

		unsigned nx = (rate_n > SF_MULT_MAX) ? SF_MULT_MAX : (rate_n - 1);
		int rating = (SF_RATE_TOL - (int)delta) * 16 +
		             4 * (prefer_exact ? (SF_MULT_MAX - (int)nx) : (int)nx);

		/* вторичный критерий: ближайшая частота (выбор 23.98 против 24.00) */
		int dist = abs (mode->hz / (int)rate_n - hz);

		if ((rating > best_rating) || ((rating == best_rating) && (dist < best_dist)))
		{
			best_rating = rating;
			best_dist = dist;
			best_id = mode->id;
			best_rate_n = (int)rate_n;
			best_hz = mode->hz;
		}
	}

	if (best_id < 0)
		return -1;

	if (out)
	{
		out->id = best_id;
		out->hz = best_hz;
		out->rate_n = best_rate_n;
	}

	return 0;
}

int sfmodes_current_hz (const sf_mode_t *modes, int n, int id)
{
	for (int i = 0; i < n; i++)
		if (modes [i].id == id)
			return modes [i].hz;

	return 0;
}
