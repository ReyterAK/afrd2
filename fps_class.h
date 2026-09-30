#ifndef FPS_CLASS_H
#define FPS_CLASS_H

#define FPS_RATE_COUNT 8

/* Оценка fps по двум замерам счётчика кадров декодера.
 * ts_* — CLOCK_MONOTONIC в микросекундах. 0 значит "измерить нельзя". */
double fps_estimate (unsigned long long frames_a, unsigned long long ts_a_us,
                     unsigned long long frames_b, unsigned long long ts_b_us);

/* Классификация измеренной частоты в ближайшую стандартную.
 * Возврат: fixed-point 24.8 Гц (напр. 23.976 -> 6138) или 0, если вне допуска. */
int fps_classify (double measured);

/* Группа режимов: частоты, которые на HDMI дают один и тот же режим.
 * 23.976 и 24 (как и 29.97/30, 59.94/60) различаются на 0.1% и выбираются
 * политикой fractional/exact, поэтому при решении «хватит ли разрешения окна»
 * считаются одной частотой. Возврат: 0 — нестандартная частота. */
int fps_rate_group (int hz);

/* Решение по окну замера счётчика кадров декодера.
 * Счётчик дискретен (±1 кадр за окно) и метки времени имеют неопределённость,
 * поэтому решение принимается, только если весь доверительный интервал
 * попадает в одну группу частот (fps_rate_group). Иначе окно надо расширять.
 *   frames_delta — прирост счётчика кадров между крайними выборками;
 *   dt_us        — интервал между выборками (середины чтений), мкс;
 *   dt_unc_us    — неопределённость интервала (±), мкс.
 * Возврат: 1 — решение есть (*hz_out = fixed-point 24.8 Гц); 0 — окно мало. */
int fps_classify_window (unsigned long long frames_delta, unsigned long long dt_us,
                         unsigned long long dt_unc_us, int *hz_out);

int fps_rate_hz (int index);
const char *fps_rate_name (int index);
int fps_rate_index_for_hz (int hz);

#endif
