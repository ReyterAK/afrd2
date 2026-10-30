#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../burst.h"

/* Моменты времени для проверок, мкс. */
#define T0	1000000000ULL
#define MS(x)	((unsigned long long)(x) * 1000ULL)

int main (void)
{
	burst_t b;
	burst_reset (&b);

	/* умолчание и допуск: 40 с, 15…120 с */
	assert (burst_clamp_ms (0) == BURST_MS_DEFAULT);
	assert (burst_clamp_ms (40000) == 40000);
	assert (burst_clamp_ms (1000) == BURST_MS_MIN);
	assert (burst_clamp_ms (15000) == 15000);
	assert (burst_clamp_ms (120000) == 120000);
	assert (burst_clamp_ms (999999) == BURST_MS_MAX);

	/* пустое состояние */
	assert (!burst_running (&b));
	assert (!burst_expired (&b, T0));
	assert (burst_left_ms (&b, T0) == 0);
	assert (burst_state_name (&b)[0] == 'i');

	/* первая команда: pending, ещё не сеанс */
	assert (burst_request (&b, 20000, T0) == BURST_REQ_OK);
	assert (b.pending && !b.active);
	assert (!strcmp (burst_state_name (&b), "pending"));

	/* пассивный цикл забирает pending — теперь сеанс идёт */
	assert (burst_take_pending (&b, T0));
	assert (burst_running (&b));
	assert (!b.pending);
	assert (!strcmp (burst_state_name (&b), "active"));
	assert (burst_left_ms (&b, T0) == 20000);

	/* истечение срока */
	assert (!burst_expired (&b, T0 + MS (19999)));
	assert (burst_expired (&b, T0 + MS (20000)));
	assert (burst_left_ms (&b, T0 + MS (20000)) == 0);
	burst_finish (&b);
	assert (!burst_running (&b));
	assert (!burst_expired (&b, T0 + MS (30000)));

	/* лимит частоты: вторая команда в ту же секунду отклоняется */
	burst_reset (&b);
	assert (burst_request (&b, 40000, T0) == BURST_REQ_OK);
	assert (burst_request (&b, 40000, T0 + MS (500)) == BURST_REQ_TOO_SOON);
	assert (burst_request (&b, 40000, T0 + MS (1000)) == BURST_REQ_OK);

	/* отмена во время сеанса НЕ сбрасывает его мгновенно: сеанс обязан
	 * закончиться штатно, иначе режим останется уведённым (найдено живьём) */
	burst_reset (&b);
	assert (burst_request (&b, 40000, T0) == BURST_REQ_OK);
	burst_take_pending (&b, T0);
	assert (burst_request (&b, 0, T0 + MS (10)) == BURST_REQ_CANCELLED);
	assert (burst_running (&b));
	assert (burst_cancel_requested (&b));
	burst_finish (&b);
	assert (!burst_running (&b));
	assert (!burst_cancel_requested (&b));
	assert (!burst_expired (&b, T0 + MS (100000)));

	/* отмена до старта сеанса (pending) — просто снимает запрос */
	burst_reset (&b);
	assert (burst_request (&b, 40000, T0) == BURST_REQ_OK);
	assert (burst_request (&b, 0, T0 + MS (10)) == BURST_REQ_CANCELLED);
	assert (!b.pending && !burst_running (&b));

	/* отмена без активного сеанса — не ошибка */
	assert (burst_request (&b, 0, T0 + MS (2000)) == BURST_REQ_OK);

	/* повтор во время сеанса продлевает срок от текущего момента */
	burst_reset (&b);
	burst_request (&b, 20000, T0);
	burst_take_pending (&b, T0);
	assert (burst_request (&b, 30000, T0 + MS (10000)) == BURST_REQ_OK);
	assert (burst_running (&b));
	assert (!burst_expired (&b, T0 + MS (35000)));
	assert (burst_expired (&b, T0 + MS (40000)));

	/* продление до окончания срока не «запускает второй» сеанс:
	 * pending не поднимается */
	assert (!b.pending);

	/* команда до старта сеанса и после паузы цикла: дедлайн не съеден
	 * переинициализацией демона (deadline <= now -> срок отсчитывается заново) */
	burst_reset (&b);
	burst_request (&b, 15000, T0);
	assert (burst_take_pending (&b, T0 + MS (5000)));
	assert (burst_left_ms (&b, T0 + MS (5000)) == 15000);

	/* take_pending без запроса ничего не делает */
	burst_reset (&b);
	assert (!burst_take_pending (&b, T0));
	assert (!burst_running (&b));

	printf ("burst: OK\n");
	return 0;
}
