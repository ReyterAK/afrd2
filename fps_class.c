#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

/* Тот же выбор для `mode.use.fract=2` — точные (целочисленные) режимы. */
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

int fps_rate_fract_member (int hz)
{
	for (int i = 0; i < FPS_RATE_COUNT; i++)
		if (fps_rate_hz (i) == hz)
			return fps_rate_hz (k_group_lead [i]);

	return 0;
}

int fps_rate_declared_exact (int declared_hz)
{
	if (declared_hz <= 0)
		return 0;

	int i = fps_rate_index_for_hz (declared_hz);
	int hz = fps_rate_hz (i);
	int d = (declared_hz > hz) ? (declared_hz - hz) : (hz - declared_hz);

	/* Допуск 2/256 Гц = 0.008 Гц (0.033 %): между 23.976 и 24.000 лежит
	 * 0.024 Гц, то есть пара различается с трёхкратным запасом, а мусор
	 * в поле (23.994 из frame dur 4001) остаётся целым. */
	if ((d > 2) || (k_group_exact [i] != i))
		return 0;

	return hz;
}

int fps_rate_apply_policy (int hz, int declared_hz, int policy)
{
	if (hz <= 0)
		return hz;

	if (policy == 2)
	{
		int exact = fps_rate_exact_member (hz);
		return exact ? exact : hz;
	}

	/* «Всегда дробная»: приводим к дробному представителю группы. Для
	 * решения замера это ничего не меняет (окно уже отдаёт 23.976/29.97/59.94),
	 * но подсказка плеера и быстрое решение по полю `frame rate` приходят
	 * как 24.000/30.000/60.000 — без приведения они уводили первый переход
	 * в целый режим, а замер через 14-20 с переключал его в дробный. */
	if (policy == 1)
	{
		int frac = fps_rate_fract_member (hz);
		return frac ? frac : hz;
	}

	if (policy != 0)
		return hz;

	/* «Авто»: целое — только если источник заявил целое И заявленное
	 * попадает в ту же группу, что замер. Гейт по группе отсекает мусор
	 * (драйвер заявляет 30 fps при реальных 23.976 — журнал §4г). */
	int exact = fps_rate_declared_exact (declared_hz);
	if (exact && (fps_rate_group (exact) == fps_rate_group (hz)))
		return exact;

	return hz;
}

int fps_sources_ratio_pct (int a_hz, int b_hz)
{
	if ((a_hz <= 0) || (b_hz <= 0))
		return 0;

	int hi = (a_hz > b_hz) ? a_hz : b_hz;
	int lo = (a_hz > b_hz) ? b_hz : a_hz;

	/* 1.6 — граница между «немного разошлись» (соседний класс, другой режим)
	 * и «ровно вдвое». Сравнение в целых: hi/lo >= 1.6 <=> hi*10 >= lo*16. */
	if ((hi * 10) < (lo * 16))
		return 0;

	return (hi * 100 + lo / 2) / lo;
}

void fps_av1_resolve (const char *value, double legacy_add, double legacy_pct,
                      double auto_add, double auto_pct, fps_av1_t *out)
{
	if (!out)
		return;

	out->factor = 1.0;
	out->offset = 0.0;
	out->src = FPS_AV1_OFF;
	out->legacy_conflict = false;

	if (value && value [0])
	{
		/* Имена проверяются ДО atof: "auto" даёт atof = 0 и был бы принят
		 * за «выключено» (нашло host-тестом). */
		if (!strcasecmp (value, "auto"))
		{
			/* Значение платформы: на ATV14 оно аддитивное, на ATV11 —
			 * относительное; некалиброванная платформа не даёт ничего. */
			out->offset = (auto_add > 0.0) ? auto_add : 0.0;
			out->factor = (auto_pct > 0.0) ? (1.0 + auto_pct / 100.0) : 1.0;
			if ((out->offset > 0.0) || (out->factor != 1.0))
				out->src = FPS_AV1_AUTO;
			return;
		}

		if (!strcasecmp (value, "off"))
			return;			/* выключено */

		double pct = atof (value);
		if (pct > 0.0)
		{
			out->factor = 1.0 + pct / 100.0;
			out->src = FPS_AV1_PCT;
		}
		return;
	}

	/* Ключа нет — старое поведение, но без двойной коррекции: обе поправки
	 * одного явления, применяется одна (относительная точнее описывает
	 * пропорциональное завышение и уже включает в себя аддитивную часть). */
	if (legacy_pct > 0.0)
	{
		out->factor = 1.0 + legacy_pct / 100.0;
		out->src = FPS_AV1_LEGACY_PCT;
		out->legacy_conflict = (legacy_add > 0.0);
		return;
	}

	if (legacy_add > 0.0)
	{
		out->offset = legacy_add;
		out->src = FPS_AV1_LEGACY_ADD;
	}
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

/* Фильтр пакетов для поправки AV1: список через запятую
 * (fps.correct.av1.pkg). Пустой фильтр — поправка для всех; имена
 * пакетов регистрозависимы, поэтому сравнение точное. */
static bool pkg_filter_match (const char *list, const char *package)
{
	if (!list || !*list)
		return true;
	if (!package || !*package)
		return false;

	const char *p = list;
	while (*p)
	{
		const char *comma = strchr (p, ',');
		size_t len = comma ? (size_t)(comma - p) : strlen (p);

		if (len && (strlen (package) == len) && (strncmp (p, package, len) == 0))
			return true;
		if (!comma)
			break;
		p = comma + 1;
	}

	return false;
}

double fps_correct_av1_factor (const char *device, const char *package)
{
	if (k_av1.pct <= 0.0)
		return 1.0;

	if (!pkg_filter_match (k_av1.pkg, package))
		return 1.0;

	if (!contains_ci (device, "av1"))
		return 1.0;

	return 1.0 + k_av1.pct / 100.0;
}

double fps_correct_av1_offset (const char *device, const char *package)
{
	if (k_av1.offset <= 0.0)
		return 0.0;

	if (!pkg_filter_match (k_av1.pkg, package))
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

/* Вердикт подачи видео-слоя (см. fps_class.h). */
int fps_cadence_verdict (int median_ms, int period_ms, int expect_ms,
                         int gap_pct, int gap_thr)
{
	double base = (expect_ms > period_ms) ? (double) expect_ms : (double) period_ms;

	if (base <= 0.0)
		return FPS_CADENCE_OK;

	/* Медиана вдвое больше базы — медленнее ВСЯ подача: это дефект
	 * vsync-пути HWC/SF после смены режима, лечится сменой режима
	 * «туда-обратно» (журнал §21.9). */
	if ((double) median_ms > 1.5 * base)
		return FPS_CADENCE_HALVED;

	/* Медиана заметно НИЖЕ базы — подача быстрее ожидаемой: на 1080i50
	 * счётчик даёт 25 кадров, а в плоскость идёт 50 полей (§115). Доля
	 * «выбросов» здесь считается от завышенной базы и ничего не значит. */
	if ((double) median_ms < 0.75 * base)
		return FPS_CADENCE_OK;

	/* Медиана в норме, но часть интервалов длиннее порога: плеер теряет
	 * кадры. Сменой режима это не лечится — живой прогон `.42` 03.11:
	 * 19 % интервалов по 84 мс при медиане 41.7 мс, после «лечения»
	 * 17 %, то есть без изменений. Диагноз ставим, режим не трогаем. */
	if (gap_pct >= gap_thr)
		return FPS_CADENCE_UNEVEN;

	return FPS_CADENCE_OK;
}

/* Частота контента по подаче видео-слоя (см. fps_class.h, журнал §179).
 * Порог «мало данных» — 100 меток: буфер SF хранит 128, при 24 к/с это ~4 с,
 * за которые среднее уже устойчиво, а старые кадры прежнего ролика успевают
 * вытесниться. */
int fps_layer_rate_hz (int frames, double avg_ms, double period_ms,
                       int gap_pct, int *hz_out)
{
	if (hz_out)
		*hz_out = 0;

	if ((frames < 100) || (avg_ms <= 0.0) || (period_ms <= 0.0))
		return 0;

	/* Слой обновляется с частотой дисплея — о частоте контента сказать
	 * нечего (кадры идут «как есть», плеер дублирует). */
	if (avg_ms <= period_ms * 1.05)
		return 0;

	/* Выброс в буфере — это пауза, смена ролика или переход: среднее уже
	 * не про текущую подачу. Ждём, пока буфер заполнится заново. */
	if (gap_pct > 0)
		return 0;

	unsigned long long n = (unsigned long long) (frames - 1);
	unsigned long long dt_us =
		(unsigned long long) (avg_ms * 1000.0 * (double) n + 0.5);
	int hz = 0;

	if (!fps_classify_window_corrected (n, dt_us, 2000, 1.0, 0.0, &hz))
		return 0;

	if (hz_out)
		*hz_out = hz;

	return 1;
}

int fps_fb_declared_hz (int measured_hz, int declared_hz)
{
	/* Окно 1.5..2.5 заявленной частоты: счётчик fb-декодера Amlogic считает
	 * вдвое (§90: 59.94 при 30), а замер огибающей на таком тракте даёт
	 * удвоенную частоту. Вне окна ничего не меняем: обычное расхождение
	 * замера и заявки правит fps_refine_declared, а не это правило. */
	if ((measured_hz <= 0) || (declared_hz <= 0))
		return measured_hz;

	long long lo = (long long) declared_hz * 3 / 2;
	long long hi = (long long) declared_hz * 5 / 2;

	if ((measured_hz < lo) || (measured_hz > hi))
		return measured_hz;

	return declared_hz;
}
