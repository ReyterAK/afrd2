/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * Перезагрузка конфига: чистые правила, вынесенные для host-тестов —
 * сам демон (afrd.c) на хосте не собирается.
 */

#ifndef RECONF_H
#define RECONF_H

#include <stdbool.h>
#include <time.h>

/* Изменился ли файл конфига: cur — текущий mtime (mtime()), prev — запомненный
 * при чтении конфига. mtime()==0 (stat не удался: файл недоступен/удалён)
 * изменением НЕ считается — иначе при недоступном файле демон перезагружался
 * бы вхолостую каждый период проверки. Запомненный prev==(time_t)-1 (afrd_reconf
 * на legacy-пути) — валидный «изменилось»: он не равен ни одному реальному mtime. */
bool config_mtime_changed (time_t cur, time_t prev);

#endif /* RECONF_H */
