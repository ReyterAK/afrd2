/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

#include "afrd.h"
#include <string.h>
#include "colorspace.h"
#include "exec.h"
#include <regex.h>
#include <string.h>

/* blatantly borrowed from include/linux/amlogic/media/vout/hdmi_tx/hdmi_common.h */
enum hdmi_color_space {
	COLORSPACE_RGB444 = 0,
	COLORSPACE_YUV422 = 1,
	COLORSPACE_YUV444 = 2,
	COLORSPACE_YUV420 = 3,
	COLORSPACE_RESERVED,
};

enum hdmi_color_depth {
	COLORDEPTH_24B = 4,
	COLORDEPTH_30B = 5,
	COLORDEPTH_36B = 6,
	COLORDEPTH_48B = 7,
	COLORDEPTH_RESERVED,
};

enum hdmi_color_range {
	COLORRANGE_LIM,
	COLORRANGE_FUL,
	COLORRANGE_RESERVED,
};

struct parse_list {
	int val;
	const char *name;
};

static struct parse_list parse_cd [] = {
        {COLORDEPTH_24B, "8bit"},
        {COLORDEPTH_30B, "10bit"},
        {COLORDEPTH_36B, "12bit"},
        {COLORDEPTH_48B, "16bit"},
        {COLORDEPTH_RESERVED, NULL},
};

static struct parse_list parse_cs [] = {
        {COLORSPACE_RGB444, "rgb"},
        {COLORSPACE_YUV422, "422"},
        {COLORSPACE_YUV444, "444"},
        {COLORSPACE_YUV420, "420"},
        {COLORSPACE_RESERVED, NULL},
};

static struct parse_list parse_cr [] = {
        {COLORRANGE_LIM, "limit"},
        {COLORRANGE_FUL, "full"},
        {COLORRANGE_RESERVED, NULL},
};

/* this attribute contains a list of supported color spaces */
const char *g_cs_list_path;
/* this attribute contains the current color space */
const char *g_cs_path;

struct colorspace_t
{
	/* color space */
	/*enum hdmi_color_space*/int cs;
	/* color depth */
	/*enum hdmi_color_depth*/int cd;
	/* color range */
	/*enum hdmi_color_range*/int cr;
};

/* A list of color spaces supported by display */
static struct colorspace_t g_cs_supported [32];
static int g_cs_supported_size = 0;

struct cs_filter_t
{
	/* Regular expression for video mode name */
	regex_t rex;
	/* Color Space details */
	struct colorspace_t cs;
};
/* regex -> colorspace filters */
static struct cs_filter_t g_cs_filter [32];
/* Number of filters in the array */
static int g_cs_filter_size = 0;
/* Default color space */
static char *g_cs_default = 0;

/* Override colorspace via API */
static struct colorspace_t g_override_cs;

/* Префикс команды вендорского сервиса (Android 10+); NULL — писать в sysfs. */
static const char *g_cs_apply_cmd;

void colorspace_set_apply_cmd (const char *cmd_prefix)
{
	g_cs_apply_cmd = (cmd_prefix && cmd_prefix [0]) ? cmd_prefix : NULL;
}

/* Чем читать текущее значение. На ATV14 узел attr не отражает то, что
 * выставил вендорский клиент (проверено 02.11 на .50: запись 422,12bit, а в
 * узле остаётся 444,12bit), поэтому там читаем клиентом (-g). NULL — sysfs. */
static const char *g_cs_get_cmd;

void colorspace_set_get_cmd (const char *cmd)
{
	g_cs_get_cmd = (cmd && cmd [0]) ? cmd : NULL;
}

/* Профиль, который был до первого изменения в текущем сеансе AFR: его
 * возвращаем, когда режим возвращается (cs.restore). */
static char *g_cs_before;
/* cs.restore: 1 — возвращать профиль на выходе */
static int g_cs_restore = 1;
/* не применять правила: идёт возврат режима, за ним последует восстановление */
static bool g_cs_hold;
static bool g_override_cs_enabled = false;

static bool parse_str (const char *str, int *val, struct parse_list *list)
{
	int i;
	for (i = 0; list [i].name; i++)
		if (!strcmp (str, list [i].name)) {
			*val = list [i].val;
			return true;
		}

	return false;
}

static bool colorspace_parse (char *filt, struct colorspace_t *cs, bool reserved)
{
	if (reserved) {
		cs->cs = COLORSPACE_RESERVED;
		cs->cd = COLORDEPTH_RESERVED;
		cs->cr = COLORRANGE_RESERVED;
	}

	if (!filt)
		return false;

	char *cur_r, *cur, *tokens = filt;
	while ((cur = strtok_r (tokens, ",", &cur_r)) != NULL)
	{
		tokens = NULL;

		if (!parse_str (cur, &cs->cs, parse_cs) &&
		    !parse_str (cur, &cs->cd, parse_cd) &&
		    !parse_str (cur, &cs->cr, parse_cr))
			return false;
	}

	return true;
}

static const char *colorspace_str (struct colorspace_t *cs)
{
	static char tmp [32];
	int len = 0;

	int i;

	for (i = 0; parse_cs [i].name; i++)
		if (cs->cs == parse_cs [i].val) {
			len += snprintf (tmp + len, sizeof (tmp) - len - 1, "%s",
				parse_cs [i].name);
			break;
		}

	for (i = 0; parse_cd [i].name; i++)
		if (cs->cd == parse_cd [i].val) {
			len += snprintf (tmp + len, sizeof (tmp) - len - 1, "%s%s",
				len ? "," : "", parse_cd [i].name);
			break;
		}

	for (i = 0; parse_cr [i].name; i++)
		if (cs->cr == parse_cr [i].val) {
			len += snprintf (tmp + len, sizeof (tmp) - len - 1, "%s%s",
				len ? "," : "", parse_cr [i].name);
			break;
		}

	tmp [len] = 0;
	return tmp;
}

/* Разбор строки без её изменения: colorspace_parse() режет строку на токены
 * (strtok_r), а значение из конфига/узла может понадобиться ещё раз. */
static void colorspace_parse_copy (const char *str, struct colorspace_t *cs)
{
	char buf [64];

	if (!str)
		return;

	snprintf (buf, sizeof (buf), "%s", str);
	colorspace_parse (buf, cs, false);
}

/* Текущее значение цветового пространства: вендорским клиентом, если он есть,
 * иначе из узла cs.path. Возвращает malloc-строку или NULL. */
static char *colorspace_read_current (void)
{
	if (g_cs_get_cmd)
	{
		char out [128];
		if (exec_capture (g_cs_get_cmd, out, sizeof (out)) == 0)
		{
			char *eol = strpbrk (out, "\r\n");
			if (eol)
				*eol = 0;
			if (out [0])
				return strdup (out);
		}
	}

	return sysfs_read (g_cs_path);
}

/* Записать значение: sysfs-узел (legacy) или вендорский клиент (Android 10+). */
static bool colorspace_write (const char *attr)
{
	if (!g_cs_apply_cmd)
		return sysfs_write (g_cs_path, attr) == 0;

	char cmd [256], out [512];
	if (snprintf (cmd, sizeof (cmd), "%s '%s'", g_cs_apply_cmd, attr) >= (int) sizeof (cmd))
		return false;

	if (exec_capture (cmd, out, sizeof (out)) != 0)
		return false;

	/* Вендорский клиент печатает "set <attr> to <value>, current value:<value>"
	 * при успехе; на ошибке этого текста нет. */
	if (!strstr (out, "current value:"))
	{
		trace (1, "цветовое пространство не применилось: %s\n", out);
		return false;
	}

	return true;
}

static bool colorspace_parse_filter (const char *csel)
{
	char *csel_dup = strdup (csel);
	char *cur_r, *cur, *tokens = csel_dup;
	while ((cur = strtok_r (tokens, spaces, &cur_r)) != NULL)
	{
		tokens = NULL;
		cur += strspn (cur, spaces);

		if (g_cs_filter_size >= ARRAY_SIZE (g_cs_filter)) {
			trace (1, "\tignoring excessive color space filter: %s\n", cur);
			continue;
		}
		struct cs_filter_t *csf = &g_cs_filter [g_cs_filter_size];

		char *val = strchr (cur, '=');
		if (!val) {
			trace (1, "\tinvalid color space selector: %s\n", csel);
			continue;
		}

		*val++ = 0;
		val += strspn (val, spaces);

		// cur is regexp for video mode
		// val is one of rgb, 444 or 420
		if (!colorspace_parse (val, &csf->cs, true)) {
			trace (1, "\tignoring invalid color space: %s\n", val);
			continue;
		}

		if (regcomp (&csf->rex, cur, REG_EXTENDED) != 0) {
			trace (1, "\tignoring bad regex: %s\n", cur);
			continue;
		}

		trace (2, "\t+ [%s] if mode matches %s\n", colorspace_str (&csf->cs), cur);
		g_cs_filter_size++;
	}

	free (csel_dup);

	return true;
}

bool colorspace_refresh ()
{
	g_cs_supported_size = 0;
	if (!g_cs_list_path || !g_cs_path)
		return false;

	char *list = sysfs_read (g_cs_list_path);
	if (!list)
		return false;

	trace (1, "loading available Color Spaces\n");

	char *cur_r, *cur, *tokens = list;
	while ((cur = strtok_r (tokens, spaces, &cur_r)) != NULL)
	{
		tokens = NULL;
		cur += strspn (cur, spaces);

		if (g_cs_supported_size > ARRAY_SIZE (g_cs_supported)) {
			trace (1, "\tignoring excessive supported color space: %s\n", cur);
			continue;
		}
		struct colorspace_t *cs = &g_cs_supported [g_cs_supported_size];

		if (!colorspace_parse (cur, cs, true)) {
			trace (1, "\tignoring invalid color space: %s\n", cur);
			continue;
		}

		trace (2, "\t+ %s\n", colorspace_str (cs));
		g_cs_supported_size++;
	}

	free (list);

	if (g_cs_default)
		free (g_cs_default);
	g_cs_default = sysfs_read (g_cs_path);

	return true;
}

static bool colorspace_supported (struct colorspace_t *cs)
{
	for (int i = 0; i < g_cs_supported_size; i++) {
		struct colorspace_t *scs = &g_cs_supported [i];

		if ((scs->cs != COLORSPACE_RESERVED) &&
		    (cs->cs != COLORSPACE_RESERVED) &&
		    (scs->cs != cs->cs))
			continue;

		if ((scs->cd != COLORDEPTH_RESERVED) &&
		    (cs->cd != COLORDEPTH_RESERVED) &&
		    (scs->cd != cs->cd))
			continue;

		if ((scs->cr != COLORRANGE_RESERVED) &&
		    (cs->cr != COLORRANGE_RESERVED) &&
		    (scs->cr != cs->cr))
			continue;

		return true;
	}

	return false;
}

/*
 * Правило может задавать только формат: глубину демон берёт текущую (её
 * выставляет система после смены режима). Если такой комбинации у ТВ нет —
 * EDID перечисляет форматы по глубинам, например 4:2:2 только в 12 битах, —
 * вендор молча оставит прежний формат. Подбираем глубину из заявленных для
 * этого формата: ближайшую к текущей (если 8 и 12 бит, а сейчас 10 — 8 бит,
 * а не 12). Диапазон правила не трогаем: он почти никогда не задан, а если
 * задан — проверяется colorspace_supported().
 */
static bool colorspace_fit_depth (struct colorspace_t *cs)
{
	int best = -1, best_diff = 0;

	if (cs->cs == COLORSPACE_RESERVED)
		return false;

	for (int i = 0; i < g_cs_supported_size; i++) {
		struct colorspace_t *scs = &g_cs_supported [i];

		if ((scs->cs != COLORSPACE_RESERVED) && (scs->cs != cs->cs))
			continue;

		if ((scs->cr != COLORRANGE_RESERVED) && (cs->cr != COLORRANGE_RESERVED) &&
		    (scs->cr != cs->cr))
			continue;

		/* узел не ограничивает глубину — текущая подходит */
		if (scs->cd == COLORDEPTH_RESERVED)
			return true;

		int diff = abs ((int) scs->cd - (int) cs->cd);
		if ((best < 0) || (diff < best_diff)) {
			best = i;
			best_diff = diff;
		}
	}

	if (best < 0)
		return false;

	cs->cd = g_cs_supported [best].cd;
	return true;
}

bool colorspace_apply (const char *mode)
{
	if (!g_cs_list_path || !g_cs_path)
		return false;

	/* Идёт возврат режима: правила на этом переключении не применяем, иначе
	 * широкое правило (`.*=422`) снова поставило бы свой формат, а сразу за
	 * возвратом мы восстановим профиль, который был до AFR. */
	if (g_cs_hold)
	{
		trace (2, "цветовое пространство: идёт возврат режима — правило не применяю\n");
		return false;
	}

	/* Правил нет — формат не трогаем вовсе: это пункт «Не изменять формат».
	 * Раньше в этом случае применялся default (значение узла на старте
	 * демона), то есть при каждой смене режима шла запись, а если стартовая
	 * глубина отличалась от выбранной по EDID — настоящая смена формата.
	 * Явно заданный через -c override по-прежнему работает. */
	if ((g_cs_filter_size == 0) && !g_override_cs_enabled)
	{
		trace (3, "правил формата нет — не меняю\n");
		return false;
	}

	const char *cs_attr;

	/* default colorspace parameters */
	struct colorspace_t def_cs = {COLORSPACE_YUV444, COLORDEPTH_24B, COLORRANGE_FUL};
	colorspace_parse_copy (g_cs_default, &def_cs);

	/* current colorspace setting */
	struct colorspace_t cur_cs = def_cs;
	char *cur_cs_str = colorspace_read_current ();

	/* копия текущего значения — colorspace_parse() режет строку на токены
	 * (strtok_r заменяет запятые на NUL), поэтому копируем ДО разбора: иначе
	 * «444,12bit» превратится в «444» и запомненный профиль потеряет глубину. */
	char cur_raw [64];
	snprintf (cur_raw, sizeof (cur_raw), "%s", cur_cs_str ? cur_cs_str : "");

	colorspace_parse (cur_cs_str, &cur_cs, false);
	free (cur_cs_str);

	/* colorspace override */
	if (g_override_cs_enabled) {
		if (g_override_cs.cs != COLORSPACE_RESERVED)
			cur_cs.cs = g_override_cs.cs;
		if (g_override_cs.cd != COLORDEPTH_RESERVED)
			cur_cs.cd = g_override_cs.cd;
		if (g_override_cs.cr != COLORRANGE_RESERVED)
			cur_cs.cr = g_override_cs.cr;
		goto apply;
	}

	for (int i = 0; i < g_cs_filter_size; i++) {
		struct cs_filter_t *csf = &g_cs_filter [i];

		regmatch_t match [1];
		if (regexec (&csf->rex, mode, 1, match, 0) == REG_NOMATCH)
			continue;
		// must match whole line
		if (match [0].rm_so != 0 || match [0].rm_eo != strlen (mode))
			continue;

		/* Итоговая комбинация: правило поверх текущего значения. Проверять
		 * только само правило нельзя — глубина в нём обычно не задана, а
		 * формат без глубины у ТВ может отсутствовать. */
		struct colorspace_t want = cur_cs;
		if (csf->cs.cs != COLORSPACE_RESERVED)
			want.cs = csf->cs.cs;
		if (csf->cs.cd != COLORDEPTH_RESERVED)
			want.cd = csf->cs.cd;
		if (csf->cs.cr != COLORRANGE_RESERVED)
			want.cr = csf->cs.cr;

		if (!colorspace_supported (&want)) {
			/* Глубину подбираем, только если правило её не задаёт: иначе
			 * комбинацию выбрал пользователь и молча менять её нельзя. */
			struct colorspace_t fit = want;
			if ((csf->cs.cd != COLORDEPTH_RESERVED) ||
			    !colorspace_fit_depth (&fit)) {
				trace (2, "Not using color space %s because not supported\n",
					colorspace_str (&csf->cs));
				continue;
			}

			/* colorspace_str() возвращает общий буфер: второй вызов в том же
			 * trace() затёр бы первый, поэтому строку копируем. */
			char fit_str [64];
			snprintf (fit_str, sizeof (fit_str), "%s", colorspace_str (&fit));
			trace (2, "формат %s в текущей глубине не заявлен — беру %s\n",
				colorspace_str (&csf->cs), fit_str);
			want = fit;
		}

		cur_cs = want;
		goto apply;
	}

	/* use default colorspace if no match found */
	cur_cs = def_cs;

apply:
	cs_attr = colorspace_str (&cur_cs);

	/* Сравниваем семантически, а не по строке: представление в узле и у
	 * вендорского сервиса различается (у сервиса диапазон "full" может
	 * отбрасываться), из-за строкового сравнения писали зря — а запись
	 * перенастраивает HDMI-линк. */
	if (cur_raw [0])
	{
		struct colorspace_t now = def_cs;
		colorspace_parse_copy (cur_raw, &now);

		if ((now.cs == cur_cs.cs) && (now.cd == cur_cs.cd) && (now.cr == cur_cs.cr))
		{
			trace (2, "Цветовое пространство уже %s — не меняю\n", cs_attr);
			return true;
		}
	}

	/* Запоминаем профиль до первого изменения: на выходе вернём его
	 * (cs.restore). Читаем до записи и только один раз за сеанс — иначе «до»
	 * станет нашим же «после». */
	if (g_cs_restore && !g_cs_before && cur_raw [0])
	{
		g_cs_before = strdup (cur_raw);
		trace (2, "запоминаю цветовой профиль %s — верну на выходе\n", g_cs_before);
	}

	trace (1, "Setting color space to %s\n", cs_attr);

	return colorspace_write (cs_attr);
}

/* Совпадают ли значения семантически: RESERVED — «любое», поэтому сравнение
 * не должно считать «444,12bit» и «444,12bit,full» разными (у вендорского
 * клиента диапазон может отбрасываться). */
static bool colorspace_same (struct colorspace_t *a, struct colorspace_t *b)
{
	if ((a->cs != COLORSPACE_RESERVED) && (b->cs != COLORSPACE_RESERVED) &&
	    (a->cs != b->cs))
		return false;

	if ((a->cd != COLORDEPTH_RESERVED) && (b->cd != COLORDEPTH_RESERVED) &&
	    (a->cd != b->cd))
		return false;

	if ((a->cr != COLORRANGE_RESERVED) && (b->cr != COLORRANGE_RESERVED) &&
	    (a->cr != b->cr))
		return false;

	return true;
}

/* Вернуть профиль, который был до первого изменения (cs.restore). Вызывается
 * там же, где возвращается режим; повторный вызов без сохранённого значения —
 * no-op. */
bool colorspace_restore (void)
{
	char *value = g_cs_before;
	g_cs_before = NULL;

	if (!value)
		return false;

	/* Если текущее значение уже совпадает — не пишем: каждая запись
	 * перенастраивает HDMI-линк. */
	bool same = false;
	char *now = colorspace_read_current ();
	if (now)
	{
		struct colorspace_t a = {COLORSPACE_RESERVED, COLORDEPTH_RESERVED, COLORRANGE_RESERVED};
		struct colorspace_t b = {COLORSPACE_RESERVED, COLORDEPTH_RESERVED, COLORRANGE_RESERVED};

		colorspace_parse_copy (now, &a);
		colorspace_parse_copy (value, &b);
		same = colorspace_same (&a, &b);

		free (now);
	}

	bool ok = true;
	if (same)
		trace (2, "цветовой профиль уже %s — не меняю\n", value);
	else
	{
		trace (1, "возвращаю цветовой профиль %s\n", value);
		ok = colorspace_write (value);
	}

	free (value);
	return ok;
}

void colorspace_hold (bool on)
{
	g_cs_hold = on;
}

void afrd_override_colorspace (char **cs)
{
	char *cur = *cs;

	if (cur && *cur && colorspace_parse (cur, &g_override_cs, true)) {
		g_override_cs_enabled = true;
		cur = strchr (cur, 0);
	} else
		g_override_cs_enabled = false;

	*cs = cur;
}

void colorspace_init ()
{
	g_override_cs_enabled = false;
	g_cs_hold = false;

	g_cs_restore = cfg_get_int ("cs.restore", 1);

	g_cs_list_path = cfg_get_str ("cs.list.path", NULL);
	if (!g_cs_list_path)
		return;

	g_cs_path = cfg_get_str ("cs.path", NULL);
	if (!g_cs_path)
		return;

	const char *cs_select = cfg_get_str ("cs.select", NULL);
	if (!cs_select)
		return;

	trace (1, "loading Color Space selector\n");

	if (!colorspace_parse_filter (cs_select))
		return;
}

void colorspace_fini ()
{
	for (int i = 0; i < g_cs_filter_size; i++) {
		struct cs_filter_t *csf = &g_cs_filter [i];
		regfree (&csf->rex);
	}

	g_cs_filter_size = 0;

	/* Указатель обязан быть обнулён: следующий afrd_init() (перезагрузка
	 * конфига: reconf или правка afrd.ini) вызовет colorspace_refresh(),
	 * которая освобождает g_cs_default перед повторным чтением, — без
	 * обнуления это освобождение уже освобождённой памяти (двойной free,
	 * повреждение кучи и падение демона; воспроизведено живьём на ATV14
	 * при второй перезагрузке конфига подряд). */
	if (g_cs_default) {
		free (g_cs_default);
		g_cs_default = NULL;
	}

	if (g_cs_before) {
		free (g_cs_before);
		g_cs_before = NULL;
	}

	g_cs_list_path = g_cs_path = NULL;
}
