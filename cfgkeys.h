#ifndef CFGKEYS_H
#define CFGKEYS_H

/*
 * Применимость ключей конфига к путям работы демона.
 *
 * У демона три пути: старый (ядра 3.14/4.9, Android 6-9: uevent'ы + запись
 * в /sys/class/display/mode), вендорный режим (Android 10-11: SystemControl
 * через afrd-disp.sh) и новый ATV14 (ядра 5.x, Android 12-14: опрос
 * vdec_status + backdoor-транзакция SurfaceFlinger). Часть ключей действует
 * только на одном из них, а часть — вообще только в приложении.
 *
 * Задача модуля — сделать это явным: при старте демон проверяет свой файл
 * конфига и пишет в лог каждый ключ, который на выбранном пути не действует
 * (или вообще неизвестен). Так настройка из GUI не может «молча потеряться».
 */

/* Пути, на которых может действовать ключ. Их три, а не два: на Android 10/11
 * режим задаёт вендорный SystemControl (PLAT_ATV11), на Android 12-14 —
 * backdoor-транзакция SurfaceFlinger (PLAT_ATV14). Ключи нового цикла
 * (`run_atv14`) обслуживают оба, но у вендорного пути есть свои
 * (`vendor.disp.*`), а часть sysfs-ключей нового пути не касается. */
#define CFGKEY_LEGACY	1u	/* старый путь (Android 6-9) */
#define CFGKEY_VENDOR	2u	/* вендорный режим (Android 10-11) */
#define CFGKEY_ATV14	4u	/* backdoor SurfaceFlinger (Android 12-14) */
#define CFGKEY_APP	8u	/* ключ настройки приложения: демон его не читает */
/* общий цикл нового пути (Android 10-14), включая вендорный режим */
#define CFGKEY_NEWPATH	(CFGKEY_VENDOR | CFGKEY_ATV14)
/* действует на всех трёх путях */
#define CFGKEY_BOTH	(CFGKEY_LEGACY | CFGKEY_VENDOR | CFGKEY_ATV14)

/* Причина, по которой ключ на данном пути (CFGKEY_LEGACY, CFGKEY_VENDOR или
 * CFGKEY_ATV14) не действует, либо NULL, если действует. */
const char *cfgkeys_note (const char *key, unsigned path);

/* Разбор файла конфига: для каждого ключа, который на данном пути не
 * действует, вызывается warn () с готовой строкой сообщения.
 * Возврат: число таких ключей; -1 — файл не прочитан. */
int cfgkeys_audit (const char *filename, unsigned path,
                   void (*warn) (const char *msg, void *ctx), void *ctx);

#endif
