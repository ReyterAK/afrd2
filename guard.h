#ifndef GUARD_H
#define GUARD_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
	GUARD_NONE = 0,
	GUARD_SWITCH,
	GUARD_RESTORE,
} guard_action_t;

/* Решение по текущей ситуации. orig_sf_id/held — состояние между вызовами.
 * want_sf_id == cur_sf_id -> ничего не делаем (не мешаем приложениям со своим AFR). */
guard_action_t guard_decide (bool video_active, int want_sf_id, int cur_sf_id,
                             int *orig_sf_id, bool *held);

/* Нужно ли возвращать исходный режим, когда видео закончилось.
 *
 * restore_on_stop — ключ switch.restore.on.stop;
 * clamp_mechanics — механики зажима включены (switch.bump);
 * pkg_in_clamp_list — плеер, который вёл видео, есть в switch.bump.pkg.
 *
 * Плееру из списка зажима возврат не нужен: режим поднимется до максимума на
 * старте следующего ролика (switch.bump), а лишняя смена режима — это мигание.
 * Остальным возвращаем, иначе следующий ролик начнётся с низкой частоты, а
 * поднимать её будет некому (журнал §47.6). Решение владельца 30.10.
 *
 * Возврат: true — вернуть сейчас; false — оставить режим (демон вернёт его,
 * когда плеер уйдёт с переднего плана). */
bool guard_restore_allowed (bool restore_on_stop, bool clamp_mechanics,
                            bool pkg_in_clamp_list);

/* Вызывается после успешного возврата исходного режима. */
void guard_note_restored (bool *held);

/* Какой режим вернуть при старте по «записке» прошлого экземпляра (состояние
 * в разделяемой памяти): если режим был удержан, а демон погиб (kill -9,
 * паника ядра), вернуть его больше некому — SurfaceFlinger сам режим не
 * сбрасывает, а флаг backdoor система гасит за секунды, и по нему удержание
 * уже не опознать (живая проверка 2026-10-08).
 * Возврат: частота в формате 24.8 или 0 — возвращать нечего. */
int guard_recover_hz (bool prev_switched, uint32_t prev_original_hz);

#endif
