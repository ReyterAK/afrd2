/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * Перезагрузка конфига: правила изменения mtime (см. reconf.h).
 */

#include "reconf.h"

bool config_mtime_changed (time_t cur, time_t prev)
{
	return (cur != 0) && (cur != prev);
}
