#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sfmodes.h"

static void read_file (const char *path, char *buf, size_t sz)
{
	FILE *f = fopen (path, "rb");
	assert (f);
	size_t n = fread (buf, 1, sz - 1, f);
	buf [n] = 0;
	fclose (f);
}

/* Дамп с синтетической таблицей (id с 0). */
static void test_synthetic (void)
{
	static char buf [65536];
	read_file ("tests/fixtures/sf_modes.txt", buf, sizeof (buf));
	char *dump = buf;

	sf_mode_t modes [SF_MODE_MAX];
	int n = sfmodes_parse (dump, modes, SF_MODE_MAX);

	assert (n == 9);
	assert (modes [0].id == 0 && modes [0].hz == 59 * 256 + 241); /* 59.94 -> 15345 */
	/* 23.98*256 = 6138.88 -> 6139 (6138 получилось бы для 23.976) */
	assert (modes [6].id == 6 && modes [6].hz == 6139);
	assert (modes [6].width == 1920 && modes [6].height == 1080);

	/* поиск: 23.976 в 1080p -> id 6 */
	assert (sfmodes_find (modes, n, 6138, 1920, 1080) == 6);
	/* 60 Гц в 1080p -> id 1 */
	assert (sfmodes_find (modes, n, 60 * 256, 1920, 1080) == 1);
	/* 24.0 Гц в 1080p -> id 7 (не 6: точное совпадение ближе) */
	assert (sfmodes_find (modes, n, 24 * 256, 1920, 1080) == 7);
	/* 50 Гц в 720p -> нет */
	assert (sfmodes_find (modes, n, 50 * 256, 1280, 720) == -1);
	/* текущая частота по id */
	assert (sfmodes_current_hz (modes, n, 2) == 50 * 256);
	/* поиск по одной частоте (для определения активного режима) */
	assert (sfmodes_find_hz (modes, n, 6138) == 6);
	assert (sfmodes_find_hz (modes, n, 50 * 256) == 2);
	assert (sfmodes_find_hz (modes, n, 100 * 256) == -1);	/* 100 Гц нет, допуск 0.125 Гц -> -1 */
}

/* Живой дамп стенда ATV14 (Amlogic S905X5M, Android 14), снятый ровно той
 * командой, что стоит в platform.sf_dump_cmd:
 *   adb -s 192.168.88.50:5555 shell \
 *     "dumpsys SurfaceFlinger | grep -E 'activeMode=|^ *[{]id='"
 * Именно её результат обязан разбираться: полный `dumpsys SurfaceFlinger` на
 * стенде — ~150 КБ, то есть больше буфера захвата (64 КБ), и таблица режимов
 * в него не влезала (живая приёмка ловила «режимов SurfaceFlinger: 0»).
 * id в таблице (15..28) принадлежат конкретному инстансу SurfaceFlinger: после
 * его рестарта нумерация сдвигается, и вместе с ней правятся ожидания ниже. */
static void test_live (void)
{
	static char buf [65536];
	read_file ("tests/fixtures/sf_modes_live.txt", buf, sizeof (buf));
	char *dump = buf;

	/* строка активного режима в дампе есть (иначе фильтр теряет activeMode) */
	assert (strstr (dump, "activeMode=59.94 Hz") != NULL);

	sf_mode_t modes [SF_MODE_MAX];
	int n = sfmodes_parse (dump, modes, SF_MODE_MAX);

	/* активная строка таблицей не считается: 14 режимов, лишнего нет */
	assert (n == 14);
	assert (sfmodes_current_hz (modes, n, 21) == 6139);	/* 23.98 Гц */

	/* 1080p23.976 -> id 21 (на стенде это id транзакции 1035 для видео) */
	assert (sfmodes_find (modes, n, 6138, 1920, 1080) == 21);
	/* 1080p50 -> id 17 */
	assert (sfmodes_find (modes, n, 12800, 1920, 1080) == 17);
	/* 1080p59.94 -> id 15 (режим возврата) */
	assert (sfmodes_find (modes, n, (int)(59.94 * 256 + 0.5), 1920, 1080) == 15);
	/* 1080p60 -> id 16: в таблице соседствуют 59.94 и 60.00 */
	assert (sfmodes_find (modes, n, 60 * 256, 1920, 1080) == 16);
	/* 50 Гц в 720p -> id 25, и он не путается с 1080p50 (id 17) */
	assert (sfmodes_find (modes, n, 12800, 1280, 720) == 25);
	/* 25 Гц есть только в 1080p -> id 20 */
	assert (sfmodes_find (modes, n, 25 * 256, 1920, 1080) == 20);
	/* 100 Гц в таблице отсутствует */
	assert (sfmodes_find (modes, n, 100 * 256, 1920, 1080) == -1);
}

/* Фильтр частот как в демоне (mode.blacklist.rates): список запрещённых частот. */
static int g_blocked [4];
static int g_blocked_n;
static bool block_list (int hz, void *ctx)
{
	(void)ctx;
	for (int i = 0; i < g_blocked_n; i++)
		if (hz == g_blocked [i])
			return false;
	return true;
}

/* Выбор режима с учётом кратных частот (mode.prefer.exact) и чёрного списка.
 * Кратность: 0 — предпочитать кратные (25 -> 50, 30 -> 60), 1 — точное
 * совпадение. Значения частот — 24.8 fixed point, как их считает fps_class. */
static void test_select_synthetic (void)
{
	static char buf [65536];
	read_file ("tests/fixtures/sf_modes.txt", buf, sizeof (buf));

	sf_mode_t modes [SF_MODE_MAX];
	int n = sfmodes_parse (buf, modes, SF_MODE_MAX);
	sf_choice_t ch;

	/* 25 fps: по умолчанию (prefer_exact=0) — кратные 50 Гц */
	assert (sfmodes_select (modes, n, 25 * 256, 1920, 1080, 0, NULL, NULL, &ch) == 0);
	assert (ch.id == 2 && ch.rate_n == 2 && ch.hz == 50 * 256);
	/* prefer_exact=1 — точные 25 Гц */
	assert (sfmodes_select (modes, n, 25 * 256, 1920, 1080, 1, NULL, NULL, &ch) == 0);
	assert (ch.id == 5 && ch.rate_n == 1);

	/* 29.97 fps (лидер группы 30): 59.94 Гц — точное удвоение, оно и выигрывает */
	assert (sfmodes_select (modes, n, 7672, 1920, 1080, 0, NULL, NULL, &ch) == 0);
	assert (ch.id == 0 && ch.rate_n == 2 && ch.hz == 15345);
	assert (sfmodes_select (modes, n, 7672, 1920, 1080, 1, NULL, NULL, &ch) == 0);
	assert (ch.id == 3 && ch.rate_n == 1);

	/* 60 fps (лидер 59.94) — без удвоения: 59.94 остаётся точным совпадением */
	assert (sfmodes_select (modes, n, 15345, 1920, 1080, 0, NULL, NULL, &ch) == 0);
	assert (ch.id == 0 && ch.rate_n == 1);

	/* пара 23.98/24.00: 23.976 -> дробный режим, 24.0 -> целый (вторичный критерий) */
	assert (sfmodes_select (modes, n, 6138, 1920, 1080, 0, NULL, NULL, &ch) == 0);
	assert (ch.id == 6 && ch.rate_n == 1);
	assert (sfmodes_select (modes, n, 24 * 256, 1920, 1080, 0, NULL, NULL, &ch) == 0);
	assert (ch.id == 7 && ch.rate_n == 1);

	/* чёрный список: 50 Гц запрещена -> для 25 fps остаётся точная 25 Гц */
	g_blocked [0] = 50 * 256; g_blocked_n = 1;
	assert (sfmodes_select (modes, n, 25 * 256, 1920, 1080, 0, block_list, NULL, &ch) == 0);
	assert (ch.id == 5 && ch.rate_n == 1);
	/* запрещена 25 Гц при prefer_exact=1 -> остаётся кратная 50 Гц */
	g_blocked [0] = 25 * 256;
	assert (sfmodes_select (modes, n, 25 * 256, 1920, 1080, 1, block_list, NULL, &ch) == 0);
	assert (ch.id == 2 && ch.rate_n == 2);

	/* 100 fps: ни точного режима, ни кратного (200 Гц нет) */
	assert (sfmodes_select (modes, n, 100 * 256, 1920, 1080, 0, NULL, NULL, &ch) == -1);
	/* 50 Гц ищем в 720p: в этой (синтетической) таблице 720p — только 59.94,
	 * то есть ни точного режима, ни кратного для 50 Гц там нет */
	assert (sfmodes_select (modes, n, 50 * 256, 1280, 720, 0, NULL, NULL, &ch) == -1);

	/* пустая таблица и нулевая частота — отказ, а не мусор в out */
	assert (sfmodes_select (modes, 0, 25 * 256, 1920, 1080, 0, NULL, NULL, &ch) == -1);
	assert (sfmodes_select (modes, n, 0, 1920, 1080, 0, NULL, NULL, &ch) == -1);

	/* запрещено всё подходящее (и точная 25, и кратная 50). При prefer_exact=0
	 * остаётся только пара 23.98/24.00 — кандидаты с ошибкой частоты ровно на
	 * границе допуска (delta == 11) и нулевым рейтингом; legacy такие не
	 * берёт, поэтому подходящего режима нет. При prefer_exact=1 они получают
	 * бонус за точность (12) и выбираются, а вторичный критерий берёт
	 * ближайший к 25 Гц — 24.00 (в legacy выбор зависел от порядка строк). */
	g_blocked [0] = 25 * 256; g_blocked [1] = 50 * 256; g_blocked_n = 2;
	assert (sfmodes_select (modes, n, 25 * 256, 1920, 1080, 0, block_list, NULL, &ch) == -1);
	assert (sfmodes_select (modes, n, 25 * 256, 1920, 1080, 1, block_list, NULL, &ch) == 0);
	assert (ch.id == 7 && ch.rate_n == 1);		/* 1080p24.00: край допуска */
}

/* То же на живом дампе стенда: 25 -> 50 Гц по умолчанию и 25 Гц при
 * prefer_exact=1; идентификаторы — из конкретного инстанса SurfaceFlinger. */
static void test_select_live (void)
{
	static char buf [65536];
	read_file ("tests/fixtures/sf_modes_live.txt", buf, sizeof (buf));

	sf_mode_t modes [SF_MODE_MAX];
	int n = sfmodes_parse (buf, modes, SF_MODE_MAX);
	sf_choice_t ch;

	assert (sfmodes_select (modes, n, 25 * 256, 1920, 1080, 0, NULL, NULL, &ch) == 0);
	assert (ch.id == 17 && ch.rate_n == 2);		/* 1080p50 */
	assert (sfmodes_select (modes, n, 25 * 256, 1920, 1080, 1, NULL, NULL, &ch) == 0);
	assert (ch.id == 20 && ch.rate_n == 1);		/* 1080p25 */

	assert (sfmodes_select (modes, n, 7672, 1920, 1080, 0, NULL, NULL, &ch) == 0);
	assert (ch.id == 15 && ch.rate_n == 2);		/* 1080p59.94 */
	assert (sfmodes_select (modes, n, 7672, 1920, 1080, 1, NULL, NULL, &ch) == 0);
	assert (ch.id == 18 && ch.rate_n == 1);		/* 1080p29.97 */

	assert (sfmodes_select (modes, n, 6138, 1920, 1080, 0, NULL, NULL, &ch) == 0);
	assert (ch.id == 21 && ch.rate_n == 1);		/* 1080p23.98 */
	assert (sfmodes_select (modes, n, 24 * 256, 1920, 1080, 1, NULL, NULL, &ch) == 0);
	assert (ch.id == 22 && ch.rate_n == 1);		/* 1080p24 */

	/* Чёрный список — по частоте, а не по конкретному режиму: 50 Гц есть и в
	 * 1080p (id 17), и в 720p (id 25), и запрет 50 Гц закрывает обе записи —
	 * обходного пути через другое разрешение нет. */
	g_blocked [0] = 50 * 256; g_blocked_n = 1;
	assert (sfmodes_select (modes, n, 25 * 256, 1920, 1080, 0, block_list, NULL, &ch) == 0);
	assert (ch.id == 20 && ch.rate_n == 1);		/* остаётся точная 1080p25 */
	assert (sfmodes_select (modes, n, 50 * 256, 1920, 1080, 0, block_list, NULL, &ch) == -1);
	assert (sfmodes_select (modes, n, 50 * 256, 1280, 720, 0, block_list, NULL, &ch) == -1);
	g_blocked_n = 0;
	/* а без запрета та же 50 Гц в 720p доступна (геометрия учитывается) */
	assert (sfmodes_select (modes, n, 50 * 256, 1280, 720, 0, NULL, NULL, &ch) == 0);
	assert (ch.id == 25 && ch.rate_n == 1);
}

/* Цель возврата режима после видео. preferred_id — из dumpsys display
 * (mUserPreferredModeId, пространство DisplayManager), cur_id — id текущего
 * режима из таблицы SurfaceFlinger. Таблица ниже повторяет стенд ATV14, где
 * нумерации DM и SF расходятся: живой баг — preferred=1 при таблице 30..43
 * и текущем режиме 30. */
static void test_restore_target (void)
{
	static const sf_mode_t sf [4] = {
		{ 30, 1920, 1080, 15345 },	/* 59.94 Гц */
		{ 36, 1920, 1080,  6139 },	/* 23.98 Гц */
		{ 37, 1920, 1080,  6144 },	/* 24.00 Гц */
		{ 43, 1280,  720, 12800 },	/* 50.00 Гц */
	};

	/* (а) preferred есть в таблице SF — возвращаемся в него */
	assert (sfmodes_restore_target (sf, 4, 37, 30) == 37);
	/* preferred совпадает с текущим режимом — тоже он */
	assert (sfmodes_restore_target (sf, 4, 30, 30) == 30);

	/* (б) живой баг: preferred=1 из нумерации DisplayManager в таблице SF
	 * отсутствует — возврат идёт в cur_id (30), а не пропускается */
	assert (sfmodes_restore_target (sf, 4, 1, 30) == 30);

	/* (в) ни preferred, ни cur нет в таблице — возвращать нечего */
	assert (sfmodes_restore_target (sf, 4, 1, 2) == -1);
	assert (sfmodes_restore_target (sf, 4, -1, -1) == -1);

	/* (г) пустая таблица: проверить id нечем — берём cur, а если и он
	 * неизвестен, остаётся preferred */
	assert (sfmodes_restore_target (sf, 0, 1, 30) == 30);
	assert (sfmodes_restore_target (NULL, 0, 1, 30) == 30);
	assert (sfmodes_restore_target (sf, 0, 1, -1) == 1);
	assert (sfmodes_restore_target (sf, 0, -1, -1) == -1);
}

int main (void)
{
	test_synthetic ();
	test_live ();
	test_select_synthetic ();
	test_select_live ();
	test_restore_target ();

	printf ("sfmodes: OK\n");
	return 0;
}
