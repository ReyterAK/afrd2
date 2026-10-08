#include "pause.h"

pause_action_t pause_step (bool paused, bool held, bool video, int cur_sf_id,
                           int held_sf_id, unsigned long long stall_us,
                           int delay_ms, pause_state_t *st)
{
	/* Пауза пережила предел — считаем её концом видео. Условие держится,
	 * только пока режим удержан: после возврата guard снимает удержание,
	 * и на той же паузе решение больше не принимается (одно на паузу). */
	if (paused && held && (delay_ms > 0) &&
	    (stall_us >= (unsigned long long) delay_ms * 1000ULL) &&
	    (held_sf_id >= 0))
	{
		st->armed = true;
		st->sf_id = held_sf_id;
		return PAUSE_END;
	}

	/* Счётчик снова растёт: возвращаем удержанный режим без нового замера —
	 * частота у того же видео уже известна, окно наблюдения ей не нужно. */
	if (st->armed && !paused && video && (st->sf_id >= 0) &&
	    (cur_sf_id != st->sf_id))
		return PAUSE_RESUME;

	return PAUSE_NONE;
}

void pause_forget (pause_state_t *st)
{
	st->armed = false;
	st->sf_id = -1;
}
