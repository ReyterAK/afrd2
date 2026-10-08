#include <assert.h>
#include <stdio.h>
#include "../pause.h"

/* «Терпение к паузе» (switch.delay.off на новом пути): пауза дольше предела
 * считается концом видео, а на возобновлении удержанный режим возвращается по
 * памяти, без нового замера. Живой замер, из которого это выросло, — журнал
 * §32.1 (залипший канал от настоящей паузы неотличим). */

int main (void)
{
	pause_state_t st;
	pause_forget (&st);
	assert (!st.armed && (st.sf_id == -1));

	/* режим не удержан -> предел не применяется */
	assert (pause_step (true, false, true, 0, -1, 60000000ULL, 10000, &st) == PAUSE_NONE);
	assert (!st.armed);

	/* пауза короче предела -> держим, как раньше */
	assert (pause_step (true, true, true, 15, 15, 9000000ULL, 10000, &st) == PAUSE_NONE);
	assert (!st.armed);

	/* ровно на пределе -> пауза считается концом видео, режим запоминается */
	assert (pause_step (true, true, true, 15, 15, 10000000ULL, 10000, &st) == PAUSE_END);
	assert (st.armed && (st.sf_id == 15));

	/* предел 0 -> держим бессрочно (прежнее поведение) */
	pause_forget (&st);
	assert (pause_step (true, true, true, 15, 15, 3600000000ULL, 0, &st) == PAUSE_NONE);
	assert (!st.armed);

	/* возобновление -> вернуть удержанный режим, ровно один раз */
	pause_forget (&st);
	assert (pause_step (true, true, true, 15, 15, 10000000ULL, 10000, &st) == PAUSE_END);
	assert (pause_step (false, false, true, 0, -1, 0, 10000, &st) == PAUSE_RESUME);
	/* режим на экране уже тот -> не трогаем */
	assert (pause_step (false, false, true, 15, -1, 0, 10000, &st) == PAUSE_NONE);

	/* видео ушло (декодер исчез) -> память паузы не применяется */
	pause_forget (&st);
	assert (pause_step (true, true, true, 15, 15, 20000000ULL, 10000, &st) == PAUSE_END);
	assert (pause_step (false, false, false, 0, -1, 0, 10000, &st) == PAUSE_NONE);

	/* второе срабатывание на том же видео: память не одноразовая */
	pause_forget (&st);
	assert (pause_step (true, true, true, 15, 15, 20000000ULL, 10000, &st) == PAUSE_END);
	assert (pause_step (false, false, true, 0, -1, 0, 10000, &st) == PAUSE_RESUME);
	assert (pause_step (true, true, true, 15, 15, 20000000ULL, 10000, &st) == PAUSE_END);

	/* сброс памяти (новое видео / конец видео): возобновления не будет */
	pause_forget (&st);
	assert (pause_step (true, true, true, 15, 15, 20000000ULL, 10000, &st) == PAUSE_END);
	pause_forget (&st);
	assert (pause_step (false, false, true, 0, -1, 0, 10000, &st) == PAUSE_NONE);

	/* вырожденные входы: неизвестный удержанный режим id -> решения нет */
	pause_forget (&st);
	assert (pause_step (true, true, true, -1, -1, 20000000ULL, 10000, &st) == PAUSE_NONE);
	assert (!st.armed);

	printf ("pause: OK\n");
	return 0;
}
