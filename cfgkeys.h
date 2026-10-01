#ifndef CFGKEYS_H
#define CFGKEYS_H

/*
 * Применимость ключей конфига к путям работы демона.
 *
 * У демона два пути: старый (ядра 3.14/4.9, Android 6-9: uevent'ы + запись
 * в /sys/class/display/mode) и новый ATV14 (ядра 5.x, Android 10-14: опрос
 * vdec_status + backdoor-транзакция SurfaceFlinger). Часть ключей действует
 * только на одном из них, а часть — вообще только в приложении.
 *
 * Задача модуля — сделать это явным: при старте демон проверяет свой файл
 * конфига и пишет в лог каждый ключ, который на выбранном пути не действует
 * (или вообще неизвестен). Так настройка из GUI не может «молча потеряться».
 */

/* Пути, на которых может действовать ключ. */
#define CFGKEY_LEGACY	1u	/* старый путь */
#define CFGKEY_ATV14	2u	/* новый путь (Android 10-14) */
#define CFGKEY_APP	4u	/* ключ настройки приложения: демон его не читает */
#define CFGKEY_BOTH	(CFGKEY_LEGACY | CFGKEY_ATV14)

/* Причина, по которой ключ на данном пути (CFGKEY_LEGACY или CFGKEY_ATV14)
 * не действует, либо NULL, если действует. */
const char *cfgkeys_note (const char *key, unsigned path);

/* Разбор файла конфига: для каждого ключа, который на данном пути не
 * действует, вызывается warn () с готовой строкой сообщения.
 * Возврат: число таких ключей; -1 — файл не прочитан. */
int cfgkeys_audit (const char *filename, unsigned path,
                   void (*warn) (const char *msg, void *ctx), void *ctx);

#endif
