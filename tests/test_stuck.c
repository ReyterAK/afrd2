#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../stuck.h"

/* Сторож залипшего канала: таблица решений и состояние. Живьём состояние
 * «плеер ушёл, а канал декодера залип» воспроизводится только руками, поэтому
 * единственная проверка логики — здесь. Тест гоняет настоящий модуль, которым
 * пользуется демон (а не его копию), поэтому расхождению взяться неоткуда. */

#define VLC	"org.videolan.vlc"
#define SETTINGS	"com.android.tv.settings"

static void test_blind (void)
{
	stuck_state_t st;

	/* Отказ пробы не снимает удержание и не подменяет пакет — только строка
	 * в журнал, и то один раз на видео. */
	stuck_reset (&st);
	assert (stuck_step (&st, -1, "") == STUCK_BLIND);
	assert (stuck_step (&st, -2, "") == STUCK_NONE);	/* сказали один раз */
	assert (st.pkg [0] == 0 && st.mismatch == 0);
	assert (stuck_step (&st, -2, VLC) == STUCK_NONE);	/* вывод при отказе не значим */

	/* Отказ при уже запомненном пакете — тоже без снятия удержания */
	stuck_reset (&st);
	stuck_set_pkg (&st, VLC);
	assert (stuck_step (&st, -1, "") == STUCK_BLIND);
	assert (st.mismatch == 0);
}

static void test_fill (void)
{
	stuck_state_t st;

	/* Проба удалась, пакет не запомнен: заполняем, а не считаем «уходом» */
	stuck_reset (&st);
	assert (stuck_step (&st, 0, VLC) == STUCK_FILL_PKG);
	assert (strcmp (st.pkg, VLC) == 0);
	assert (st.mismatch == 0);

	/* Следующая проверка с тем же плеером — уже обычное совпадение */
	assert (stuck_step (&st, 0, VLC) == STUCK_NONE);
	assert (st.mismatch == 0);

	/* Пустая проба на заполнении оставила бы пустой пакет — заполнять нечего */
	stuck_reset (&st);
	assert (stuck_step (&st, 0, "") == STUCK_FILL_PKG);
	assert (st.pkg [0] == 0);

	/* Как только пакет запомнен, FILL больше не выдаётся */
	stuck_reset (&st);
	stuck_set_pkg (&st, VLC);
	assert (stuck_step (&st, 0, VLC) != STUCK_FILL_PKG);
}

static void test_give_up (void)
{
	stuck_state_t st;

	/* Настоящая пауза: сколько бы раз ни проверяли — удержание на месте */
	stuck_reset (&st);
	stuck_set_pkg (&st, VLC);
	for (int i = 0; i < 7; i++)
		assert (stuck_step (&st, 0, VLC) == STUCK_NONE);
	assert (st.mismatch == 0);

	/* Два несовпадения подряд — снимаем; порог берём из константы */
	stuck_reset (&st);
	stuck_set_pkg (&st, VLC);
	assert (stuck_step (&st, 0, SETTINGS) == STUCK_NONE);
	assert (st.mismatch == 1);
	assert (stuck_step (&st, 0, SETTINGS) == STUCK_GIVE_UP);
	assert (st.mismatch == STUCK_CONFIRM);

	/* Порог не должен выродиться в «никогда» */
	assert (STUCK_CONFIRM >= 2);
}

static void test_sequences (void)
{
	stuck_state_t st;

	/* Сценарий дыры 1: стартовая проба не удалась, при проверке она удалась —
	 * пакет заполняется, и только со следующей проверки считаются уходы. */
	stuck_reset (&st);
	assert (stuck_step (&st, -2, "") == STUCK_BLIND);
	assert (stuck_step (&st, 0, VLC) == STUCK_FILL_PKG);
	assert (stuck_step (&st, 0, SETTINGS) == STUCK_NONE);
	assert (stuck_step (&st, 0, SETTINGS) == STUCK_GIVE_UP);

	/* Вернулись в плеер — накопленное несовпадение сброшено */
	stuck_reset (&st);
	stuck_set_pkg (&st, VLC);
	assert (stuck_step (&st, 0, SETTINGS) == STUCK_NONE);
	assert (stuck_step (&st, 0, VLC) == STUCK_NONE);
	assert (st.mismatch == 0);
	assert (stuck_step (&st, 0, SETTINGS) == STUCK_NONE);
	assert (st.mismatch == 1);

	/* Одиночный отказ пробы между несовпадениями ничего не сбрасывает:
	 * иначе сторож не накопил бы двух подряд при «моргающем» дампе */
	stuck_reset (&st);
	stuck_set_pkg (&st, VLC);
	assert (stuck_step (&st, 0, SETTINGS) == STUCK_NONE);
	assert (stuck_step (&st, -2, "") == STUCK_BLIND);
	assert (stuck_step (&st, 0, SETTINGS) == STUCK_GIVE_UP);

	/* Новое видео: состояние забыто, старый плеер не «уход», а другой — уход */
	stuck_reset (&st);
	assert (st.pkg [0] == 0 && st.mismatch == 0 && !st.blind_logged);
	assert (stuck_step (&st, 0, VLC) == STUCK_FILL_PKG);
	assert (stuck_step (&st, 0, SETTINGS) == STUCK_NONE);	/* одно несовпадение */
	stuck_reset (&st);
	assert (st.mismatch == 0);
	assert (stuck_step (&st, 0, SETTINGS) == STUCK_FILL_PKG);
}

int main (void)
{
	test_blind ();
	test_fill ();
	test_give_up ();
	test_sequences ();
	printf ("stuck: OK\n");
	return 0;
}
