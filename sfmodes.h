#ifndef SFMODES_H
#define SFMODES_H

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

/* Частота (24.8) по id режима. 0, если id не найден. */
int sfmodes_current_hz (const sf_mode_t *modes, int n, int id);

#endif
