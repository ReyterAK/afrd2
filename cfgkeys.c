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
} k_keys [] =
{
	/* общие для обоих путей */
	{ "enable",			CFGKEY_BOTH },
	{ "log.enable",			CFGKEY_BOTH },
	{ "log.file",			CFGKEY_BOTH },
	{ "mode.prefer.exact",		CFGKEY_BOTH },
	{ "mode.use.fract",		CFGKEY_BOTH },
	{ "mode.blacklist.rates",	CFGKEY_BOTH },
	{ "switch.timeout",		CFGKEY_BOTH },

	/* только новый путь (Android 10-14) */
	{ "trigger.poll.ms",		CFGKEY_ATV14 },
	{ "trigger.start.debounce.ms",	CFGKEY_ATV14 },
	{ "trigger.stop.debounce.ms",	CFGKEY_ATV14 },
	{ "fps.measure.window.ms",	CFGKEY_ATV14 },
	{ "fps.measure.window.max.ms",	CFGKEY_ATV14 },
	{ "fps.measure.retries",	CFGKEY_ATV14 },
	{ "fps.declared.crosscheck.pct", CFGKEY_ATV14 },
	{ "fps.recheck.delay.ms",	CFGKEY_ATV14 },
	{ "fps.prefer.api",		CFGKEY_ATV14 },
	{ "pause.keep",			CFGKEY_ATV14 },
	{ "switch.restore.on.stop",	CFGKEY_ATV14 },

	/* только старый путь (Android 6-9) */
	{ "mode.extra",			CFGKEY_LEGACY },
	{ "mode.path",			CFGKEY_LEGACY },
	{ "vdec.sysfs",			CFGKEY_LEGACY },
	{ "vdec.blacklist",		CFGKEY_LEGACY },
	{ "frhint.vdec.blacklist",	CFGKEY_LEGACY },
	{ "hdmi.sysfs",			CFGKEY_LEGACY },
	{ "hdmi.state",			CFGKEY_LEGACY },
	{ "switch.delay.on",		CFGKEY_LEGACY },
	{ "switch.delay.off",		CFGKEY_LEGACY },
	{ "switch.delay.retry",		CFGKEY_LEGACY },
	{ "switch.blackout",		CFGKEY_LEGACY },
	{ "switch.ignore",		CFGKEY_LEGACY },
	{ "switch.hdmi",		CFGKEY_LEGACY },
	{ "cs.select",			CFGKEY_LEGACY },
	{ "cs.list.path",		CFGKEY_LEGACY },
	{ "cs.path",			CFGKEY_LEGACY },
	{ "uevent.filter.frhint",	CFGKEY_LEGACY },
	{ "uevent.filter.vdec",		CFGKEY_LEGACY },
	{ "uevent.filter.hdmi",		CFGKEY_LEGACY },
	{ "uevent.filter.hdcp",		CFGKEY_LEGACY },

	/* настройка приложения: в ini попадает, но демон её не читает */
	{ "toast.hz",			CFGKEY_APP },
};

const char *cfgkeys_note (const char *key, unsigned path)
{
	for (unsigned i = 0; i < sizeof (k_keys) / sizeof (k_keys [0]); i++)
		if (strcmp (key, k_keys [i].key) == 0)
		{
			if (k_keys [i].paths & path)
				return NULL;

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
