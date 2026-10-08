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

int main (void)
{
	test_25_doubling ();
	test_find_name ();
	test_25_exact ();
	test_23976 ();
	test_60 ();
	test_interlace_mismatch ();
	test_geometry ();
	test_floor ();
	test_blacklist ();
	test_no_match ();

	printf ("modes_sel: OK\n");
	return 0;
}
