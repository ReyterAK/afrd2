/*
 * Разбор «пакета на переднем плане» из вывода
 * `dumpsys activity activities | grep -m1 ResumedActivity`.
 * Фикстуры — дословные строки со стендов: .50 (ATV14) и .76 (Android 9).
 */
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "../pkgdet.h"

/* Реальные строки: ATV14 (X88 Pro X5M) и Android 9 (UGOBO AM6). */
static const char *ATV14_LINE =
	"    topResumedActivity=ActivityRecord{d34eeb2 u0 studio.scillarium.ottnavigator/.MainActivity t1590}";
static const char *ATV9_LINE =
	"  ResumedActivity: ActivityRecord{b4570ac u0 com.google.android.tvlauncher/.BootHelperEmptyActivity t3427}";

/* На ATV14 в дампе есть оба поля, и grep отдаёт первым topResumedActivity;
 * проверяем, что при обоих строках результат тот же. */
static const char *ATV14_BOTH =
	"    topResumedActivity=ActivityRecord{d34eeb2 u0 studio.scillarium.ottnavigator/.MainActivity t1590}\n"
	"  ResumedActivity: ActivityRecord{d34eeb2 u0 studio.scillarium.ottnavigator/.MainActivity t1590}\n";

static void test_format (void)
{
	char pkg [64];

	assert (pkgdet_parse (ATV14_LINE, pkg, sizeof (pkg)));
	assert (!strcmp (pkg, "studio.scillarium.ottnavigator"));

	assert (pkgdet_parse (ATV9_LINE, pkg, sizeof (pkg)));
	assert (!strcmp (pkg, "com.google.android.tvlauncher"));

	assert (pkgdet_parse (ATV14_BOTH, pkg, sizeof (pkg)));
	assert (!strcmp (pkg, "studio.scillarium.ottnavigator"));
}

/* Обрезанная запись: пакет отделяется ' ' или '}', закрывающей скобки может не быть. */
static void test_truncated (void)
{
	char pkg [64];

	assert (pkgdet_parse ("  ResumedActivity: ActivityRecord{b4570ac u0 com.example.player/.Main t1",
	                      pkg, sizeof (pkg)));
	assert (!strcmp (pkg, "com.example.player"));
}

/* Мусор и «нетактивности» — false и пустая строка, не мусор в имени пакета. */
static void test_garbage (void)
{
	char pkg [64];

	assert (!pkgdet_parse ("    topResumedActivity=null", pkg, sizeof (pkg)));
	assert (!pkg [0]);

	assert (!pkgdet_parse ("  ResumedActivity: ActivityRecord{b4570ac u0 }", pkg, sizeof (pkg)));
	assert (!pkg [0]);

	assert (!pkgdet_parse ("No activities", pkg, sizeof (pkg)));
	assert (!pkg [0]);

	assert (!pkgdet_parse ("", pkg, sizeof (pkg)));
	assert (!pkg [0]);
}

/* Буфера не хватает — false, а не усечённое имя (иначе пакет не совпадёт
 * с элементом switch.skip.pkg и AFR включится плееру, которому он мешает). */
static void test_short_buffer (void)
{
	char pkg [8];
	assert (!pkgdet_parse (ATV9_LINE, pkg, sizeof (pkg)));
}

int main (void)
{
	test_format ();
	test_truncated ();
	test_garbage ();
	test_short_buffer ();
	printf ("test_pkgdet: OK\n");
	return 0;
}
