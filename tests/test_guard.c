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

	printf ("guard: OK\n");
	return 0;
}
