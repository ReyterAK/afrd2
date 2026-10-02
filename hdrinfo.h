#ifndef HDRINFO_H
#define HDRINFO_H

#include <stdbool.h>

/* Атрибуты дисплея вендорского сервиса (Android 10-14, ядро 5.x):
 * HDR/SDR-режимы, которые система и плеер выставляют сами. Демон их только
 * читает и показывает в журнале/API — политику HDR он не трогает (см. README).
 * Источник — `meson_display_client -r dumpDisplayAttribute`. */
typedef struct
{
	bool valid;		/* удалось прочитать и разобрать */
	int mode;		/* HDR Mode */
	int policy;		/* HDR Policy */
	int priority;		/* HDR Priority */
	int sdr_mode;		/* SDR Mode */
	int force_hdr;		/* FORCE HDR Mode */
} hdrinfo_t;

/* Разбор вывода dumpDisplayAttribute (чистая функция, host-тест).
 * info обнуляется; возврат — разобрано ли хоть одно поле. */
bool hdrinfo_parse (const char *text, hdrinfo_t *info);

/* Прочитать атрибуты через вендорский клиент (cli может быть NULL или пустым —
 * тогда данные помечаются недействительными). Возврат: удалось ли прочитать. */
bool hdrinfo_refresh (const char *cli);

/* Последние прочитанные значения (никогда не NULL; valid=false — не читали). */
const hdrinfo_t *hdrinfo_get (void);

/* Строка в журнал: префикс + значения (или сообщение о недоступности). */
void hdrinfo_log (const char *prefix);

#endif /* HDRINFO_H */
