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

int fps_rate_hz (int index);
const char *fps_rate_name (int index);
int fps_rate_index_for_hz (int hz);

#endif
