/*
 * Нижняя огибающая частоты кадров: см. fps_env.h.
 */

#include <string.h>
#include "fps_env.h"
#include "fps_class.h"

void fps_env_reset (fps_env_t *e)
{
	e->n = 0;
}

bool fps_env_push (fps_env_t *e, unsigned long long t_us,
                   unsigned long long frames, unsigned long long unc_us)
{
	if (e->n > 0 && (frames <= e->s [e->n - 1].frames))
	{
		/* пауза (счётчик стоит) или новое видео (счётчик сброшен):
		 * прежние выборки к текущему наблюдению не относятся */
		e->n = 0;
	}

	if (e->n >= FPS_ENV_MAX)
	{
		/* кольцо полно: вытесняем самую старую выборку */
		memmove (&e->s [0], &e->s [1], sizeof (e->s [0]) * (FPS_ENV_MAX - 1));
		e->n = FPS_ENV_MAX - 1;
	}

	e->s [e->n].t_us = t_us;
	e->s [e->n].frames = frames;
	e->s [e->n].unc_us = unc_us;
	e->n++;

	return (e->n > 1) && (frames > e->s [e->n - 2].frames);
}

bool fps_env_ready (const fps_env_t *e, unsigned long long horizon_us)
{
	if (e->n < 2)
		return false;

	return (e->s [e->n - 1].t_us - e->s [0].t_us) >= horizon_us;
}

/* Все выборки между i и j должны строго расти по счётчику. */
static bool window_clean (const fps_env_t *e, int i, int j)
{
	for (int k = i; k < j; k++)
		if (e->s [k + 1].frames <= e->s [k].frames)
			return false;

	return true;
}

int fps_env_decide (const fps_env_t *e, unsigned long long horizon_us, unsigned long long skip_us,
                    unsigned long long sub_us, double factor, double offset, int *hz_out)
{
	if (hz_out)
		*hz_out = 0;

	if ((e->n < 2) || (sub_us == 0))
		return 0;

	if (skip_us >= horizon_us)
		skip_us = 0;	/* опечатка в конфиге не должна гасить замер */

	unsigned long long t0 = e->s [0].t_us;
	unsigned long long best_us = 0;		/* выбранное окно: мкс на кадр */
	unsigned long long best_delta = 0, best_dt = 0, best_unc = 0;
	bool have = false;

	for (int i = 0; (i + 1) < e->n; i++)
	{
		unsigned long long ti = e->s [i].t_us;

		if ((ti - t0) < skip_us)
			continue;		/* прогрев */

		int j = -1;
		for (int k = i + 1; k < e->n; k++)
		{
			if ((e->s [k].t_us - ti) < sub_us)
				continue;
			if (!window_clean (e, i, k))
				break;		/* внутри окна пауза — шире брать нельзя */
			j = k;
			break;
		}

		if (j < 0)
			continue;

		if ((e->s [j].t_us - t0) > horizon_us)
			break;			/* горизонт исчерпан */

		unsigned long long delta = e->s [j].frames - e->s [i].frames;
		unsigned long long dt = e->s [j].t_us - e->s [i].t_us;
		unsigned long long rate_us = dt / delta;	/* меньше — быстрее */

		if (!have || (rate_us > best_us))
		{
			have = true;
			best_us = rate_us;
			best_delta = delta;
			best_dt = dt;
			best_unc = (e->s [i].unc_us + e->s [j].unc_us) / 2;
		}
	}

	if (!have)
		return 0;

	return fps_classify_window_corrected (best_delta, best_dt, best_unc, factor, offset, hz_out);
}
