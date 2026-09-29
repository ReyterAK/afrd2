#include "guard.h"

guard_action_t guard_decide (bool video_active, int want_sf_id, int cur_sf_id,
                             int *orig_sf_id, bool *held)
{
	if (!video_active)
	{
		if (*held)
		{
			/* вернуть исходный режим, только если мы его меняли */
			return (*orig_sf_id >= 0) ? GUARD_RESTORE : GUARD_NONE;
		}
		return GUARD_NONE;
	}

	if (want_sf_id < 0 || want_sf_id == cur_sf_id)
		return GUARD_NONE;

	if (!*held)
	{
		*orig_sf_id = cur_sf_id;	/* запомнить один раз */
		*held = true;
	}

	return GUARD_SWITCH;
}

void guard_note_restored (bool *held)
{
	*held = false;
}
