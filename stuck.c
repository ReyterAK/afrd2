/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * См. stuck.h — состояние и решения сторожа «залипшего канала».
 */

#include <stdio.h>
#include <string.h>

#include "stuck.h"

void stuck_reset (stuck_state_t *st)
{
	st->pkg [0] = 0;
	st->mismatch = 0;
	st->blind_logged = false;
}

void stuck_set_pkg (stuck_state_t *st, const char *pkg)
{
	if (pkg && pkg [0])
		snprintf (st->pkg, sizeof (st->pkg), "%s", pkg);
	else
		st->pkg [0] = 0;
	st->mismatch = 0;
}

stuck_action_t stuck_step (stuck_state_t *st, int probe_rc, const char *top_pkg)
{
	/* Отказ пробы значит ровно одно: проверить залипший канал сейчас нельзя.
	 * Ни снять удержание, ни запомнить пакет по пустому выводу нельзя.
	 * Говорим об этом один раз на видео, дальше молчим (иначе журнал забит). */
	if (probe_rc != 0)
	{
		if (st->blind_logged)
			return STUCK_NONE;
		st->blind_logged = true;
		return STUCK_BLIND;
	}

	/* Пакет не был запомнен (проба на старте видео сорвалась) — запоминаем
	 * сейчас; сравнение с ним начнётся со следующей проверки, иначе текущий
	 * передний план сразу считался бы «уходом». */
	if (st->pkg [0] == 0)
	{
		stuck_set_pkg (st, top_pkg);
		return STUCK_FILL_PKG;
	}

	/* Передний план — тот же плеер: это настоящая пауза, счётчик несовпадений
	 * сбрасываем (пользователь вернулся в плеер — сторож начинается заново). */
	if (strcmp (top_pkg, st->pkg) == 0)
	{
		st->mismatch = 0;
		return STUCK_NONE;
	}

	st->mismatch++;
	return (st->mismatch >= STUCK_CONFIRM) ? STUCK_GIVE_UP : STUCK_NONE;
}
