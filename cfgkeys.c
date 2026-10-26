/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * Применимость ключей конфига к путям работы демона: см. cfgkeys.h.
 */

#include <stdio.h>
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
	{ "switch.bump",		CFGKEY_ATV14, NULL },

	/* Кому применяются механики зажима: список пакетов (по умолчанию —
	 * штатный YouTube). Зажим измерен только у него, а сброс потолка — смена
	 * конфигурации экрана, на которой часть плееров перезапускает Activity
	 * (живой случай: OTT Navigator). Журнал §55.9. */
	{ "switch.bump.pkg",		CFGKEY_ATV14, NULL },

	/* Сброс «потолка» частоты у плеера сменой плотности экрана (Android 12-14;
	 * на вендорном пути Android 10/11 операция стоит 1.5-2.5 с и по умолчанию
	 * выключена). Журнал §47.9. */
	{ "switch.ceiling.reset",	CFGKEY_ATV14, NULL },

	/* Подъём режима при выходе плеера на передний план: на Android 10/11 это
	 * успевает до того, как плеер запомнит частоту (журнал §49.13). */
	{ "switch.bump.on.app",		CFGKEY_ATV14, NULL },

	/* Быстрое решение по заявленной частоте кадра: живёт в цикле нового пути
	 * (на старом свой тайминг по uevent'ам, заявленную частоту он не читает).
	 * Умолчание зависит от вида нового пути: на Android 10/11 включено, на
	 * ATV14 выключено. */
	{ "fps.declare.fast",		CFGKEY_ATV14, NULL },

	/* только новый путь (Android 10-14) */
	/* Вендорный путь смены режима (Android 10/11, Amlogic): путь к помощнику
	 * и список режимов ТВ. Помощник раскладывают приложение и Magisk-модуль,
	 * поэтому путь настраиваемый; список нужен, потому что таблицы режимов
	 * SurfaceFlinger на 10/11 нет. */
	{ "vendor.disp.cli",		CFGKEY_ATV14, NULL },
	/* Постоянный помощник смены режима: держит VM загруженной, снимая ~490 мс
	 * запуска app_process с каждой смены (журнал §49.9). */
	{ "vendor.disp.persist",	CFGKEY_ATV14, NULL },
	{ "vendor.disp.cap",		CFGKEY_ATV14, NULL },
	{ "trigger.poll.ms",		CFGKEY_ATV14, NULL },
	{ "trigger.start.debounce.ms",	CFGKEY_ATV14, NULL },
	{ "trigger.stop.debounce.ms",	CFGKEY_ATV14, NULL },
	{ "fps.measure.envelope.ms",	CFGKEY_ATV14, NULL },
	{ "fps.measure.envelope.early.ms", CFGKEY_ATV14, NULL },
	{ "fps.measure.envelope.max.ms", CFGKEY_ATV14, NULL },
	{ "fps.measure.envelope.sub.ms", CFGKEY_ATV14, NULL },
	{ "fps.measure.envelope.skip.ms", CFGKEY_ATV14, NULL },
	{ "fps.measure.retries",	CFGKEY_ATV14, NULL },
	{ "fps.declared.crosscheck.pct", CFGKEY_ATV14, NULL },
	{ "fps.recheck.delay.ms",	CFGKEY_ATV14, NULL },
	{ "fps.cadence.fix.ms",		CFGKEY_ATV14, NULL },
	{ "fps.prefer.api",		CFGKEY_ATV14, NULL },
	{ "pause.keep",			CFGKEY_ATV14, NULL },
	{ "switch.restore.on.stop",	CFGKEY_ATV14, NULL },
	{ "fps.correct.enable",		CFGKEY_ATV14, NULL },
	{ "fps.correct.list",		CFGKEY_ATV14, NULL },
	{ "fps.correct.av1.offset",	CFGKEY_ATV14, NULL },
	/* Относительная поправка AV1 (проценты): на Android 10/11 завышение
	 * пропорционально частоте (журнал §49.18). */
	{ "fps.correct.av1.pct",	CFGKEY_ATV14, NULL },
	{ "fps.correct.av1.pkg",	CFGKEY_ATV14, NULL },

	/* заменены огибающей (наблюдение 20 с): ключ известен, но на новом пути
	 * не действует — старый конфиг получает внятное сообщение, а не
	 * «неизвестный ключ (опечатка?)» */
	{ "fps.measure.window.ms",	0, "устарел: заменён fps.measure.envelope.ms (Android 10-14)" },
	{ "fps.measure.window.max.ms",	0, "устарел: заменён fps.measure.envelope.ms (Android 10-14)" },

	/* только старый путь (Android 6-9) */
	{ "mode.extra",			CFGKEY_LEGACY, NULL },
	{ "mode.path",			CFGKEY_LEGACY, NULL },
	{ "vdec.sysfs",			CFGKEY_LEGACY, NULL },
	{ "vdec.blacklist",		CFGKEY_LEGACY, NULL },
	{ "frhint.vdec.blacklist",	CFGKEY_LEGACY, NULL },
	{ "hdmi.sysfs",			CFGKEY_LEGACY, NULL },
	{ "hdmi.state",			CFGKEY_LEGACY, NULL },
	{ "switch.delay.on",		CFGKEY_LEGACY, NULL },
	{ "switch.delay.retry",		CFGKEY_LEGACY, NULL },
	{ "switch.blackout",		CFGKEY_LEGACY, NULL },
	{ "switch.ignore",		CFGKEY_LEGACY, NULL },
	{ "switch.hdmi",		CFGKEY_LEGACY, NULL },
	{ "cs.select",			CFGKEY_BOTH, NULL },
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

			return (path == CFGKEY_ATV14)
				? "не действует на новом пути (Android 10-14)"
				: "не действует на старом пути (Android 6-9)";
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

	struct audit_ctx a = { warn, ctx, path, 0 };
	cfg_visit (cfg, audit_key, &a);

	cfg_free (cfg);
	return a.count;
}
