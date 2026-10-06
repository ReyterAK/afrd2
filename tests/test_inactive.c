#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../cfgkeys.h"

/* Пометки «не действует на ...» в GUI (SettingsFragment.kInactiveOnAtv14 /
 * kInactiveOnLegacy) синхронизируются с таблицей ключей демона (cfgkeys.c)
 * вручную — и уже разъехались (cs.select был помечен недействующим на ATV14,
 * хотя действует там). Тест сверяет три источника:
 *   * cfgkeys.c        — через публичный cfgkeys_note();
 *   * preferences.xml  — какие настройки вообще показаны в GUI;
 *   * SettingsFragment — как они помечены.
 * Любое расхождение = пользователь видит выключенную рабочую настройку или
 * наоборот; тест падает, называя ключ. */

#define MAX_KEYS	128
#define KEY_LEN		128

static const char *k_prefs = "android/app/src/main/res/xml/preferences.xml";
static const char *k_fragment = "android/app/src/main/java/ru/cobra/zap/afrd/gui/SettingsFragment.java";

static char g_gui [MAX_KEYS][KEY_LEN];
static int g_gui_n;
static char g_atv14 [MAX_KEYS][KEY_LEN];
static int g_atv14_n;
static char g_legacy [MAX_KEYS][KEY_LEN];
static int g_legacy_n;

static void fail (const char *fmt, const char *arg)
{
	fprintf (stderr, "test_inactive: ");
	fprintf (stderr, fmt, arg);
	fprintf (stderr, "\n");
	exit (1);
}

static char *read_file (const char *path)
{
	FILE *f = fopen (path, "rb");
	if (!f)
	{
		fprintf (stderr, "test_inactive: %s не открывается\n", path);
		exit (2);
	}

	fseek (f, 0, SEEK_END);
	long n = ftell (f);
	fseek (f, 0, SEEK_SET);
	char *buf = malloc ((size_t) n + 1);
	if (!buf || (fread (buf, 1, (size_t) n, f) != (size_t) n))
	{
		fprintf (stderr, "test_inactive: %s не читается\n", path);
		exit (2);
	}
	buf [n] = 0;
	fclose (f);
	return buf;
}

/* Убрать XML-комментарии: в них ключи упоминаются как пояснения, и считать их
 * строками GUI нельзя — иначе тест даёт ложную зелень на удалённой настройке и
 * ложное красное на закомментированной. */
static void strip_xml_comments (char *s)
{
	char *p;
	while ((p = strstr (s, "<!--")) != NULL)
	{
		char *e = strstr (p, "-->");
		if (!e)
			break;	/* незакрытый комментарий — оставляем: разбор упадёт заметно */

		memmove (p, e + 3, strlen (e + 3) + 1);
	}
}

/* То же для Java-исходника: в комментариях массивы цитируются примерами. */
static void strip_java_comments (char *s)
{
	char *p;
	while ((p = strstr (s, "/*")) != NULL)
	{
		char *e = strstr (p, "*/");
		if (!e)
			break;

		memmove (p, e + 2, strlen (e + 2) + 1);
	}

	while ((p = strstr (s, "//")) != NULL)
	{
		char *e = strchr (p, '\n');
		if (!e)
		{
			*p = 0;
			break;
		}

		memmove (p, e, strlen (e) + 1);
	}
}

static void add_key (char out [][KEY_LEN], int *count, const char *text, size_t len)
{
	if ((len == 0) || (len >= KEY_LEN) || (*count >= MAX_KEYS))
	{
		fprintf (stderr, "test_inactive: ключ неразумной длины или их слишком много\n");
		exit (2);
	}

	memcpy (out [*count], text, len);
	out [*count][len] = 0;
	(*count)++;
}

/* Ключи настроек из preferences.xml: все элементы, кроме групп
 * PreferenceScreen (у них android:key — идентификатор группы, а не настройки). */
static void parse_gui (const char *xml)
{
	const char *attr = "android:key=\"";
	const char *p = xml;

	while ((p = strstr (p, attr)) != NULL)
	{
		/* начало тега — ближайший '<' слева */
		const char *tag = p;
		while ((tag > xml) && (tag [-1] != '<'))
			tag--;

		size_t tag_len = strcspn (tag, " \t\r\n/>");
		const char *val = p + strlen (attr);
		const char *end = strchr (val, '"');
		if (!end)
		{
			fprintf (stderr, "test_inactive: незакрытый android:key в %s\n", k_prefs);
			exit (2);
		}

		if ((tag_len != strlen ("PreferenceScreen")) ||
		    (strncmp (tag, "PreferenceScreen", tag_len) != 0))
			add_key (g_gui, &g_gui_n, val, (size_t) (end - val));

		p = end;
	}
}

/* Строковый массив из SettingsFragment.java: от "String [] <имя>" до "};". */
static int parse_array (const char *src, const char *name, char out [][KEY_LEN])
{
	char decl [KEY_LEN];
	snprintf (decl, sizeof (decl), "String [] %s", name);

	const char *p = strstr (src, decl);
	if (!p)
	{
		fprintf (stderr, "test_inactive: в %s нет массива %s\n", k_fragment, name);
		exit (2);
	}

	p = strchr (p, '{');
	const char *end = p ? strstr (p, "};") : NULL;
	if (!end)
	{
		fprintf (stderr, "test_inactive: не найден конец массива %s\n", name);
		exit (2);
	}

	int n = 0;
	while (++p < end)
	{
		if (*p != '"')
			continue;

		const char *e = strchr (p + 1, '"');
		if (!e || (e > end))
			break;

		add_key (out, &n, p + 1, (size_t) (e - p - 1));
		p = e;
	}

	return n;
}

/* Ключ настройки в GUI → ключ в ini (то же правило, что в Config.load (SharedPreferences)). */
static void key_to_ini (const char *pref, char *ini)
{
	size_t i = 0;
	for (; pref [i] && (i + 1 < KEY_LEN); i++)
		ini [i] = (pref [i] == '_') ? '.' : pref [i];
	ini [i] = 0;
}

static int in_list (char list [][KEY_LEN], int n, const char *key)
{
	for (int i = 0; i < n; i++)
		if (strcmp (list [i], key) == 0)
			return 1;

	return 0;
}

static int is_unknown (const char *note)
{
	return note && strstr (note, "неизвестный");
}

int main (void)
{
	char *xml = read_file (k_prefs);
	char *java = read_file (k_fragment);

	strip_xml_comments (xml);
	strip_java_comments (java);

	parse_gui (xml);
	g_atv14_n = parse_array (java, "kInactiveOnAtv14", g_atv14);
	g_legacy_n = parse_array (java, "kInactiveOnLegacy", g_legacy);

	/* Санитарные проверки самого разбора: «ноль найденного» не должен
	 * выглядеть как согласованность. */
	if (g_gui_n < 25)
		fail ("найдено подозрительно мало строк GUI: %s", "");
	if ((g_atv14_n < 5) || (g_legacy_n < 10))
		fail ("найдено подозрительно мало пометок в списках: %s", "");

	for (int i = 0; i < g_gui_n; i++)
	{
		char ini [KEY_LEN];
		key_to_ini (g_gui [i], ini);

		const char *a14 = cfgkeys_note (ini, CFGKEY_ATV14);
		const char *leg = cfgkeys_note (ini, CFGKEY_LEGACY);
		int marked_a14 = in_list (g_atv14, g_atv14_n, g_gui [i]);
		int marked_leg = in_list (g_legacy, g_legacy_n, g_gui [i]);

		if (is_unknown (a14) || is_unknown (leg))
			fail ("строка GUI есть, а ключа нет в cfgkeys.c: %s", g_gui [i]);

		int only_legacy = (a14 != NULL) && (leg == NULL);	/* ключ старого пути */
		int only_a14 = (a14 == NULL) && (leg != NULL);		/* ключ нового пути */

		if (only_legacy && !marked_a14)
			fail ("не действует на Android 10-14, но в GUI не помечена: %s", g_gui [i]);
		if (only_a14 && !marked_leg)
			fail ("не действует на Android 6-9, но в GUI не помечена: %s", g_gui [i]);
		if (!only_legacy && marked_a14)
			fail ("помечена «не действует на Android 10-14», хотя ключ действует: %s", g_gui [i]);
		if (!only_a14 && marked_leg)
			fail ("помечена «не действует на Android 6-9», хотя ключ действует: %s", g_gui [i]);
	}

	/* Обратная проверка: в списках не должно быть ключей, которых нет в GUI
	 * (не-GUI ключ вроде mode.path пометки не получает никогда). */
	for (int i = 0; i < g_atv14_n; i++)
		if (!in_list (g_gui, g_gui_n, g_atv14 [i]))
			fail ("в kInactiveOnAtv14 есть ключ, которого нет в preferences.xml: %s", g_atv14 [i]);

	for (int i = 0; i < g_legacy_n; i++)
		if (!in_list (g_gui, g_gui_n, g_legacy [i]))
			fail ("в kInactiveOnLegacy есть ключ, которого нет в preferences.xml: %s", g_legacy [i]);

	/* Списки не пересекаются: строка не может быть помечена двумя путями сразу. */
	for (int i = 0; i < g_atv14_n; i++)
		if (in_list (g_legacy, g_legacy_n, g_atv14 [i]))
			fail ("ключ помечен недействующим на обоих путях: %s", g_atv14 [i]);

	free (xml);
	free (java);

	printf ("inactive: OK (%d строк GUI, %d на ATV14, %d на legacy)\n",
	        g_gui_n, g_atv14_n, g_legacy_n);
	return 0;
}
