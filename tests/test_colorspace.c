/*
 * Host-тест выбора цветового пространства: правило задаёт формат, а глубина
 * берётся текущая, поэтому демон должен сверять с EDID итоговую комбинацию
 * «формат+глубина», а не только формат из правила.
 *
 * Фикстуры — реальные dc_cap стендов: ATV14 (.50) перечисляет 4:2:2 только в
 * 12 битах, ATV9 (.48) — в 8; 4:2:0 не заявлен ни там, ни там.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../colorspace.h"

/* upstream-символы, которых нет в host-сборке (см. tests/stub.c) */
const char *spaces = " \t\r\n";

static const char *fixture_list;
static const char *fixture_cur;
static const char *fixture_select;
static char written [128];

static const char *kList = "list";
static const char *kCs = "cs";

char *sysfs_read (const char *path)
{
	if (!strcmp (path, kList))
		return fixture_list ? strdup (fixture_list) : NULL;
	if (!strcmp (path, kCs))
		return fixture_cur ? strdup (fixture_cur) : NULL;

	return NULL;
}

int sysfs_write (const char *path, const char *value)
{
	(void) path;
	snprintf (written, sizeof (written), "%s", value);
	return 0;
}

int exec_capture (const char *cmd, char *out, size_t out_sz)
{
	(void) cmd;
	(void) out;
	(void) out_sz;
	return -1;
}

const char *cfg_get_str (const char *name, const char *def)
{
	(void) def;
	if (!strcmp (name, "cs.list.path"))
		return kList;
	if (!strcmp (name, "cs.path"))
		return kCs;
	if (!strcmp (name, "cs.select"))
		return fixture_select;

	return NULL;
}

void trace (int level, const char *format, ...)
{
	(void) level;
	(void) format;
}

static void setup (const char *list, const char *cur, const char *select)
{
	colorspace_fini ();
	fixture_list = list;
	fixture_cur = cur;
	fixture_select = select;
	written [0] = 0;

	colorspace_init ();
	assert (colorspace_refresh ());
}

static void expect_write (const char *mode, const char *value)
{
	written [0] = 0;
	assert (colorspace_apply (mode));
	assert (strcmp (written, value) == 0);
}

static const char *kAtv14 =
	"444,12bit\n444,10bit\n444,8bit\n422,12bit\nrgb,12bit\nrgb,10bit\nrgb,8bit\n";
static const char *kAtv9 =
	"444,8bit\n422,8bit\nrgb,8bit\n";

int main (void)
{
	/* ATV14: 4:2:2 только в 12 битах, система после смены режима оставила
	 * 8 бит. Правило `.*=422` должно дать 422,12bit, а не 422,8bit. */
	setup (kAtv14, "444,8bit", "2160p[4-9].*=420 .*=422");
	expect_write ("1080p24hz", "422,12bit,full");

	/* Если текущая глубина уже заявлена для формата — её и оставляем. */
	setup (kAtv14, "444,12bit", "2160p[4-9].*=420 .*=422");
	expect_write ("1080p24hz", "422,12bit,full");

	/* ATV9: 4:2:2 есть в 8 битах — глубину не трогаем. */
	setup (kAtv9, "444,8bit", "2160p[4-9].*=420 .*=422");
	expect_write ("1080p24hz", "422,8bit,full");

	/* Несколько глубин: берём ближайшую к текущей, а не максимальную. */
	setup ("444,8bit\n444,10bit\n422,10bit\n422,12bit\n", "444,8bit",
		".*=422");
	expect_write ("1080p24hz", "422,10bit,full");

	/* Глубина задана в самом правиле — она и выигрывает. */
	setup (kAtv14, "444,8bit", ".*=422,12bit");
	expect_write ("1080p24hz", "422,12bit,full");

	/* Явно заданную комбинацию, которой у ТВ нет, не подменяем глубиной:
	 * правило не применяется, остаётся текущее значение. */
	setup (kAtv14, "444,8bit", ".*=422,8bit");
	expect_write ("1080p24hz", "");

	/* Формата у ТВ нет вовсе: ничего не пишем (раньше это было «молча
	 * пропустить», так и осталось — но и глубина не подставляется). */
	setup (kAtv14, "444,8bit", "2160p[4-9].*=420");
	expect_write ("2160p50hz", "");
	written [0] = 0;
	assert (colorspace_apply ("1080p24hz"));
	assert (written [0] == 0);

	colorspace_fini ();
	printf ("colorspace: OK\n");
	return 0;
}
