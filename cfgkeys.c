/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * Применимость ключей конфига к путям работы демона: см. cfgkeys.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg_parse.h"
#include "cfgkeys.h"

/* Все ключи, которые демон знает, и пути, на которых они действуют.
 *
 * Таблица — единственный источник правды и для проверки конфига, и для
 * README: добавляя ключ, добавь его сюда, иначе аудит назовёт его
 * «неизвестным» (а host-тест поймает это на наших же профилях). */
static const struct
{
	const char *key;
	unsigned paths;
	const char *note;	/* примечание вместо стандартной формулировки (NULL — обычная) */
} k_keys [] =
{
	/* общие для обоих путей */
	{ "enable",			CFGKEY_BOTH, NULL },
	{ "log.enable",			CFGKEY_BOTH, NULL },
	{ "log.file",			CFGKEY_BOTH, NULL },
	{ "mode.prefer.exact",		CFGKEY_BOTH, NULL },
	{ "mode.use.fract",		CFGKEY_BOTH, NULL },
	{ "mode.blacklist.rates",	CFGKEY_BOTH, NULL },
	{ "mode.min.hz.list",		CFGKEY_BOTH, NULL },
	{ "switch.skip.pkg",		CFGKEY_BOTH, NULL },
	{ "switch.timeout",		CFGKEY_BOTH, NULL },
	/* На новом пути ключ ожил 2026-10-08 со своим смыслом (предел удержания
	 * режима на паузе): события «видео кончилось», к которому он привязан на
	 * старом пути, там не существует — остановленное видео с плеером на
	 * экране неотличимо от паузы (журнал §32.1). */
	{ "switch.delay.off",		CFGKEY_BOTH, NULL },

	/* Подъём режима до максимальной частоты ТВ при старте видео (Android 11+).
	 * Плеер фиксирует частоту в начале сессии проигрывания; если дисплей в этот
	 * момент медленнее контента, темп не поднимется никогда (журнал §47.6). */
	{ "switch.bump",		CFGKEY_NEWPATH, NULL },

	/* Кому применяются механики зажима: список пакетов (по умолчанию —
	 * штатный YouTube). Зажим измерен только у него, а запасной способ
	 * сброса — смена конфигурации экрана, на которой часть плееров
	 * перезапускает Activity (живой случай: OTT Navigator). Журнал §55.9. */
	{ "switch.bump.pkg",		CFGKEY_NEWPATH, NULL },

	/* Сброс «потолка» частоты у плеера (Android 10-14): перезапуск процесса
	 * кодека, при отказе — смена плотности. Не зависит от подъёма режима;
	 * умолчание следует за switch.bump. Журнал §47.9, §60, §67. */
	{ "switch.ceiling.reset",	CFGKEY_NEWPATH, NULL },

	/* Чем сбрасывать потолок: codec — перезапуском процесса кодека (быстро,
	 * 1.1 с, декодер пересоздаётся у всех приложений), density — сменой
	 * плотности экрана (медленно, 6.8 с, в пределах приложения), auto —
	 * кодек с запасным путём на плотность. Журнал §60. */
	{ "switch.ceiling.method",	CFGKEY_NEWPATH, NULL },

	/* Кому применяется сброс «потолка». Отдельный список нужен потому, что
	 * подъём режима (switch.bump.pkg) для некоторых плееров безопасен, а
	 * перезапуск кодека — нет (журнал §110). Ключ не задан — список содержит
	 * только штатный YouTube (ветка «следует за switch.bump.pkg» больше не
	 * достигается: список заполняется явно, см. start-up), задан пустым —
	 * никому. */
	{ "switch.ceiling.pkg",		CFGKEY_NEWPATH, NULL },

	/* Короткий пробник подачи после подъёма: пока держим максимум, решаем по
	 * данным видео-слоя (мс; 0 — выключено). Журнал §111.4, §118. */
	{ "switch.bump.probe.ms",	CFGKEY_NEWPATH, NULL },

	/* Возврат режима, когда подача после переключения осталась «зажатой»
	 * (switch.revert): плеер держит темп от старта под другим режимом, и
	 * вылечить это «туда-обратно» не удалось. Возврат к прежнему режиму даёт
	 * гладкую картинку ценой потери части кадров контента. Только плеерам из
	 * switch.bump.pkg; один раз на видео. Журнал §113. */
	{ "switch.revert",		CFGKEY_NEWPATH, NULL },

	/* Порог «неровной подачи» по данным видео-слоя: интервал считается
	 * выбросом, если он больше ratio процентов от базы (максимум из периода
	 * дисплея и ожидаемого интервала), а доля выбросов выше gap процентов
	 * означает «подача неровная» — по этой проверке срабатывает лечение
	 * (журнал §110: медиана видела 40 мс при 20 % провалов по 80 мс). */
	{ "fps.feed.ratio.pct",		CFGKEY_NEWPATH, NULL },
	{ "fps.feed.gap.pct",		CFGKEY_NEWPATH, NULL },

	/* Подъём режима при выходе плеера на передний план: на Android 10/11 это
	 * успевает до того, как плеер запомнит частоту (журнал §49.13). */
	{ "switch.bump.on.app",		CFGKEY_NEWPATH, NULL },

	/* Быстрое решение по заявленной частоте кадра: живёт в цикле нового пути
	 * (на старом свой тайминг по uevent'ам, заявленную частоту он не читает).
	 * Умолчание зависит от вида нового пути: на Android 10/11 включено, на
	 * ATV14 выключено. */
	{ "fps.declare.fast",		CFGKEY_NEWPATH, NULL },

	/* только новый путь (Android 10-14) */
	/* Вендорный путь смены режима (Android 10/11, Amlogic): путь к помощнику
	 * и список режимов ТВ. Помощник раскладывают приложение и Magisk-модуль,
	 * поэтому путь настраиваемый; список нужен, потому что таблицы режимов
	 * SurfaceFlinger на 10/11 нет. */
	{ "vendor.disp.cli",		CFGKEY_VENDOR, NULL },
	/* Постоянный помощник смены режима: держит VM загруженной, снимая ~490 мс
	 * запуска app_process с каждой смены (журнал §49.9). */
	{ "vendor.disp.persist",	CFGKEY_VENDOR, NULL },
	{ "vendor.disp.cap",		CFGKEY_VENDOR, NULL },
	{ "trigger.poll.ms",		CFGKEY_NEWPATH, NULL },
	{ "trigger.start.debounce.ms",	CFGKEY_NEWPATH, NULL },
	{ "trigger.stop.debounce.ms",	CFGKEY_NEWPATH, NULL },
	{ "fps.measure.envelope.ms",	CFGKEY_NEWPATH, NULL },
	{ "fps.measure.envelope.early.ms", CFGKEY_NEWPATH, NULL },
	{ "fps.measure.envelope.max.ms", CFGKEY_NEWPATH, NULL },
	{ "fps.measure.envelope.sub.ms", CFGKEY_NEWPATH, NULL },
	{ "fps.measure.envelope.skip.ms", CFGKEY_NEWPATH, NULL },
	{ "fps.measure.retries",	CFGKEY_NEWPATH, NULL },
	{ "fps.declared.crosscheck.pct", CFGKEY_NEWPATH, NULL },
	{ "fps.recheck.delay.ms",	CFGKEY_NEWPATH, NULL },
	{ "fps.cadence.fix.ms",		CFGKEY_NEWPATH, NULL },
	{ "fps.prefer.api",		CFGKEY_NEWPATH, NULL },
	{ "pause.keep",			CFGKEY_NEWPATH, NULL },
	/* Один ключ на поведение при паузе (keep|timeout|off): в GUI заменяет
	 * pause.keep и switch.delay.off, старые ключи продолжают работать. */
	{ "pause.mode",			CFGKEY_NEWPATH, NULL },
	{ "burst.ms",			CFGKEY_NEWPATH, NULL },
	{ "burst.broadcast",		CFGKEY_NEWPATH, NULL },
	{ "switch.restore.on.stop",	CFGKEY_NEWPATH, NULL },
	/* Куда возвращать режим после подъёма: 1 — на максимум (умолчание,
	 * защита следующего ролика), 0 — на режим до подъёма (как в 1.1.0).
	 * Экспертный ключ, в GUI его нет. */
	{ "switch.restore.bump",	CFGKEY_NEWPATH, NULL },
	{ "fps.correct.enable",		CFGKEY_NEWPATH, NULL },
	{ "fps.correct.list",		CFGKEY_NEWPATH, NULL },
	/* Одна настройка на поправку AV1: off | проценты; применяется только к
	 * плеерам из fps.correct.av1.pkg (пустой список — выключено; автопоправки
	 * и встроенного списка пакетов нет, решение 05.11). Старые ключи
	 * .offset и .pct остаются известными (старые конфиги) и читаются, только
	 * если нового ключа нет, — взаимоисключающе (журнал §99.5). */
	{ "fps.correct.av1",		CFGKEY_NEWPATH, NULL },
	{ "fps.correct.av1.offset",	CFGKEY_NEWPATH, NULL },
	/* Относительная поправка AV1 (проценты): на Android 10/11 завышение
	 * пропорционально частоте (журнал §49.18). */
	{ "fps.correct.av1.pct",	CFGKEY_NEWPATH, NULL },
	{ "fps.correct.av1.pkg",	CFGKEY_NEWPATH, NULL },

	/* заменены огибающей (наблюдение 20 с): ключ известен, но на новом пути
	 * не действует — старый конфиг получает внятное сообщение, а не
	 * «неизвестный ключ (опечатка?)» */
	{ "fps.measure.window.ms",	0, "устарел: заменён fps.measure.envelope.ms (Android 10-14)" },
	{ "fps.measure.window.max.ms",	0, "устарел: заменён fps.measure.envelope.ms (Android 10-14)" },

	/* только старый путь (Android 6-9) */
	{ "mode.extra",			CFGKEY_LEGACY, NULL },
	/* узел текущего режима читает и цветовое пространство нового пути
	 * (colorspace_apply_current_mode), поэтому действует на всех трёх */
	{ "mode.path",			CFGKEY_BOTH, NULL },
	{ "vdec.sysfs",			CFGKEY_LEGACY, NULL },
	{ "vdec.blacklist",		CFGKEY_LEGACY, NULL },
	{ "frhint.vdec.blacklist",	CFGKEY_LEGACY, NULL },
	/* hdmi.sysfs: frac_rate_policy и disp_cap читаются и на вендорном пути
	 * (vendor_frac_ensure, atv14_report_hz); на backdoor их роль играет
	 * транзакция SurfaceFlinger */
	{ "hdmi.sysfs",			CFGKEY_LEGACY | CFGKEY_VENDOR, NULL },
	{ "hdmi.state",			CFGKEY_LEGACY, NULL },
	{ "switch.delay.on",		CFGKEY_LEGACY, NULL },
	{ "switch.delay.retry",		CFGKEY_LEGACY, NULL },
	{ "switch.blackout",		CFGKEY_LEGACY, NULL },
	{ "switch.ignore",		CFGKEY_LEGACY, NULL },
	{ "switch.hdmi",		CFGKEY_LEGACY, NULL },
	{ "cs.select",			CFGKEY_BOTH, NULL },
	/* возврат профиля, который был до первого изменения (см. cs.restore) */
	{ "cs.restore",			CFGKEY_BOTH, NULL },
	{ "cs.list.path",			CFGKEY_BOTH, NULL },
	{ "cs.path",			CFGKEY_BOTH, NULL },
	{ "uevent.filter.frhint",	CFGKEY_LEGACY, NULL },
	{ "uevent.filter.vdec",		CFGKEY_LEGACY, NULL },
	{ "uevent.filter.hdmi",		CFGKEY_LEGACY, NULL },
	{ "uevent.filter.hdcp",		CFGKEY_LEGACY, NULL },

	/* настройка приложения: в ini попадает, но демон её не читает */
	{ "toast.hz",			CFGKEY_APP, NULL },
};

const char *cfgkeys_note (const char *key, unsigned path)
{
	for (unsigned i = 0; i < sizeof (k_keys) / sizeof (k_keys [0]); i++)
		if (strcmp (key, k_keys [i].key) == 0)
		{
			if (k_keys [i].paths & path)
				return NULL;

			if (k_keys [i].note)
				return k_keys [i].note;

			if (k_keys [i].paths & CFGKEY_APP)
				return "ключ настройки приложения — демон его не читает";

			/* Путь назван точно: ключ может отсутствовать на одном из двух
			 * новых путей, хотя на другом действует. */
			switch (path)
			{
			case CFGKEY_VENDOR:
				if (k_keys [i].paths & CFGKEY_ATV14)
					return "не действует на вендорном пути (Android 10-11)";
				break;

			case CFGKEY_ATV14:
				if (k_keys [i].paths & CFGKEY_VENDOR)
					return "не действует на новом пути (Android 12-14)";
				break;
			}

			return (path == CFGKEY_LEGACY)
				? "не действует на старом пути (Android 6-9)"
				: "не действует на новом пути (Android 10-14)";
		}

	return "неизвестный ключ (опечатка?)";
}

struct audit_ctx
{
	void (*warn) (const char *msg, void *ctx);
	void *ctx;
	unsigned path;
	int count;
};

static void audit_key (const char *key, const char *value, void *uctx)
{
	(void)value;
	struct audit_ctx *a = uctx;

	const char *note = cfgkeys_note (key, a->path);
	if (!note)
		return;

	char msg [256];
	snprintf (msg, sizeof (msg), "ключ конфига '%s': %s", key, note);
	a->count++;
	if (a->warn)
		a->warn (msg, a->ctx);
}

/* Дубли ключей: cfg_set() при повторе перезаписывает значение, и молча
 * действует последняя строка. Живой случай 31.10 (журнал §104): в профиле
 * оказались и fps.declare.fast=1, и ниже =0 (дописало приложение) — быстрый
 * режим был выключен, и после смены канала 20 с стоял прежний режим.
 * Сообщаем только о дублях с разными значениями: одинаковые безвредны. */
static void audit_duplicates (const char *filename,
                              void (*warn) (const char *msg, void *ctx), void *ctx)
{
	enum { MAX_KEYS = 256 };
	char *keys [MAX_KEYS];
	char *vals [MAX_KEYS];
	int n = 0, i;
	char line [512];
	FILE *f = fopen (filename, "r");

	if (!f)
		return;

	while (fgets (line, sizeof (line), f))
	{
		char *s = line, *eq, *key, *val;
		size_t kl, vl;

		while ((*s == ' ') || (*s == '\t'))
			s++;
		if ((*s == '#') || (*s == '\n') || (*s == '\r') || (*s == 0))
			continue;

		eq = strchr (s, '=');
		if (!eq)
			continue;

		key = s;
		kl = (size_t) (eq - s);
		val = eq + 1;
		vl = strlen (val);
		while (kl && ((key [kl - 1] == ' ') || (key [kl - 1] == '\t')))
			kl--;
		while (vl && ((val [vl - 1] == '\n') || (val [vl - 1] == '\r') ||
		              (val [vl - 1] == ' ') || (val [vl - 1] == '\t')))
			vl--;

		for (i = 0; i < n; i++)
			if ((strlen (keys [i]) == kl) && !strncmp (keys [i], key, kl))
				break;

		if (i < n)
		{
			if ((strlen (vals [i]) != vl) || strncmp (vals [i], val, vl))
			{
				char msg [256];
				snprintf (msg, sizeof (msg),
				          "дубль ключа '%.*s': '%.*s' затем '%.*s' — "
				          "действует последнее значение",
				          (int) kl, key, (int) strlen (vals [i]), vals [i],
				          (int) vl, val);
				if (warn)
					warn (msg, ctx);
			}
			continue;
		}

		if (n >= MAX_KEYS)
			break;

		keys [n] = (char *) malloc (kl + 1);
		vals [n] = (char *) malloc (vl + 1);
		if (!keys [n] || !vals [n])
		{
			free (keys [n]);
			free (vals [n]);
			break;
		}
		memcpy (keys [n], key, kl);
		keys [n][kl] = 0;
		memcpy (vals [n], val, vl);
		vals [n][vl] = 0;
		n++;
	}

	fclose (f);

	for (i = 0; i < n; i++)
	{
		free (keys [i]);
		free (vals [i]);
	}
}

int cfgkeys_audit (const char *filename, unsigned path,
                   void (*warn) (const char *msg, void *ctx), void *ctx)
{
	if (!filename)
		return -1;

	struct cfg_struct *cfg = cfg_init ();
	if (!cfg)
		return -1;

	if (cfg_load (cfg, filename) < 0)
	{
		cfg_free (cfg);
		return -1;
	}

	audit_duplicates (filename, warn, ctx);

	struct audit_ctx a = { warn, ctx, path, 0 };
	cfg_visit (cfg, audit_key, &a);

	cfg_free (cfg);
	return a.count;
}
