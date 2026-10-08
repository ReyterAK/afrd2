#include <assert.h>
#include <stdio.h>
#include "../guard.h"

int main (void)
{
	int orig = -1;
	bool held = false;

	/* режим уже подходит -> ничего не делаем (важно для приложений со своим AFR) */
	assert (guard_decide (true, 6, 6, &orig, &held) == GUARD_NONE);
	assert (!held);

	/* режим не подходит -> переключаем и запоминаем исходный */
	assert (guard_decide (true, 6, 0, &orig, &held) == GUARD_SWITCH);
	assert (orig == 0 && held);

	/* повторный вызов с той же целью -> ничего */
	assert (guard_decide (true, 6, 6, &orig, &held) == GUARD_NONE);

	/* видео кончилось -> возвращаем */
	assert (guard_decide (false, 0, 6, &orig, &held) == GUARD_RESTORE);
	assert (orig == 0);

	/* после возврата флаг снят */
	guard_note_restored (&held);
	assert (!held);

	/* стоп без удержания -> ничего */
	assert (guard_decide (false, 0, 0, &orig, &held) == GUARD_NONE);

	/* Восстановление режима при старте по «записке» прошлого экземпляра:
	 * если демона убили (kill -9, паника) с удержанным режимом, вернуть его
	 * больше некому — SurfaceFlinger сам режим не сбрасывает, а флаг backdoor
	 * система гасит за секунды. */
	assert (guard_recover_hz (false, 15345) == 0);	/* режим не удерживали */
	assert (guard_recover_hz (false, 0) == 0);
	assert (guard_recover_hz (true, 0) == 0);	/* частоты нет — возвращать нечем */
	assert (guard_recover_hz (true, 15345) == 15345);	/* 59.94 Гц */
	assert (guard_recover_hz (true, 6139) == 6139);		/* 23.98 Гц */

	printf ("guard: OK\n");
	return 0;
}
