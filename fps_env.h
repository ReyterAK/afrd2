#ifndef FPS_ENV_H
#define FPS_ENV_H

#include <stdbool.h>

/*
 * Нижняя огибающая частоты кадров по выборкам счётчика декодера.
 *
 * Счётчик кадров на ATV14 завышен, и завышение односторонне: плеер повторно
 * декодирует кадры на границах DASH/HLS-сегментов, AV1-тракт добавляет свои —
 * но ничто кадры не отнимает. Поэтому истинная частота ≈ минимум наблюдённой
 * по подокнам: шум и повторные кадры поднимают отдельные подокна, но не
 * опускают все.
 *
 * Живой замер 2026-10-01 (13 роликов, штатный YouTube, 30 с на ролик, граница
 * класса 24 -> 25 = 24.49 fps): решение по одному окну даёт верный класс для
 * 24-фпс контента в 14 % случаев, минимум по 5-с окнам (20 с наблюдения) —
 * в 86 %, для 25-фпс — 100 %; все ошибки в сторону 25 Гц (безопасная:
 * пропуск кадра заметнее повтора).
 */

#define FPS_ENV_MAX	128

typedef struct
{
	unsigned long long t_us;	/* середина чтения vdec_status */
	unsigned long long frames;	/* frame count */
	unsigned long long unc_us;	/* длительность самого чтения */
} fps_sample_t;

typedef struct
{
	fps_sample_t	s [FPS_ENV_MAX];
	int		n;
} fps_env_t;

/* Начать наблюдение заново (старт/стоп видео, пауза, принятое решение). */
void fps_env_reset (fps_env_t *e);

/*
 * Добавить выборку. Возврат false — счётчик не вырос (пауза) либо сбросился
 * (новое видео): кольцо сброшено, наблюдение начинается с этой выборки.
 * Вызывающий может игнорировать результат.
 */
bool fps_env_push (fps_env_t *e, unsigned long long t_us,
                   unsigned long long frames, unsigned long long unc_us);

/* Прошёл ли горизонт наблюдения с первой выборки. */
bool fps_env_ready (const fps_env_t *e, unsigned long long horizon_us);

/*
 * Решение по нижней огибающей.
 *   horizon_us — конец наблюдения (подокна дальше не рассматриваются);
 *   skip_us    — прогрев: подокна, начинающиеся раньше, не рассматриваются
 *                (замер сразу после старта потока бывает занижен: VLC отдавал
 *                первые кадры 60-фпс канала так, что окно давало 50.3 fps);
 *   sub_us     — длина подокна (окно = первые выборки, покрывающие sub_us);
 *   factor, offset — поправки, см. fps_classify_window_corrected.
 * Возврат: 1 — решение есть (*hz_out), 0 — данных мало/окна неоднозначны.
 */
int fps_env_decide (const fps_env_t *e, unsigned long long horizon_us, unsigned long long skip_us,
                    unsigned long long sub_us, double factor, double offset, int *hz_out);

#endif
