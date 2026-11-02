/*
 * Host-тест выбора цветового пространства: правило задаёт формат, а глубина
 * берётся текущая, поэтому демон должен сверять с EDID итоговую комбинацию
 * «формат+глубина», а не только формат из правила.
 *
 * Фикстуры — реальные dc_cap стендов: ATV14 (.50) перечисляет 4:2:2 только в
 * 12 битах, ATV9 (.48) — в 8; 4:2:0 не заявлен ни там, ни там.
 *
 * Вторая часть — возврат профиля (cs.restore): демон запоминает значение до
 * первого изменения и пишет его обратно на выходе; на ATV14 и чтение, и запись
 * идут вендорским клиентом, потому что узел attr там не отражает состояние.
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
static int fixture_restore;
static char written [128];
/* Текущее значение узла cs.path: запись меняет его так же, как на устройстве. */
static char node_cur [64];

/* «Вендорский клиент»: своё состояние и учёт вызовов. */
static char vendor_cur [64];
static int vendor_sets;

static const char *kList = "list";
static const char *kCs = "cs";

char *sysfs_read (const char *path)
{
	if (!strcmp (path, kList))
		return fixture_list ? strdup (fixture_list) : NULL;
	if (!strcmp (path, kCs))
		return node_cur [0] ? strdup (node_cur) : NULL;

	return NULL;
}

int sysfs_write (const char *path, const char *value)
{
	(void) path;
	snprintf (written, sizeof (written), "%s", value);
	snprintf (node_cur, sizeof (node_cur), "%s", value);
	return 0;
}

int exec_capture (const char *cmd, char *out, size_t out_sz)
{
	/* "get ..." отдаёт текущее значение клиента, "set ... 'VALUE'" — пишет */
	if (!strncmp (cmd, "get", 3))
	{
		snprintf (out, out_sz, "%s\n", vendor_cur);
		return 0;
	}

	const char *q = strrchr (cmd, '\'');
	if (!q)
		return -1;

	const char *p = q;
	while ((p > cmd) && (p [-1] != '\''))
		p--;

	char val [64];
	size_t n = (size_t) (q - p);
	if (n >= sizeof (val))
		return -1;
	memcpy (val, p, n);
	val [n] = 0;

	snprintf (vendor_cur, sizeof (vendor_cur), "%s", val);
	vendor_sets++;
	snprintf (out, out_sz, "set HDMI Color ATTR to %s, current value:%s", val, val);
	return 0;
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

int cfg_get_int (const char *name, int def)
{
	if (!strcmp (name, "cs.restore"))
		return fixture_restore;

	return def;
}

void trace (int level, const char *format, ...)
{
	(void) level;
	(void) format;
}

static void setup (const char *list, const char *cur, const char *select)
{
	colorspace_fini ();
	colorspace_set_apply_cmd (NULL);
	colorspace_set_get_cmd (NULL);

	fixture_list = list;
	fixture_cur = cur;
	fixture_select = select;
	fixture_restore = 1;
	written [0] = 0;
	snprintf (node_cur, sizeof (node_cur), "%s", cur);
	vendor_cur [0] = 0;
	vendor_sets = 0;

	colorspace_init ();
	assert (colorspace_refresh ());
}

/* То же, но чтение и запись — вендорским клиентом (путь ATV14). */
static void setup_vendor (const char *list, const char *cur, const char *select)
{
	setup (list, cur, select);
	colorspace_set_apply_cmd ("set");
	colorspace_set_get_cmd ("get");
	snprintf (vendor_cur, sizeof (vendor_cur), "%s", cur);
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

	/* cs.restore: профиль до первого изменения запоминается один раз и
	 * возвращается на выходе, повторный вызов — no-op. */
	setup (kAtv14, "rgb,8bit", "2160p[4-9].*=420 .*=422");
	expect_write ("1080p24hz", "422,12bit,full");
	/* система после другой смены режима поставила другое значение — оно не
	 * должно затереть запомненное */
	snprintf (node_cur, sizeof (node_cur), "444,8bit");
	expect_write ("1080p50hz", "422,12bit,full");
	written [0] = 0;
	assert (colorspace_restore ());
	assert (strcmp (written, "rgb,8bit") == 0);
	written [0] = 0;
	assert (!colorspace_restore ());
	assert (written [0] == 0);

	/* cs.restore=0: профиль не запоминается, возвращать нечего. */
	setup (kAtv14, "444,8bit", ".*=422");
	fixture_restore = 0;
	colorspace_fini ();
	colorspace_init ();
	assert (colorspace_refresh ());
	expect_write ("1080p24hz", "422,12bit,full");
	assert (!colorspace_restore ());

	/* Возврат режима: правила не применяются вовсе (colorspace_hold), чтобы
	 * широкое правило не перебило восстанавливаемый профиль. */
	setup (kAtv14, "444,8bit", ".*=422");
	expect_write ("1080p24hz", "422,12bit,full");
	colorspace_hold (true);
	written [0] = 0;
	assert (!colorspace_apply ("1080p50hz"));
	assert (written [0] == 0);
	colorspace_hold (false);
	written [0] = 0;
	assert (colorspace_restore ());
	assert (strcmp (written, "444,8bit") == 0);

	/* Путь ATV14: текущее значение читается клиентом (узел врёт) — запись
	 * идёт клиентом, возврат тоже. */
	setup_vendor (kAtv14, "444,12bit", ".*=422");
	assert (colorspace_apply ("1080p24hz"));
	assert (vendor_sets == 1);
	assert (strcmp (vendor_cur, "422,12bit,full") == 0);
	assert (colorspace_restore ());
	assert (vendor_sets == 2);
	assert (strcmp (vendor_cur, "444,12bit") == 0);

	/* Если клиент уже показывает возвращённое значение — лишней записи нет. */
	setup_vendor (kAtv14, "444,12bit", ".*=422");
	assert (colorspace_apply ("1080p24hz"));
	snprintf (vendor_cur, sizeof (vendor_cur), "444,12bit,full");
	vendor_sets = 0;
	assert (colorspace_restore ());
	assert (vendor_sets == 0);

	colorspace_fini ();
	printf ("colorspace: OK\n");
	return 0;
}
