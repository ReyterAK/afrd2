/*
 * Разбор «пакета на переднем плане» из вывода
 * `dumpsys activity activities | grep -m1 ResumedActivity`.
 * Фикстуры — дословные строки со стендов: .50 (ATV14) и .76 (Android 9).
 */
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
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

/* Длинное имя пакета: система принимает такие (проверено установкой APK с
 * именем 171 символа), поэтому с буфером штатного размера оно должно
 * разбираться целиком, а не теряться. */
static const char *LONG_PKG =
	"com.reyter.xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx."
	"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.probe";

static void test_long_name (void)
{
	char pkg [PKG_NAME_LEN];

	assert (strlen (LONG_PKG) > 64);

	char line [512];
	snprintf (line, sizeof (line),
	          "    topResumedActivity=ActivityRecord{abc123 u0 %s/.ProbeActivity t42}", LONG_PKG);

	assert (pkgdet_parse (line, pkg, sizeof (pkg)));
	assert (!strcmp (pkg, LONG_PKG));

	/* старый размер буфера такое имя не принимает — граница, ради которой
	 * размер и поднят (лучше ложный отказ, чем усечённое имя) */
	char small [64];
	assert (!pkgdet_parse (line, small, sizeof (small)));
}

/* Наши буферы имён пакетов в демоне обязаны вмещать любое имя, которое примет
 * система (PKG_NAME_LEN). Проверить это «в рантайме» нечем: буфера статические
 * (g_cur_pkg, video_pkg, списки mode.min.hz.list и fps.correct.*), а усечение
 * видно только на стенде. Поэтому контракт пинится разбором исходников: любое
 * объявление вида `pkg [<число>]` с числом меньше PKG_NAME_LEN — регресс,
 * из-за которого настройки для плеера с длинным именем молча не работают. */
static int check_pkg_buffers (const char *path)
{
	FILE *f = fopen (path, "r");
	if (!f)
	{
		fprintf (stderr, "test_pkgdet: %s не открывается\n", path);
		return -1;
	}

	char line [512];
	int bad = 0;
	while (fgets (line, sizeof (line), f))
	{
		/* размер, заданный своим #define, обязан выводиться из PKG_NAME_LEN */
		if (strstr (line, "#define") && strstr (line, "PKG_LEN") && !strstr (line, "PKG_NAME_LEN"))
		{
			fprintf (stderr, "test_pkgdet: %s: размер не выведен из PKG_NAME_LEN: %s", path, line);
			bad++;
		}

		const char *p = line;
		while ((p = strstr (p, "pkg")) != NULL)
		{
			/* имя целиком: после «pkg» не буква/цифра/подчёркивание
			 * (отсекает pkgdet_*), а слева границы нет намеренно: буфера
			 * зовутся g_cur_pkg, video_pkg, MODE_MIN_HZ_PKG_LEN */
			char after = p [3];
			if (isalnum ((unsigned char) after) || (after == '_'))
			{
				p += 3;
				continue;
			}

			const char *br = p + 3;
			while (*br == ' ')
				br++;
			if (*br != '[')
			{
				p += 3;
				continue;
			}

			int n = atoi (br + 1);
			if ((n > 0) && (n < PKG_NAME_LEN))
			{
				fprintf (stderr, "test_pkgdet: %s: буфер имени пакета %d < %d: %s",
				         path, n, PKG_NAME_LEN, line);
				bad++;
			}

			p = br;
		}
	}

	fclose (f);
	return bad;
}

int main (void)
{
	test_format ();
	test_truncated ();
	test_garbage ();
	test_short_buffer ();
	test_long_name ();
	assert (check_pkg_buffers ("afrd.c") == 0);
	assert (check_pkg_buffers ("fps_class.c") == 0);
	printf ("test_pkgdet: OK\n");
	return 0;
}
