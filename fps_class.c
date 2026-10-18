#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fps_class.h"
#include "pkgdet.h"	/* PKG_NAME_LEN: буферы имён пакетов */

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

/* Тот же выбор для `mode.use.fract=0` — точные (целочисленные) режимы. */
static const int k_group_exact [FPS_RATE_COUNT] = { 1, 1, 2, 4, 4, 5, 7, 7 };

int fps_rate_group (int hz)
{
	for (int i = 0; i < FPS_RATE_COUNT; i++)
		if (fps_rate_hz (i) == hz)
			return k_group [i];

	return 0;
}

int fps_rate_exact_member (int hz)
{
	for (int i = 0; i < FPS_RATE_COUNT; i++)
		if (fps_rate_hz (i) == hz)
			return fps_rate_hz (k_group_exact [i]);

	return 0;
}

/* ---- Поправка частоты для конкретного плеера: см. fps_class.h ---- */

static struct
{
	char pkg [PKG_NAME_LEN];
	double factor;	/* 1 + pct/100 */
} k_correct [FPS_CORRECT_MAX];

static int k_correct_n;

void fps_correct_clear (void)
{
	k_correct_n = 0;
	fps_correct_clear_av1 ();
}

void fps_correct_load (const char *list)
{
	fps_correct_clear ();
	if (!list)
		return;

	const char *cur = list;
	while (*cur)
	{
		while ((*cur == ' ') || (*cur == '\t'))
			cur++;
		if (!*cur)
			break;

		const char *end = cur + strcspn (cur, " \t");

		const char *eq = memchr (cur, '=', (size_t)(end - cur));
		if (eq && (eq > cur) && (k_correct_n < FPS_CORRECT_MAX))
		{
			double pct = atof (eq + 1);
			size_t len = (size_t)(eq - cur);
			if ((len < sizeof (k_correct [0].pkg)) && (pct > -50.0) && (pct < 50.0) && (pct != 0.0))
			{
				memcpy (k_correct [k_correct_n].pkg, cur, len);
				k_correct [k_correct_n].pkg [len] = 0;
				k_correct [k_correct_n].factor = 1.0 + pct / 100.0;
				k_correct_n++;
			}
		}

		cur = end;
	}
}

double fps_correct_factor (const char *package)
{
	if (package)
		for (int i = 0; i < k_correct_n; i++)
			if (strcmp (k_correct [i].pkg, package) == 0)
				return k_correct [i].factor;

	return 1.0;
}

/* ---- Поправка на завышение счётчика AV1-трактом: см. fps_class.h ---- */

static struct
{
	double	offset;
	double	pct;		/* относительная поправка, % (fps.correct.av1.pct) */
	char	pkg [PKG_NAME_LEN];
} k_av1;

void fps_correct_clear_av1 (void)
{
	k_av1.offset = 0.0;
	k_av1.pct = 0.0;
	k_av1.pkg [0] = 0;
}

void fps_correct_set_av1 (double offset_fps, const char *package)
{
	/* Не сбрасываем pct: обе поправки задаются разными ключами и должны
	 * сосуществовать (иначе вторая вызываемая затирала бы первую —
	 * живая находка 18.10: pct не применялась). */
	if (offset_fps <= 0.0)
		return;

	k_av1.offset = offset_fps;
	if (package)
	{
		while ((*package == ' ') || (*package == '\t'))
			package++;

		size_t len = strcspn (package, " \t");
		if (len >= sizeof (k_av1.pkg))
		{
			/* Имя пакета не влезает: скопировать его нельзя, а оставить
			 * пакет пустым — ещё хуже: пустой пакет означает «любой
			 * плеер», и поправка расширилась бы на всех. Отбрасываем
			 * поправку целиком, как fps_correct_load (). */
			fps_correct_clear_av1 ();
			return;
		}
		if (len)
		{
			memcpy (k_av1.pkg, package, len);
			k_av1.pkg [len] = 0;
		}
	}
}

/* Регистронезависимый поиск подстроки: strcasestr есть не во всех libc
 * (bionic до API 30 его не имеет). */
static bool contains_ci (const char *hay, const char *needle)
{
	if (!hay || !needle || !*needle)
		return false;

	for (; *hay; hay++)
	{
		const char *h = hay, *n = needle;
		while (*h && *n && (tolower ((unsigned char)*h) == tolower ((unsigned char)*n)))
		{
			h++;
			n++;
		}
		if (!*n)
			return true;
	}

	return false;
}

void fps_correct_set_av1_pct (double pct, const char *package)
{
	if (pct <= 0.0)
		return;

	/* Диапазон как у поправок по плееру: 0.1..49.9 %, иначе запись считается
	 * ошибочной (защита от опечатки «570» вместо «5.7»). */
	if (pct > 49.9)
		return;

	k_av1.pct = pct;
	if (package)
	{
		while ((*package == ' ') || (*package == '\t'))
			package++;

		size_t len = strcspn (package, " \t");
		if (len >= sizeof (k_av1.pkg))
		{
			fps_correct_clear_av1 ();
			return;
		}
		if (len)
		{
			memcpy (k_av1.pkg, package, len);
			k_av1.pkg [len] = 0;
		}
	}
}

double fps_correct_av1_factor (const char *device, const char *package)
{
	if (k_av1.pct <= 0.0)
		return 1.0;

	if (k_av1.pkg [0] && (!package || (strcmp (k_av1.pkg, package) != 0)))
		return 1.0;

	if (!contains_ci (device, "av1"))
		return 1.0;

	return 1.0 + k_av1.pct / 100.0;
}

double fps_correct_av1_offset (const char *device, const char *package)
{
	if (k_av1.offset <= 0.0)
		return 0.0;

	if (k_av1.pkg [0] && (!package || (strcmp (k_av1.pkg, package) != 0)))
		return 0.0;

	if (!contains_ci (device, "av1"))
		return 0.0;

	return k_av1.offset;
}

static int window_decide (double frames_delta, unsigned long long dt_us,
                          unsigned long long dt_unc_us, int *hz_out)
{
	if (hz_out)
		*hz_out = 0;

	if (frames_delta <= 0.0 || !dt_us || (dt_us <= dt_unc_us))
		return 0;

	double dt_lo = (double)(dt_us - dt_unc_us) / 1000000.0;
	double dt_hi = (double)(dt_us + dt_unc_us) / 1000000.0;

	int center = fps_classify (frames_delta * 1000000.0 / (double)dt_us);
	int group = fps_rate_group (center);
	if (!group)
		return 0;

	/* границы доверительного интервала: кадров меньше при самом длинном
	 * интервале и кадров больше при самом коротком */
	double lo = (frames_delta > 1.0) ? (frames_delta - 1.0) / dt_hi : 0.0;
	double hi = (frames_delta + 1.0) / dt_lo;

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

int fps_classify_window_corrected (unsigned long long frames_delta, unsigned long long dt_us,
                                   unsigned long long dt_unc_us,
                                   double factor, double offset, int *hz_out)
{
	if (factor <= 0.0)
		factor = 1.0;

	double dt_s = (double)dt_us / 1000000.0;
	double adj = (double)frames_delta / factor - offset * dt_s;

	return window_decide (adj, dt_us, dt_unc_us, hz_out);
}

int fps_classify_window (unsigned long long frames_delta, unsigned long long dt_us,
                         unsigned long long dt_unc_us, int *hz_out)
{
	return fps_classify_window_corrected (frames_delta, dt_us, dt_unc_us, 1.0, 0.0, hz_out);
}

int fps_classify_window_scaled (unsigned long long frames_delta, unsigned long long dt_us,
                                unsigned long long dt_unc_us, double correct, int *hz_out)
{
	return fps_classify_window_corrected (frames_delta, dt_us, dt_unc_us, correct, 0.0, hz_out);
}

int fps_refine_declared (int measured_hz, double declared_fps, double max_pct, int *hz_out)
{
	if (hz_out)
		*hz_out = 0;

	if (!measured_hz || (declared_fps <= 0.0) || (max_pct <= 0.0))
		return 0;

	int declared = fps_classify (declared_fps);
	if (!declared)
		return 0;

	double measured = (double)measured_hz / 256.0;
	double decl = (double)declared / 256.0;
	double diff = (measured > decl) ? (measured - decl) : (decl - measured);

	if (diff / decl > max_pct / 100.0)
		return 0;	/* далеко — это мусор в поле, а не уточнение */

	if (hz_out)
		*hz_out = fps_rate_hz (k_group_lead [fps_rate_index_for_hz (declared)]);

	return 1;
}

int fps_declared_class (int declared_fps)
{
	/* Драйвер заявляет целые fps; мусор (0, 255, 1000) не берём — такую
	 * частоту быстрый путь применять не должен, решает замер. */
	if ((declared_fps <= 0) || (declared_fps > 120))
		return 0;

	return fps_classify ((double)declared_fps);
}

bool fps_group_differs (int a_hz, int b_hz)
{
	int ga = fps_rate_group (a_hz);
	int gb = fps_rate_group (b_hz);
	if (!ga || !gb)
		return false;

	return ga != gb;
}
