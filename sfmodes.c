#include <stdio.h>
#include <string.h>
#include "sfmodes.h"

/* Допуск по частоте при поиске режима: 0.125 Гц в fixed-point 24.8.
 * Частота нужного режима должна совпасть, иначе это не наш режим
 * (например, 50 Гц в списке есть только для 1080p). */
#define SF_HZ_TOL	32

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

int sfmodes_current_hz (const sf_mode_t *modes, int n, int id)
{
	for (int i = 0; i < n; i++)
		if (modes [i].id == id)
			return modes [i].hz;

	return 0;
}
