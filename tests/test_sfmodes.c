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

int main (void)
{
	test_synthetic ();
	test_live ();

	printf ("sfmodes: OK\n");
	return 0;
}
