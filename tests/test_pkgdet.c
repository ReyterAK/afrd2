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
 * и живут в разных местах, а усечение видно только на стенде. Поэтому контракт
 * пинится разбором исходников — от мест вызова, а не по имени буфера: имя может
 * быть любым (на этом уже обожглись: буфер pkg_name() звался buf [64], и
 * проверка «в имени есть pkg» его не увидела). Для каждого вызова
 * pkg_probe()/pkgdet_parse() находим буфер-аргумент и его объявление и требуем
 * размер не меньше PKG_NAME_LEN. */
static char *read_all (const char *path)
{
	FILE *f = fopen (path, "rb");
	if (!f)
	{
		fprintf (stderr, "test_pkgdet: %s не открывается\n", path);
		return NULL;
	}

	fseek (f, 0, SEEK_END);
	long n = ftell (f);
	fseek (f, 0, SEEK_SET);
	char *buf = malloc ((size_t) n + 1);
	if (!buf || (fread (buf, 1, (size_t) n, f) != (size_t) n))
	{
		free (buf);
		fclose (f);
		return NULL;
	}

	buf [n] = 0;
	fclose (f);
	return buf;
}

/* Идентификатор второго аргумента вызова: "pkg_probe (plat, g_cur_pkg, sizeof (...))"
 * -> "g_cur_pkg"; для параметра-указателя объявления в файле нет — пропускаем. */
static int arg_identifier (const char *open_paren, char *out, size_t out_sz)
{
	const char *comma = strchr (open_paren, ',');
	if (!comma)
		return -1;

	const char *p = comma + 1;
	while ((*p == ' ') || (*p == '\t'))
		p++;

	size_t i = 0;
	while (isalnum ((unsigned char) *p) || (*p == '_'))
	{
		if (i + 1 >= out_sz)
			return -1;
		out [i++] = *p++;
	}

	out [i] = 0;
	return i ? 0 : -1;
}

static int check_call_buffers (const char *path)
{
	char *src = read_all (path);
	if (!src)
		return -1;

	int bad = 0;
	const char *calls [] = { "pkg_probe (", "pkgdet_parse (" };

	for (unsigned c = 0; c < 2; c++)
	{
		const char *p = src;
		while ((p = strstr (p, calls [c])) != NULL)
		{
			char id [64];
			if (arg_identifier (p + strlen (calls [c]) - 1, id, sizeof (id)) != 0)
			{
				p += strlen (calls [c]);
				continue;
			}

			/* Объявление буфера в этом же файле: "char <id> [<N>]" */
			char decl [128];
			snprintf (decl, sizeof (decl), "char %s [", id);
			const char *d = src;
			while ((d = strstr (d, decl)) != NULL)
			{
				int n = atoi (d + strlen (decl));
				if ((n > 0) && (n < PKG_NAME_LEN))
				{
					fprintf (stderr, "test_pkgdet: %s: буфер %s в вызове %s меньше PKG_NAME_LEN (%d): %d\n",
					         path, id, calls [c], PKG_NAME_LEN, n);
					bad++;
				}

				d += strlen (decl);
			}

			p += strlen (calls [c]);
		}
	}

	/* размер, заданный своим #define, обязан выводиться из PKG_NAME_LEN */
	const char *p = src;
	while ((p = strstr (p, "#define")) != NULL)
	{
		const char *eol = strchr (p, '\n');
		if (!eol)
			break;

		size_t len = (size_t)(eol - p);
		if ((len < sizeof (char) * 512))
		{
			char line [512];
			memcpy (line, p, len);
			line [len] = 0;
			if (strstr (line, "PKG_LEN") && !strstr (line, "PKG_NAME_LEN"))
			{
				fprintf (stderr, "test_pkgdet: %s: размер не выведен из PKG_NAME_LEN: %s\n", path, line);
				bad++;
			}
		}

		p = eol;
	}

	free (src);
	return bad;
}

int main (void)
{
	test_format ();
	test_truncated ();
	test_garbage ();
	test_short_buffer ();
	test_long_name ();
	assert (check_call_buffers ("afrd.c") == 0);
	assert (check_call_buffers ("fps_class.c") == 0);
	printf ("test_pkgdet: OK\n");
	return 0;
}
