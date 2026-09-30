#include <math.h>
#include <stdio.h>
#include "fps_class.h"

static const struct { const char *name; double fps; } k_rates [FPS_RATE_COUNT] =
{
	{ "23.976", 24000.0 / 1001.0 },
	{ "24",     24.0 },
	{ "25",     25.0 },
	{ "29.97",  30000.0 / 1001.0 },
	{ "30",     30.0 },
	{ "50",     50.0 },
	{ "59.94",  60000.0 / 1001.0 },
	{ "60",     60.0 },
};

/* Допуск 6% от частоты: окна соседних стандартных частот перекрываются, */
/* поэтому выбирается ближайшая по относительной ошибке (fps_classify).   */
/* 35 fps не классифицируется: отн. ошибка >6% и к 30 (16.7%), и к 25.    */
#define FPS_TOLERANCE	0.06

int fps_rate_hz (int index)
{
	return (int)(k_rates [index].fps * 256.0 + 0.5);
}

const char *fps_rate_name (int index)
{
	return k_rates [index].name;
}

int fps_classify (double measured)
{
	if (measured <= 0.0)
		return 0;

	int best = -1;
	double best_err = 1e9;
	for (int i = 0; i < FPS_RATE_COUNT; i++)
	{
		double err = fabs (measured - k_rates [i].fps) / k_rates [i].fps;
		if (err < best_err)
		{
			best_err = err;
			best = i;
		}
	}

	if ((best < 0) || (best_err > FPS_TOLERANCE))
		return 0;

	return fps_rate_hz (best);
}

int fps_rate_index_for_hz (int hz)
{
	int best = -1, best_d = 1 << 30;

	for (int i = 0; i < FPS_RATE_COUNT; i++)
	{
		int h = fps_rate_hz (i);
		int d = (hz > h) ? (hz - h) : (h - hz);
		if (d < best_d)
		{
			best_d = d;
			best = i;
		}
	}

	return best;
}

/* Группа режимов (индекс строки k_rates -> частота группы, Гц):
 * 23.976 и 24, 29.97 и 30, 59.94 и 60 — на выходе HDMI это один режим. */
static const int k_group [FPS_RATE_COUNT] = { 24, 24, 25, 30, 30, 50, 60, 60 };

/* Представитель группы, которого отдаёт fps_classify_window: дробный
 * (23.976, 29.97, 59.94) — контент почти всегда идёт с дробной частотой,
 * а счётчик кадров эту пару не различает (0.1%). */
static const int k_group_lead [FPS_RATE_COUNT] = { 0, 0, 2, 3, 3, 5, 6, 6 };

int fps_rate_group (int hz)
{
	for (int i = 0; i < FPS_RATE_COUNT; i++)
		if (fps_rate_hz (i) == hz)
			return k_group [i];

	return 0;
}

int fps_classify_window (unsigned long long frames_delta, unsigned long long dt_us,
                         unsigned long long dt_unc_us, int *hz_out)
{
	if (hz_out)
		*hz_out = 0;

	if (!frames_delta || !dt_us || (dt_us <= dt_unc_us))
		return 0;

	double dt_lo = (double)(dt_us - dt_unc_us) / 1000000.0;
	double dt_hi = (double)(dt_us + dt_unc_us) / 1000000.0;

	int center = fps_classify ((double)frames_delta * 1000000.0 / (double)dt_us);
	int group = fps_rate_group (center);
	if (!group)
		return 0;

	/* границы доверительного интервала: кадров меньше при самом длинном
	 * интервале и кадров больше при самом коротком */
	double lo = (frames_delta > 1) ? (double)(frames_delta - 1) / dt_hi : 0.0;
	double hi = (double)(frames_delta + 1) / dt_lo;

	if ((group != fps_rate_group (fps_classify (lo)))
	 || (group != fps_rate_group (fps_classify (hi))))
		return 0;

	/* Внутри группы отдаём ДРОБНОГО представителя: счётчик не различает
	 * 23.976 и 24 (0.1% = 0.04 кадра за окно), а контент почти всегда идёт
	 * с дробной частотой (плёнка 23.976, NTSC 29.97/59.94) — так же поступает
	 * legacy-путь через frac_rate_policy. Иначе выбор между 23.98 и 24.00
	 * зависел бы от субмиллисекундной добавки к номинальному окну. */
	if (hz_out)
		*hz_out = fps_rate_hz (k_group_lead [fps_rate_index_for_hz (center)]);

	return 1;
}
