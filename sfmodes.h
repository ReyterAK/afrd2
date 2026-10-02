#ifndef SFMODES_H
#define SFMODES_H

#include <stdbool.h>

#define SF_MODE_MAX	64

/* Режим SurfaceFlinger (то, что печатает dumpsys SurfaceFlinger). */
typedef struct
{
	int id;		/* DisplayModeId, он же аргумент транзакции 1035 */
	int width;
	int height;
	int hz;		/* fixed-point 24.8 */
} sf_mode_t;

/* Разбор вывода dumpsys. Возврат: число найденных режимов. */
int sfmodes_parse (const char *dump, sf_mode_t *out, int max);

/* Поиск id режима по частоте (24.8) и разрешению. -1, если нет. */
int sfmodes_find (const sf_mode_t *modes, int n, int hz, int width, int height);

/* Поиск id режима по одной частоте (24.8). Допуск 0.125 Гц; -1, если нет. */
int sfmodes_find_hz (const sf_mode_t *modes, int n, int hz);

/* Фильтр частот для выбора режима (например, mode.blacklist.rates).
 * Возврат: true — частота допустима; ctx — контекст вызывающего (может быть NULL). */
typedef bool (*sf_hz_filter_t) (int hz, void *ctx);

/* Выбранный режим: id, его частота (24.8) и кратность к запрошенной частоте. */
typedef struct
{
	int id;
	int hz;
	int rate_n;	/* 1 — точное совпадение, 2 — удвоение, 3 — утроение */
} sf_choice_t;

/* Выбор режима под частоту контента (24.8) — те же правила, что в legacy-пути
 * (см. rating в afrd.c): допускаются кратные частоты с ошибкой до ~4.3 % и
 * кратностью не выше 3; при prefer_exact == 0 предпочитаются кратные
 * (25 -> 50, 30 -> 60), при prefer_exact == 1 — точное совпадение.
 * Геометрия режима должна совпадать с запрошенной; частоты, для которых
 * allowed() вернула false, не рассматриваются. При равном рейтинге побеждает
 * режим, ближайший по частоте (пара 23.98/24.00).
 * Возврат: 0 — выбран (заполняет *out), -1 — подходящего режима нет. */
int sfmodes_select (const sf_mode_t *modes, int n, int hz, int width, int height,
                    int prefer_exact, sf_hz_filter_t allowed, void *ctx,
                    sf_choice_t *out);

/* Частота (24.8) по id режима. 0, если id не найден. */
int sfmodes_current_hz (const sf_mode_t *modes, int n, int id);

#endif
