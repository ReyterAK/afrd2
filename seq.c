#include "seq.h"
#include "fps_class.h"

seq_step_t seq_settled (seq_t *st, int vhz, int expect_hz,
                        int mid_hz, bool mid_is_double)
{
	seq_step_t r = {SEQ_NOTHING};

	/* Сдались — до конца видео режим не трогаем ни по какому поводу (I2).
	 * Раньше это держалось флагом в цикле, и его снимали перепроверка замера,
	 * подсказка плеера и «режим не найден»; здесь единственный способ снять —
	 * seq_new_video(). */
	if (st->gave_up)
		return r;

	/* Расхождения нет: либо частота совпала по группе (23.976 и 24 — одна
	 * группа), либо сверять нечего (узел не читается). Второй случай — не
	 * «проверено»: об этом отдельно сообщает вызывающий, но формат всё равно
	 * ставится только здесь, после сверки (I3). */
	bool bad = (expect_hz > 0) && (vhz > 0) && fps_group_differs (expect_hz, vhz);

	if (!bad)
	{
		st->state = SEQ_IDLE;
		r.action = SEQ_SET_FORMAT;
		return r;
	}

	switch (st->state)
	{
		case SEQ_IDLE:
			/* Узел vinfo обновляется асинхронно: подтверждаем повторным
			 * чтением через 800 мс, иначе ложное срабатывание на каждом
			 * переключении. */
			st->state = SEQ_CONFIRM;
			r.action = SEQ_RECHECK;
			return r;

		case SEQ_CONFIRM:
			st->state = SEQ_RETRIED;
			r.action = SEQ_RETRY;
			return r;

		default:
			break;
	}

	/* Повтор не помог. Остаться на удвоении можно только если это правда
	 * удвоение: иначе это исходный режим, и его возвращают штатным путём
	 * (вместе с цветовым профилем), иначе «оставляю удвоение» врёт в журнале,
	 * а профиль остаётся чужим. */
	st->state = SEQ_IDLE;
	st->gave_up = true;

	if (mid_is_double && (mid_hz > 0) && (vhz == mid_hz))
	{
		r.action = SEQ_KEEP_MID;
		return r;
	}

	r.action = SEQ_RESTORE;
	return r;
}

seq_action_t seq_retry_result (seq_t *st, bool mid_applied, bool mid_is_double)
{
	st->state = SEQ_IDLE;
	st->gave_up = true;

	/* Целевой режим не применился, но промежуточный (удвоение) — да: экран
	 * уже в нём, и это лучше исходного 60 Гц для 25-к/с контента. */
	if (mid_applied && mid_is_double)
		return SEQ_KEEP_MID;

	return SEQ_RESTORE;
}

void seq_note_applied (seq_t *st)
{
	st->state = SEQ_IDLE;
}

void seq_new_video (seq_t *st)
{
	st->state = SEQ_IDLE;
	st->gave_up = false;
}

bool seq_gave_up (const seq_t *st)
{
	return st->gave_up;
}
