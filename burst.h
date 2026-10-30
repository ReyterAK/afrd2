#ifndef BURST_H
#define BURST_H

#include <stdbool.h>

/* AFR «по требованию»: сеанс по кнопке (журнал §84, заявка slimBOXtv).
 *
 * Демон живёт в пассивном режиме (`enable=0`), внешнее приложение или скрипт
 * присылает `burst <ms>` — демон входит в обычный активный цикл с дедлайном,
 * делает своё дело (замер → одно переключение), возвращает прежний режим и
 * уходит обратно в пассив.
 *
 * Здесь только логика сеанса — без sysfs, без сети, без журнала: так её можно
 * проверить host-тестом. Связь с циклом демона — в afrd.c.
 *
 * Умолчание и допуск — ответ прошивки на вопрос 3: 40 000 мс, 15 000…120 000.
 * Повтор команды продлевает срок, а не запускает второй сеанс; лимит частоты
 * «не чаще раза в секунду» — ответ на вопрос 10. */

#define BURST_MS_DEFAULT	40000
#define BURST_MS_MIN		15000
#define BURST_MS_MAX		120000
#define BURST_MIN_GAP_US	1000000ULL

/* Приведение запрошенной длительности к допуску: 0 -> умолчание,
 * меньше минимума -> минимум, больше максимума -> максимум. */
int burst_clamp_ms (int ms);

typedef struct
{
	bool pending;			/* просили сеанс, цикл ещё не перешёл в активный */
	bool active;			/* сеанс идёт */
	bool cancel;			/* просили отменить: сеанс закончится штатно —
					 * с возвратом режима, а не мгновенным сбросом флагов */
	int ms;				/* текущий срок, мс */
	unsigned long long deadline_us;	/* когда сеанс заканчивается */
	unsigned long long last_req_us;	/* для лимита частоты вызовов */
	int hz;				/* принятая частота (24.8), 0 — решения ещё нет */
} burst_t;

typedef enum
{
	BURST_REQ_OK = 0,		/* принято (старт или продление) */
	BURST_REQ_CANCELLED,		/* отменено командой burst 0 */
	BURST_REQ_TOO_SOON,		/* лимит частоты вызовов */
} burst_req_t;

void burst_reset (burst_t *b);

/* Команда `burst <ms>`: ms < 0 — умолчание (burst.ms), ms == 0 — отмена.
 * Повтор во время сеанса продлевает срок от текущего момента (а не от
 * прежнего дедлайна): пользователь нажал ещё раз — значит хочет ещё столько. */
burst_req_t burst_request (burst_t *b, int ms, unsigned long long now_us);

/* Пассивный цикл: пора ли уходить в сеанс. Переводит pending -> active. */
bool burst_take_pending (burst_t *b, unsigned long long now_us);

/* Просили отменить сеанс (команда `burst 0`). Отмена — не мгновенный сброс:
 * сеанс обязан закончиться штатно, иначе режим останется уведённым. */
bool burst_cancel_requested (const burst_t *b);

/* Сеанс идёт? */
bool burst_running (const burst_t *b);

/* Срок истёк (сеанс идёт и дедлайн прошёл). */
bool burst_expired (const burst_t *b, unsigned long long now_us);

/* Сеанс закончен (по сроку, остановке видео или отмене). */
void burst_finish (burst_t *b);

/* Остаток срока, мс (0 — сеанс не идёт). */
int burst_left_ms (const burst_t *b, unsigned long long now_us);

/* Имя состояния для журнала, API и broadcast: "idle", "pending", "active".
 * Переход active -> idle приложение и прошивка читают как «сеанс закончен»:
 * отдельного состояния «закончен» нет, потому что оно неотличимо от «не
 * запускали» и требует памяти о прошлом сеансе ради одной строки. */
const char *burst_state_name (const burst_t *b);

#endif
