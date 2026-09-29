#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sfmodes.h"

static char *read_file (const char *path)
{
	FILE *f = fopen (path, "rb");
	assert (f);
	static char buf [65536];
	size_t n = fread (buf, 1, sizeof (buf) - 1, f);
	buf [n] = 0;
	fclose (f);
	return buf;
}

int main (void)
{
	char *dump = read_file ("tests/fixtures/sf_modes.txt");
	sf_mode_t modes [SF_MODE_MAX];
	int n = sfmodes_parse (dump, modes, SF_MODE_MAX);

	assert (n == 9);
	assert (modes [0].id == 0 && modes [0].hz == 59 * 256 + 241); /* 59.94 -> 15345 */
	assert (modes [6].id == 6 && modes [6].hz == 6138);           /* 23.98 */
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

	printf ("sfmodes: OK\n");
	return 0;
}
