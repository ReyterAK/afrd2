#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../afrd.h"
#include "../modes_sel.h"
#include "../fps_class.h"

/* Список режимов как из disp_cap стенда .45 (Amlogic X4, Android 11). */
static display_mode_t modes [] =
{
	{ "1080p60hz", 1920, 1080, 60, false, false },
	{ "1080p50hz", 1920, 1080, 50, false, false },
	{ "1080p30hz", 1920, 1080, 30, false, false },
	{ "1080p25hz", 1920, 1080, 25, false, false },
	{ "1080p24hz", 1920, 1080, 24, false, false },
	{ "720p50hz",  1280,  720, 50, false, false },
	{ "1080i50hz", 1920, 1080, 50, true,  false },
};
#define N_MODES ((int)(sizeof (modes) / sizeof (g_modes [0])))

/* Чёрный список частот (mode.blacklist.rates): true — частота разрешена. */
static bool allow_all (int hz, void *ctx)
{
	(void)hz; (void)ctx;
	return true;
}

static bool ban_50 (int hz, void *ctx)
{
	(void)ctx;
	return hz != 50 * 256;
}

/* Отчётная частота: список disp_cap хранит номиналы, а фактическую частоту
 * задаёт узел frac_rate_policy (1 — дробная). На вендорном пути (Android
 * 10/11) имя режима одно на оба варианта, и без этого уточнения отчёт
 * показывал 60.00 Гц при фактических 59.94 — это приняли за переключение
 * базовой частоты демоном (вопрос тестера 27.10). */
static void test_hz_actual (void)
{
	display_mode_t m60 = { "1080p60hz", 1920, 1080, 60, false, false };
	display_mode_t m50 = { "1080p50hz", 1920, 1080, 50, false, false };
	display_mode_t m24 = { "1080p24hz", 1920, 1080, 24, false, false };

	/* политика целая — номинал */
	assert (display_mode_hz_actual (&m60, false) == 60 * 256);

	/* политика дробная — те же значения, что даёт таблица частот */
	assert (display_mode_hz_actual (&m60, true) == (5994 * 256 + 50) / 100);
	assert (display_mode_hz_actual (&m24, true) == (2997 * 256 + 62) / 125);

	/* у 50/25 дробного варианта нет — политика число не меняет */
	assert (display_mode_hz_actual (&m50, true) == 50 * 256);
}

/* 25 fps: при prefer_exact=0 предпочитается удвоение 2:1 (50 Гц) — как в
 * legacy-пути, чтобы плеер не «зажимал» темп. */
static void test_25_doubling (void)
{
	char name [32];
	int hz = 0;

	assert (modes_select_by_hz (modes, N_MODES, 25 * 256, 1920, 1080, false,
	                            0, 0, allow_all, NULL, NULL, name, sizeof (name), &hz) == 0);
	assert (strcmp (name, "1080p50hz") == 0);
	assert (hz == 50 * 256);
}

/* prefer_exact=1 — точное совпадение (25 Гц). */
static void test_25_exact (void)
{
	char name [32];
	int hz = 0;

	assert (modes_select_by_hz (modes, N_MODES, 25 * 256, 1920, 1080, false,
	                            1, 0, allow_all, NULL, NULL, name, sizeof (name), &hz) == 0);
	assert (strcmp (name, "1080p25hz") == 0);
	assert (hz == 25 * 256);
}

/* 23.976 (класс даёт 6138 в 24.8) — дробный представитель режима 24 Гц. */
static void test_23976 (void)
{
	char name [32];
	int hz = 0;

	int c = fps_classify (23.976);
	assert (modes_select_by_hz (modes, N_MODES, c, 1920, 1080, false,
	                            0, 0, allow_all, NULL, NULL, name, sizeof (name), &hz) == 0);
	assert (strcmp (name, "1080p24hz") == 0);
	/* выбранному режиму выставили дробную частоту (23.976), а не целые 24 */
	assert (hz == 6138);
}

/* 60 fps — точное совпадение без удвоения. */
static void test_60 (void)
{
	char name [32];
	int hz = 0;

	assert (modes_select_by_hz (modes, N_MODES, 60 * 256, 1920, 1080, false,
	                            0, 0, allow_all, NULL, NULL, name, sizeof (name), &hz) == 0);
	assert (strcmp (name, "1080p60hz") == 0);
}

/* Чересстрочность обязана совпасть: для прогрессивного контента 1080i не годится. */
static void test_interlace_mismatch (void)
{
	char name [32];
	int hz = 0;

	/* единственный режим с такой геометрией и interlaced=true — 1080i50:
	 * для 25 fps он подойдёт (удвоение), для 1080p-запроса — нет */
	assert (modes_select_by_hz (modes, N_MODES, 25 * 256, 1920, 1080, true,
	                            0, 0, allow_all, NULL, NULL, name, sizeof (name), &hz) == 0);
	assert (strcmp (name, "1080i50hz") == 0);
}

/* Геометрия: 720p-режим не должен подхватываться для 1920x1080 того же класса. */
static void test_geometry (void)
{
	char name [32];
	int hz = 0;

	assert (modes_select_by_hz (modes, N_MODES, 50 * 256, 1280, 720, false,
	                            0, 0, allow_all, NULL, NULL, name, sizeof (name), &hz) == 0);
	assert (strcmp (name, "720p50hz") == 0);
}

/* Пол (mode.min.hz.list): 25-фпс плееру режимы ниже 30 Гц не годятся —
 * остаётся удвоение 50. */
static void test_floor (void)
{
	char name [32];
	int hz = 0;

	assert (modes_select_by_hz (modes, N_MODES, 25 * 256, 1920, 1080, false,
	                            1, 30 * 256, allow_all, NULL, NULL, name, sizeof (name), &hz) == 0);
	assert (strcmp (name, "1080p50hz") == 0);
}

/* Чёрный список: 50 Гц запрещена -> для 25 fps остаётся точная 25 Гц. */
static void test_blacklist (void)
{
	char name [32];
	int hz = 0;

	assert (modes_select_by_hz (modes, N_MODES, 25 * 256, 1920, 1080, false,
	                            0, 0, ban_50, NULL, NULL, name, sizeof (name), &hz) == 0);
	assert (strcmp (name, "1080p25hz") == 0);
}

/* Запрет по группе частот: на вендорном и legacy-путях имя режима одно на
 * 24.000 и 23.976, поэтому запрет любого из двух должен отсекать этот режим.
 * На backdoor-пути оба члена приходят отдельными режимами — там сравнение
 * остаётся точным (иначе пользователь не смог бы запретить только 23.98). */
static bool present_one24 (int hz, void *ctx)
{
	(void) ctx;
	return hz == (24 * 256);
}

static bool present_two24 (int hz, void *ctx)
{
	(void) ctx;
	return (hz == (24 * 256)) || (hz == 6138);
}

static bool present_one30 (int hz, void *ctx)
{
	(void) ctx;
	return hz == (30 * 256);
}

static void test_blacklist_group (void)
{
	const int bl_23976 [] = { 6138 };
	const int bl_24 [] = { 24 * 256 };
	const int bl_2997 [] = { 7672 };
	const int bl_25 [] = { 25 * 256 };

	/* Таблица с одним режимом 24 (disp_cap ATV11): запрет любой из пары
	 * отсекает режим, иначе половина записей в настройке не действует. */
	assert (mode_rate_blacklisted (24 * 256, bl_23976, 1, present_one24, NULL));
	assert (mode_rate_blacklisted (24 * 256, bl_24, 1, present_one24, NULL));
	assert (!mode_rate_blacklisted (24 * 256, bl_25, 1, present_one24, NULL));

	/* Таблица с двумя режимами (SurfaceFlinger ATV14): записи различимы,
	 * запрет 23.98 не трогает 24.00 и наоборот. */
	assert (!mode_rate_blacklisted (24 * 256, bl_23976, 1, present_two24, NULL));
	assert (!mode_rate_blacklisted (6138, bl_24, 1, present_two24, NULL));
	assert (mode_rate_blacklisted (6138, bl_23976, 1, present_two24, NULL));
	assert (mode_rate_blacklisted (24 * 256, bl_24, 1, present_two24, NULL));

	/* Класс 30 — та же пара: 29.97 и 30. */
	assert (mode_rate_blacklisted (30 * 256, bl_2997, 1, present_one30, NULL));
	assert (!mode_rate_blacklisted (30 * 256, bl_25, 1, present_one30, NULL));

	/* Пустой список и нечисловая частота — не запрещено. */
	assert (!mode_rate_blacklisted (24 * 256, NULL, 0, present_one24, NULL));
	assert (!mode_rate_blacklisted (0, bl_24, 1, present_one24, NULL));
}

/* Кратность — это точное удвоение/утроение, а не соседний класс частот.
 * 23.976 и 25 различаются на 4.3 %, и старый допуск (11/256) позволял 50 Гц
 * «быть удвоением» для 24p: при запрете 1080p24hz демон уводил 23.98-контент
 * на 50 Гц (живой прогон `.42`, 05.11). */
static display_mode_t modes_no24 [] =
{
	{ "1080p50hz", 1920, 1080, 50, false, false },
	{ "1080p60hz", 1920, 1080, 60, false, false },
};
#define N_MODES_NO24 ((int)(sizeof (modes_no24) / sizeof (modes_no24 [0])))

static void test_no_fake_multiple (void)
{
	char name [32];
	int hz = 0;

	/* 24p без режима 24 — подходящего режима нет вовсе (60 Гц тоже не
	 * кратна: 2.5x), демон оставит текущий режим. */
	assert (modes_select_by_hz (modes_no24, N_MODES_NO24, 6138, 1920, 1080, false,
	                            0, 0, allow_all, NULL, NULL, name, sizeof (name), &hz) == -1);

	/* 25 -> 50 и 30 -> 60 остаются законным удвоением. */
	assert (modes_select_by_hz (modes_no24, N_MODES_NO24, 25 * 256, 1920, 1080, false,
	                            0, 0, allow_all, NULL, NULL, name, sizeof (name), &hz) == 0);
	assert (strcmp (name, "1080p50hz") == 0);
	assert (modes_select_by_hz (modes_no24, N_MODES_NO24, 30 * 256, 1920, 1080, false,
	                            0, 0, allow_all, NULL, NULL, name, sizeof (name), &hz) == 0);
	assert (strcmp (name, "1080p60hz") == 0);
}

/* Нет подходящего режима — честный отказ, а не «первый попавшийся». */
static void test_no_match (void)
{
	char name [32];
	int hz = 0;

	assert (modes_select_by_hz (modes, N_MODES, 25 * 256, 3840, 2160, false,
	                            0, 0, allow_all, NULL, NULL, name, sizeof (name), &hz) == -1);
	/* нулевой список */
	assert (modes_select_by_hz (modes, 0, 25 * 256, 1920, 1080, false,
	                            0, 0, allow_all, NULL, NULL, name, sizeof (name), &hz) == -1);
}

/* Имя режима по частоте: нужно пути Android 10/11 (помощник принимает имя). */
static void test_find_name (void)
{
	char name [32];

	/* класс 23.976 (6138) -> имя режима 24 Гц, хотя в списке хранится 24.000 */
	assert (modes_find_name_by_hz (modes, N_MODES, 6138, name, sizeof (name)) == 0);
	assert (strcmp (name, "1080p24hz") == 0);

	assert (modes_find_name_by_hz (modes, N_MODES, 50 * 256, name, sizeof (name)) == 0);
	assert (strcmp (name, "1080p50hz") == 0);

	/* такого режима нет */
	assert (modes_find_name_by_hz (modes, N_MODES, 100 * 256, name, sizeof (name)) == -1);
}

/* Какая дробность нужна для частоты контента (решение для узла
 * frac_rate_policy на вендорном пути). */
static void test_rate_fract_decide (void)
{
	int want = -1;

	/* 23.976 (6138) — дробная, 24.000 (6144) — целая */
	assert (mode_rate_fract_decide (6138, &want) && want == 1);
	assert (mode_rate_fract_decide (6144, &want) && want == 0);
	/* 29.97 (7672) и 59.94 (15345) — дробные, их целые пары — целые */
	assert (mode_rate_fract_decide (7672, &want) && want == 1);
	assert (mode_rate_fract_decide (15345, &want) && want == 1);
	assert (mode_rate_fract_decide (15360, &want) && want == 0);
	/* у 25 и 50 дробного варианта нет — политику не трогаем */
	assert (!mode_rate_fract_decide (25 * 256, &want));
	assert (!mode_rate_fract_decide (50 * 256, &want));
	assert (!mode_rate_fract_decide (0, &want));
}

int main (void)
{
	test_rate_fract_decide ();
	test_hz_actual ();
	test_25_doubling ();
	test_find_name ();
	test_25_exact ();
	test_23976 ();
	test_60 ();
	test_interlace_mismatch ();
	test_geometry ();
	test_floor ();
	test_blacklist ();
	test_blacklist_group ();
	test_no_fake_multiple ();
	test_no_match ();

	printf ("modes_sel: OK\n");
	return 0;
}
