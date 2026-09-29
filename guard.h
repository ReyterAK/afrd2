#ifndef GUARD_H
#define GUARD_H

#include <stdbool.h>

typedef enum
{
	GUARD_NONE = 0,
	GUARD_SWITCH,
	GUARD_RESTORE,
} guard_action_t;

/* Решение по текущей ситуации. orig_sf_id/held — состояние между вызовами.
 * want_sf_id == cur_sf_id -> ничего не делаем (не мешаем приложениям со своим AFR). */
guard_action_t guard_decide (bool video_active, int want_sf_id, int cur_sf_id,
                             int *orig_sf_id, bool *held);

/* Вызывается после успешного возврата исходного режима. */
void guard_note_restored (bool *held);

#endif
