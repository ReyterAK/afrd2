/*
 * AFRd2: сеанс «AFR по требованию» — логика без побочных эффектов.
 *
 * Разбор заявки — журнал §84, ответы прошивки — docs/afr-on-demand-answers.md.
 * Здесь только состояние и правила: срок, продление, отмена, лимит частоты.
 * Переключение режима делает обычный цикл демона (afrd.c), который включает
 * «сеансовые» гейты, пока burst_running() истинно.
 */

#include "burst.h"

int burst_clamp_ms (int ms)
{
	if (ms <= 0)
		return BURST_MS_DEFAULT;
	if (ms < BURST_MS_MIN)
		return BURST_MS_MIN;
	if (ms > BURST_MS_MAX)
		return BURST_MS_MAX;

	return ms;
}

void burst_reset (burst_t *b)
{
	if (!b)
		return;

	b->pending = false;
	b->active = false;
	b->cancel = false;
	b->ms = BURST_MS_DEFAULT;
	b->deadline_us = 0;
	b->last_req_us = 0;
	b->hz = 0;
}

burst_req_t burst_request (burst_t *b, int ms, unsigned long long now_us)
{
	if (!b)
		return BURST_REQ_TOO_SOON;

	/* Лимит частоты: защита от дребезга кнопки и залипшего вызова.
	 * Отмена под лимит не попадает — иначе кнопка «выключить» не сработает
	 * в ту же секунду, когда включили. */
	if ((ms > 0) && b->last_req_us && ((now_us - b->last_req_us) < BURST_MIN_GAP_US))
		return BURST_REQ_TOO_SOON;

	if (ms < 0)
		ms = burst_clamp_ms (0);	/* умолчание */

	if (ms == 0)
	{
		b->last_req_us = now_us;

		/* Ещё не начали — отменять нечего, просто снимаем запрос. */
		if (b->pending && !b->active)
		{
			b->pending = false;
			b->deadline_us = 0;
			return BURST_REQ_CANCELLED;
		}

		if (!b->active)
			return BURST_REQ_OK;

		/* Сеанс идёт: просим цикл закончить его штатно (возврат режима).
		 * Сбрасывать active здесь нельзя — режим остался бы уведённым. */
		b->cancel = true;
		return BURST_REQ_CANCELLED;
	}

	b->ms = burst_clamp_ms (ms);
	b->deadline_us = now_us + (unsigned long long)b->ms * 1000ULL;
	b->last_req_us = now_us;
	/* Повтор во время сеанса — продление: сеанс остаётся активным,
	 * меняется только дедлайн. */
	if (!b->active)
		b->pending = true;

	return BURST_REQ_OK;
}

bool burst_take_pending (burst_t *b, unsigned long long now_us)
{
	if (!b || !b->pending)
		return false;

	b->pending = false;
	b->active = true;
	/* Срок отсчитывается от момента старта сеанса, а не от команды: между
	 * ними стоит переинициализация демона (платформа, таблица режимов) —
	 * на ATV11 это секунды, и вычитать их из сеанса незачем. */
	b->deadline_us = now_us + (unsigned long long)b->ms * 1000ULL;

	return true;
}

bool burst_cancel_requested (const burst_t *b)
{
	return b && b->cancel;
}

bool burst_running (const burst_t *b)
{
	return b && b->active;
}

bool burst_expired (const burst_t *b, unsigned long long now_us)
{
	return b && b->active && b->deadline_us && (now_us >= b->deadline_us);
}

void burst_finish (burst_t *b)
{
	if (!b)
		return;

	b->pending = false;
	b->active = false;
	b->cancel = false;
	b->deadline_us = 0;
}

int burst_left_ms (const burst_t *b, unsigned long long now_us)
{
	if (!b || !b->active || !b->deadline_us || (now_us >= b->deadline_us))
		return 0;

	return (int)((b->deadline_us - now_us) / 1000ULL);
}

const char *burst_state_name (const burst_t *b)
{
	if (!b)
		return "idle";
	if (b->active)
		return "active";
	if (b->pending)
		return "pending";

	return "idle";
}
