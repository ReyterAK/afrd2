/*
 * AFRd2: выбор режима по частоте и работа с частотой режима (24.8 fixed-point).
 *
 * Вынесено из modes.c/afrd.c: правила выбора нужны ДВУМ путям — старому
 * (Android 6-9, режим задаётся записью имени в /sys/class/display/mode) и
 * новому на Android 10/11 (режим задаётся вендорным SystemControl по имени,
 * журнал §33.18). Держать две копии рейтинга нельзя: они разойдутся.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "afrd.h"
#include "modes_sel.h"

int display_mode_hz (display_mode_t *mode)
{
	if (mode->fractional)
		switch (mode->framerate) {
			case  24: return ( 2997 * 256 + 62) / 125;
			case  30: return ( 2997 * 256 + 50) / 100;
			case  60: return ( 5994 * 256 + 50) / 100;
			case 120: return (11988 * 256 + 50) / 100;
			case 240: return (23976 * 256 + 50) / 100;
		}

	return mode->framerate * 256;
}

bool mode_rate_is_fract (int hz)
{
	/* Дробный представитель группы отличается от целого на 0.1 %, то есть
	 * младшие 8 бит (дробная часть 24.8) ненулевые: 6138 против 6144,
	 * 7672 против 7680. У 25/50/60 дробного варианта нет — там 0. */
	return (hz > 0) && ((hz & 255) != 0);
}

int display_mode_hz_actual (const display_mode_t *mode, bool fract)
{
	display_mode_t m = *mode;

	/* Политику прикладываем к копии: у режимов без дробного варианта
	 * display_mode_hz вернёт номинал сам (50 -> 50, 25 -> 25). */
	if (fract)
		m.fractional = true;

	return display_mode_hz (&m);
}

void display_mode_set_hz (display_mode_t *mode, int hz)
{
	mode->fractional = true;
	int hz_frac = display_mode_hz (mode);
	int hz_int = mode->framerate * 256;

	if (hz_frac == hz_int) {
		// this mode has no fractional variant
		mode->fractional = false;
		return;
	}

	// find multiple of hz closest to hz_int
	int hz_n = 1;
	int best_hz = hz;
	int best_diff = abs (hz - hz_int);
	for (;;) {
		hz_n++;
		int multiple_hz = hz * hz_n;
		int multiple_diff = abs (multiple_hz - hz_int);
		if (multiple_diff > best_diff)
			break;
		best_hz = multiple_hz;
		best_diff = multiple_diff;
	}

	if (abs (hz_int - best_hz) < abs (hz_frac - best_hz))
		mode->fractional = false;
}

int modes_find_name_by_hz (const display_mode_t *modes, int n, int hz,
                           char *name_out, size_t name_sz)
{
	int best = -1, best_d = 1 << 30;

	for (int i = 0; i < n; i++) {
		int mhz = display_mode_hz ((display_mode_t *)&modes [i]);
		int d = (mhz > hz) ? (mhz - hz) : (hz - mhz);
		if (d < best_d) {
			best_d = d;
			best = i;
		}
	}

	/* допуск тот же, что у sfmodes (0.125 Гц): частота активного/целевого
	 * режима должна совпасть точно, иначе это не наш режим */
	if ((best < 0) || (best_d > 32))
		return -1;

	if (name_out && name_sz)
		snprintf (name_out, name_sz, "%s", modes [best].name);

	return 0;
}

int modes_select_by_hz (const display_mode_t *modes, int n, int hz,
                        int width, int height, bool interlaced,
                        int prefer_exact, int floor_hz,
                        modes_hz_filter_t allowed, void *ctx,
                        display_mode_t *out, char *name_out, size_t name_sz, int *hz_out)
{
	/* Те же правила, что были в afrd.c: select_mode (legacy-путь):
	 * a) совпадают ширина, высота и чересстрочность;
	 * b) частота режима делится на частоту контента с ошибкой < 4.3 %
	 *    (разница 23.976 и 25 Гц);
	 * c) при prefer_exact=0 предпочитается высокая кратная частота
	 *    (50 Гц лучше 25 Гц для 25-фпс видео), при 1 — ближайшая. */
	unsigned best_rating = 0;
	display_mode_t best;
	memset (&best, 0, sizeof (best));

	for (int i = 0; i < n; i++) {
		const display_mode_t *mode = &modes [i];
		if ((mode->width != width) || (mode->height != height) ||
		    (mode->interlaced != interlaced))
			continue;

		/* mode.min.hz.list: для этого плеера режимы ниже порога не годятся
		 * (плеер «зажимает» темп отрисовки по частоте экрана в момент старта). */
		if (floor_hz && (display_mode_hz ((display_mode_t *)mode) < floor_hz))
			continue;

		unsigned rate_n = 1;
		unsigned rate = ((unsigned)mode->framerate << 16) / (unsigned)hz;
		while (rate > 0x180) {
			rate_n++;
			rate = ((unsigned)mode->framerate << 16) / ((unsigned)hz * rate_n);
		}

		unsigned delta = (unsigned)abs ((int)(rate - 0x100));
		if (delta > 11)
			continue; // freq error > 4.3%

		// rating is larger as delta is closer to 1.0 rate
		int rating = (11 - delta) * 16;

		unsigned nn = (rate_n > 3) ? 3 : (rate_n - 1);
		rating += 4 * (prefer_exact ? (3 - nn) : nn);

		if ((unsigned)rating > best_rating) {
			display_mode_t tmp = *mode;

			// decide if integer or fractional framerate is better
			display_mode_set_hz (&tmp, hz);

			// если частота запрещена — пробуем перевернуть дробность
			if (allowed && !allowed (display_mode_hz (&tmp), ctx)) {
				tmp.fractional = !tmp.fractional;
				if (!allowed (display_mode_hz (&tmp), ctx))
					continue;	// обе частоты запрещены
			}

			best_rating = (unsigned)rating;
			best = tmp;
		}
	}

	if (!best.name [0])
		return -1;

	if (out)
		*out = best;
	if (name_out && name_sz)
		snprintf (name_out, name_sz, "%s", best.name);
	if (hz_out)
		*hz_out = display_mode_hz (&best);

	return 0;
}
