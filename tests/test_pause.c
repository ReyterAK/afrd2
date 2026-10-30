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

	/* Один ключ на поведение при паузе (pause.mode): три состояния вместо
	 * тумблера и числа в разных категориях (журнал §99.3). */
	{
		int keep = -1, pat = -1;

		assert (pause_mode_apply ("keep", &keep, &pat) == 1);
		assert (keep == 1 && pat == 0);

		/* timeout берёт switch.delay.off, а при нуле — умолчание 20 с */
		keep = -1; pat = 0;
		assert (pause_mode_apply ("timeout", &keep, &pat) == 1);
		assert (keep == 1 && pat == PAUSE_MODE_DEFAULT_TIMEOUT_MS);

		keep = -1; pat = 5000;
		assert (pause_mode_apply ("timeout", &keep, &pat) == 1);
		assert (keep == 1 && pat == 5000);

		keep = -1; pat = 5000;
		assert (pause_mode_apply ("off", &keep, &pat) == 1);
		assert (keep == 0 && pat == 0);

		/* нет ключа / неизвестное значение — вызывающий берёт старые ключи */
		keep = 7; pat = 7;
		assert (pause_mode_apply (NULL, &keep, &pat) == 0);
		assert (keep == 7 && pat == 7);
		assert (pause_mode_apply ("", &keep, &pat) == 0);
		assert (pause_mode_apply ("hold", &keep, &pat) == 0);
	}

	printf ("pause: OK\n");
	return 0;
}
