/*
 * AFRd2: разбор пользовательского preferred-режима DisplayManager.
 * См. dispmgr.h — id из другого пространства, нужны частота и геометрия.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dispmgr.h"

#define PREF_PREFIX	"mUserPreferredModeId="
/* Android 11 не печатает mUserPreferredModeId. mDefaultModeId — НЕ отдельная
 * настройка пользователя, а последний активный режим: вендорный
 * setActiveDispMode переписывает и его (проверено на `.42`: 60 -> 19,
 * 24 -> 18, getPrefHdmiDispMode следует за ним же). Поэтому для рабочего
 * стола демон кэширует значение на старте (vendor_desktop_cache). */
#define PREF_DEFAULT	"mDefaultModeId="
#define REC_PREFIX	"DisplayModeRecord{mMode={"

int dispmgr_preferred_parse (const char *dump, dispmgr_mode_t *out)
{
	if (!dump || !out)
		return -1;

	const char *p = strstr (dump, PREF_PREFIX);
	int pref = p ? atoi (p + strlen (PREF_PREFIX)) : -1;

	/* Нет пользовательского предпочтения или оно -1 — берём defaultMode */
	if (pref < 0)
	{
		p = strstr (dump, PREF_DEFAULT);
		pref = p ? atoi (p + strlen (PREF_DEFAULT)) : -1;
	}

	if (pref < 0)
		return -1;

	p = dump;
	while ((p = strstr (p, REC_PREFIX)) != NULL)
	{
		int id, w, h;
		double fps;

		/* Формат Android 12-14:
		 * DisplayModeRecord{mMode={id=1, width=1920, height=1080,
		 *                        fps=59.94006, ...}} */
		if (sscanf (p, REC_PREFIX "id=%d, width=%d, height=%d, fps=%lf",
		            &id, &w, &h, &fps) == 4)
		{
			if (id == pref)
			{
				out->id = id;
				out->width = w;
				out->height = h;
				out->hz = (int)(fps * 256.0 + 0.5);
				return 0;
			}
		}

		p += strlen (REC_PREFIX);
	}

	return -1;
}
