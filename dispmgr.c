/*
 * AFRd2: разбор пользовательского preferred-режима DisplayManager.
 * См. dispmgr.h — id из другого пространства, нужны частота и геометрия.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dispmgr.h"

#define PREF_PREFIX	"mUserPreferredModeId="
#define REC_PREFIX	"DisplayModeRecord{mMode={"

int dispmgr_preferred_parse (const char *dump, dispmgr_mode_t *out)
{
	if (!dump || !out)
		return -1;

	const char *p = strstr (dump, PREF_PREFIX);
	if (!p)
		return -1;

	int pref = atoi (p + strlen (PREF_PREFIX));
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
