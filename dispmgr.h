/*
 * AFRd2: пользовательский preferred-режим из dumpsys display (DisplayManager).
 *
 * id здесь — из пространства DisplayManager, а НЕ SurfaceFlinger: на стенде
 * `.50` mUserPreferredModeId=1 означает 1080p59.94, тогда как id 1 в таблице
 * SurfaceFlinger — 1080p60.00. Поэтому в транзакцию 1035 этот id отправлять
 * нельзя; нужны частота и геометрия, по которым режим ищется в таблице SF.
 */

#ifndef DISPMGR_H
#define DISPMGR_H

typedef struct
{
	int id;		/* id в пространстве DisplayManager (для справки) */
	int width;
	int height;
	int hz;		/* частота в 24.8 fixed-point */
} dispmgr_mode_t;

/* Разобрать вывод `dumpsys display` (или его отфильтрованный grep-ом вид):
 * строка `mUserPreferredModeId=<id>` и запись
 * `DisplayModeRecord{mMode={id=<id>, width=…, height=…, fps=…}}`.
 * Возврат: 0 — режим найден и заполнен, -1 — предпочтения нет/не разобралось. */
int dispmgr_preferred_parse (const char *dump, dispmgr_mode_t *out);

#endif
