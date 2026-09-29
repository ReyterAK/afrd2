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
#define FPS_MIN		15.0
#define FPS_MAX		130.0

int fps_rate_hz (int index)
{
	return (int)(k_rates [index].fps * 256.0 + 0.5);
}

const char *fps_rate_name (int index)
{
	return k_rates [index].name;
}

double fps_estimate (unsigned long long frames_a, unsigned long long ts_a_us,
                     unsigned long long frames_b, unsigned long long ts_b_us)
{
	if (frames_b <= frames_a || ts_b_us <= ts_a_us)
		return 0.0;

	double dt = (double)(ts_b_us - ts_a_us) / 1000000.0;
	if (dt < 0.5)
		return 0.0;

	double fps = (double)(frames_b - frames_a) / dt;
	if (fps < FPS_MIN || fps > FPS_MAX)
		return 0.0;

	return fps;
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
