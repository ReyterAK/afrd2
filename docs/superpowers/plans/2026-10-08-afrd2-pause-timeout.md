# AFRd2: «терпение к паузе» — `switch.delay.off` на новом пути (Android 10–14) — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** оживить `switch.delay.off` на новом пути демона как **предел удержания режима на паузе**: пауза дольше N мс считается концом видео (исходный режим возвращается), а на возобновлении удержанный режим возвращается по памяти — без нового замера.

**Architecture:** решение выносится в чистый модуль `pause.c` (образец — `guard.c`, `stuck.c`), проверяется host-тестом `tests/test_pause.c` и мутационно; в цикле `atv14_run` его вердикт подменяет `video_eff`, поэтому возврат идёт существующим путём `guard_decide → GUARD_RESTORE`, а память режима — через `want_sf_id`, минуя окно замера.

**Tech Stack:** C (host-тесты через `sh tests/run_tests.sh`), Android/Java (GUI + seeding), POSIX sh (стендовые скрипты).

**Spec:** журнал `/home/reyter/AI/afrd2-journal.md` §31.15 (семантика случая 3), §31.18 (план по шагам), §32.1 (замер: `status` и `media_session` паузу от залипшего канала не отличают) + решение владельца 2026-10-08: «опереться на `switch.delay.off` (оживить на ATV14)».

## Global Constraints

* Комментарии, трассы и тексты — по-русски (правило репозитория).
* Ключ ровно один: `switch.delay.off`, целое, мс. Новых строк в GUI не заводим (решение владельца: «у нас и без того настройки заполнены сложными опциями»).
* Смысл по пути: legacy (Android 6–9) — как было (задержка возврата после события «видео кончилось»); ATV14 — предел терпения к паузе. `0` на ATV14 = держать режим до конца паузы (прежнее поведение).
* Умолчание на ATV14 — `0` в `config/afrd2.ini` **и** `DEFAULT_SWITCH_DELAY_OFF_ATV14 = 0` в коде: поведение существующих пользователей не меняется, ключ включается осознанно.
* Уровни трасс: `PAUSE_END` — уровень 0 (возврат режима, видно в обычном журнале `log.enable=1`), `PAUSE_RESUME` — уровень 1.
* `CFGKEY_BOTH` для `switch.delay.off`; запись в списках «не действует» (`SettingsFragment.kInactiveOnAtv14`) убирается — расхождение ловит `tests/test_inactive.c`.
* Проверки без стенда обязательны (host-тесты + сборка); живая приёмка — на 192.168.88.50 (ATV14, 32-битный userspace).

## Review Focus

* **Пауза короче предела.** Ждём: режим держится, как раньше; переключения и строки `PAUSE_END` нет. Пин: `tests/test_pause.c`, случай `stall < delay`.
* **Возобновление после сработавшего предела.** Ждём: возврат к тому же режиму без нового замера (нет окна `fps.measure.envelope.*`). Пин: `tests/test_pause.c` (`PAUSE_RESUME` один раз) + живая проверка.
* **`switch.delay.off=0`.** Ждём: ничего не меняется (держит бессрочно, памяти нет). Пин: `tests/test_pause.c` (`PAUSE_NONE` при `delay=0`) + ключ умолчания профиля.
* **Новое видео.** Ждём: память паузы не переносится на другое видео (иначе вернём чужой режим, не измерив частоту). Пин: `pause_forget` в трёх точках + `tests/test_pause.c` (память сбрасывается → `PAUSE_RESUME` не срабатывает).
* **Граница по времени.** Ждём: ровно на пределе (равенство) — уже срабатывает; на 1 мс меньше — нет. Пин: `tests/test_pause.c`.

---

### Task 1: Модуль `pause.c` + host-тест

**Files:**
- Create: `pause.h`, `pause.c`
- Create: `tests/test_pause.c`
- Modify: `tests/run_tests.sh` (строка `run test_pause pause.c`)

**Interfaces:**
- Produces:
  * `typedef enum { PAUSE_NONE = 0, PAUSE_END, PAUSE_RESUME } pause_action_t;`
  * `typedef struct { bool armed; int sf_id; } pause_state_t;`
  * `pause_action_t pause_step (bool paused, bool held, bool video, int cur_sf_id, int held_sf_id, unsigned long long stall_us, int delay_ms, pause_state_t *st);`
  * `void pause_forget (pause_state_t *st);`

- [ ] **Step 1: Написать падающий тест** (`tests/test_pause.c`, стиль `tests/test_guard.c`: `assert` + `printf ("pause: OK\n")`)

```c
#include <assert.h>
#include <stdio.h>
#include "../pause.h"

int main (void)
{
	pause_state_t st;
	pause_forget (&st);
	assert (!st.armed && (st.sf_id == -1));

	/* режим не удержан -> предел не применяется */
	assert (pause_step (true, false, true, 0, -1, 60000000ULL, 10000, &st) == PAUSE_NONE);
	assert (!st.armed);

	/* пауза короче предела -> держим */
	assert (pause_step (true, true, true, 15, 15, 9000000ULL, 10000, &st) == PAUSE_NONE);
	assert (!st.armed);

	/* ровно на пределе -> конец видео, режим запоминается */
	assert (pause_step (true, true, true, 15, 15, 10000000ULL, 10000, &st) == PAUSE_END);
	assert (st.armed && (st.sf_id == 15));

	/* предел 0 -> держим бессрочно (прежнее поведение) */
	pause_forget (&st);
	assert (pause_step (true, true, true, 15, 15, 3600000000ULL, 0, &st) == PAUSE_NONE);
	assert (!st.armed);

	/* возобновление -> вернуть удержанный режим, ровно один раз */
	pause_forget (&st);
	assert (pause_step (true, true, true, 15, 15, 10000000ULL, 10000, &st) == PAUSE_END);
	assert (pause_step (false, false, true, 0, -1, 0, 10000, &st) == PAUSE_RESUME);
	/* режим уже тот -> не трогаем */
	assert (pause_step (false, false, true, 15, -1, 0, 10000, &st) == PAUSE_NONE);

	/* видео ушло -> память не применяется */
	pause_forget (&st);
	assert (pause_step (true, true, true, 15, 15, 20000000ULL, 10000, &st) == PAUSE_END);
	assert (pause_step (false, false, false, 0, -1, 0, 10000, &st) == PAUSE_NONE);

	/* второе срабатывание на том же видео (память не одноразовая) */
	pause_forget (&st);
	assert (pause_step (true, true, true, 15, 15, 20000000ULL, 10000, &st) == PAUSE_END);
	assert (pause_step (false, false, true, 0, -1, 0, 10000, &st) == PAUSE_RESUME);
	assert (pause_step (true, true, true, 15, 15, 20000000ULL, 10000, &st) == PAUSE_END);

	printf ("pause: OK\n");
	return 0;
}
```

- [ ] **Step 2: Убедиться, что тест падает по нужной причине**

Run: `printf 'run test_pause pause.c\n' >> tests/run_tests.sh` (вставить рядом с `run test_guard guard.c`), затем `cc -Wall -o out/host-tests/test_pause tests/test_pause.c pause.c`
Expected: ошибка компиляции — `pause.h: No such file or directory` (модуля нет).

- [ ] **Step 3: Объявить интерфейс** (`pause.h`) — заголовок с пояснением смысла ключа на двух путях (см. `guard.h`):

```c
#ifndef PAUSE_H
#define PAUSE_H

#include <stdbool.h>

/* «Терпение к паузе»: switch.delay.off на новом пути (Android 10-14).
 * ... (полный текст — в реализации) */
typedef enum { PAUSE_NONE = 0, PAUSE_END, PAUSE_RESUME } pause_action_t;
typedef struct { bool armed; int sf_id; } pause_state_t;

pause_action_t pause_step (bool paused, bool held, bool video, int cur_sf_id,
                           int held_sf_id, unsigned long long stall_us,
                           int delay_ms, pause_state_t *st);

void pause_forget (pause_state_t *st);

#endif
```

- [ ] **Step 4: Заглушка `pause.c`** — `pause_forget` сбрасывает состояние, `pause_step` возвращает `PAUSE_NONE`, затем `sh tests/test_pause.c`-прогон ожидаемо падает на первой же непустой проверке (`pause: ...` assert).

Run: `cc -Wall -o out/host-tests/test_pause tests/test_pause.c pause.c && ./out/host-tests/test_pause`
Expected: FAIL — `Assertion ... failed` на проверке «ровно на пределе».

- [ ] **Step 5: Реализация (GREEN)**

```c
pause_action_t pause_step (bool paused, bool held, bool video, int cur_sf_id,
                           int held_sf_id, unsigned long long stall_us,
                           int delay_ms, pause_state_t *st)
{
	/* Пауза пережила предел — считаем её концом видео. Условие держится
	 * только пока режим удержан, поэтому срабатывает один раз на паузу. */
	if (paused && held && (delay_ms > 0) && (held_sf_id >= 0) &&
	    (stall_us >= (unsigned long long) delay_ms * 1000ULL))
	{
		st->armed = true;
		st->sf_id = held_sf_id;
		return PAUSE_END;
	}

	/* Счётчик снова растёт — возвращаем удержанный режим без нового замера. */
	if (st->armed && !paused && video && (st->sf_id >= 0) && (cur_sf_id != st->sf_id))
		return PAUSE_RESUME;

	return PAUSE_NONE;
}

void pause_forget (pause_state_t *st)
{
	st->armed = false;
	st->sf_id = -1;
}
```

- [ ] **Step 6: Прогнать и проверить мутацией**

Run: `sh tests/run_tests.sh`
Expected: `pause: OK`, остальные наборы зелёные.
Мутации (каждая обязана уронить тест): (а) убрать `(delay_ms > 0)`; (б) убрать `held_sf_id >= 0`; (в) заменить `>=` на `>`; (г) убрать `video` в ветке возобновления.

---

### Task 2: Ключ, профиль и таблица применимости

**Files:**
- Modify: `config/afrd2.ini` (блок «переключение»)
- Modify: `afrd.h` (`DEFAULT_SWITCH_DELAY_OFF_ATV14`), `afrd.c:3297` (чтение ключа)
- Modify: `cfgkeys.c:75` (`CFGKEY_LEGACY` → `CFGKEY_BOTH`), при необходимости — `cfgkeys_note`
- Modify: `tests/test_cfgkeys.c` (проверки для нового смысла)

- [ ] **Step 1: Профиль** — в `config/afrd2.ini` рядом с `switch.restore.on.stop`:

```ini
# Предел удержания режима на паузе (терпение к паузе). 0 — держать режим до
# конца паузы (как было). N>0 — пауза дольше N мс считается концом видео:
# исходный режим возвращается, а на возобновлении удержанный режим
# возвращается сразу, без нового замера (на ATV14 события «видео кончилось»
# нет: остановленное видео с плеером на экране неотличимо от паузы, §32.1).
# Значение по умолчанию — 0; тот же ключ на Android 6-9 значит другое
# (задержка возврата после настоящей остановки).
switch.delay.off=0
```

- [ ] **Step 2: Код** — `afrd.h`: `#define DEFAULT_SWITCH_DELAY_OFF_ATV14 0`; `afrd.c:3297`:

```c
g_switch_delay_off = cfg_get_int ("switch.delay.off",
                                  (g_plat.kind == PLAT_ATV14)
                                  ? DEFAULT_SWITCH_DELAY_OFF_ATV14
                                  : DEFAULT_SWITCH_DELAY_OFF);
```

- [ ] **Step 3: `cfgkeys.c`** — `{ "switch.delay.off", CFGKEY_BOTH, NULL }`.

- [ ] **Step 4: Тесты** — `tests/test_cfgkeys.c`: `switch.delay.off` действует на обоих путях; `tests/test_inactive.c` зелёный только после правки Task 3 (GUI-список).

Run: `sh tests/run_tests.sh`
Expected: `test_cfgkeys` зелёный, `test_inactive` **красный** с текстом «помечена «не действует на Android 10-14», хотя ключ действует: switch_delay_off».

---

### Task 3: Приложение — умолчание и перенос значения

**Files:**
- Modify: `android/app/src/main/java/ru/cobra/zap/afrd/gui/SettingsFragment.java` (убрать `switch_delay_off` из `kInactiveOnAtv14`)
- Modify: `android/app/src/main/java/ru/cobra/zap/afrd/gui/MainActivity.java` (умолчание 0 и разовый перенос)
- Modify: `android/app/src/main/res/values/strings.xml`, `values-ru/strings.xml` (пояснение строки)
- Test: `tests/SeedTest.java` при добавлении правила в `Seed`

- [ ] **Step 1:** убрать ключ из списка «не действует на ATV14»; `sh tests/run_tests.sh` → `test_inactive` зелёный.
- [ ] **Step 2:** на ATV14 привести настройку к 0: если значения нет или оно равно старому умолчанию XML (`10000`) — записать `0` и в префы, и в ini; своё значение пользователя не трогать (`Seed.shouldReset`), перенос одноразовый (флаг в `requisites`). Вызывать в `updateConfig` до `conf.condSave`.
- [ ] **Step 3:** пояснение строки в GUI: «На Android 12–14 — сколько держать режим на паузе (0 — держать до конца паузы)».
- [ ] **Step 4:** `sh tests/run_tests.sh` — зелёный.

---

### Task 4: Встройка в цикл ATV14

**Files:**
- Modify: `afrd.c` (цикл `atv14_run`: `still_since_us`, `pause_state_t`, вызов `pause_step`, `pause_forget` в трёх точках, трассы)

- [ ] **Step 1:** рядом с `prev_frames/still/paused` завести `unsigned long long still_since_us = 0;` и обновлять в ветке роста счётчика.
- [ ] **Step 2:** `pause_state_t pause_mem; pause_forget (&pause_mem);` рядом с прочим состоянием цикла.
- [ ] **Step 3:** перед вычислением `video_eff`:

```c
pause_action_t pact = pause_step (paused, held, video, cur_sf_id, cur_sf_id,
                                  sample_t - still_since_us, g_switch_delay_off,
                                  &pause_mem);
if (pact == PAUSE_END)
{
	trace (0, "пауза дольше %d мс (switch.delay.off) — считаю видео законченным, "
	       "возвращаю режим; на возобновлении вернусь к нему без замера\n",
	       g_switch_delay_off);
}
else if (pact == PAUSE_RESUME)
{
	want_sf_id = pause_mem.sf_id;
	trace (1, "счётчик кадров снова растёт — возвращаю удержанный режим (%d) "
	       "без нового замера\n", pause_mem.sf_id);
}
```
и `bool video_eff = video; if ((!pause_keep && paused) || (pact == PAUSE_END)) video_eff = false;`

- [ ] **Step 4:** `pause_forget (&pause_mem)` — при подтверждённом конце видео (дебаунс `new_video == false`), при старте нового видео и при сбросе счётчика (R34в).
- [ ] **Step 5:** `sh tests/run_tests.sh` + `./mka` (обе ABI) без предупреждений; `afrd -v`/`-s` на host-сборке не падают.

---

### Task 5: Стендовая проверка и приёмка

**Files:**
- Modify: `tools/stand/acceptance_matrix.sh` (новая проверка предела паузы, включается переменной)
- Modify: `README.md` (таблицы применимости ключей, описание `switch.delay.off`, раздел приёмки)

- [ ] **Step 1:** README: `switch.delay.off` переезжает из списка «не действует на новом пути» в «действует» (со сноской о разном смысле), добавляется строка в таблицу ключей ATV14 и пояснение в описание legacy-ключа.
- [ ] **Step 2:** в `acceptance_matrix.sh` — проверка: при `switch.delay.off=N` пауза дольше N возвращает исходный режим и на возобновлении режим возвращается к удержанному (сверить по `/sys/class/display/mode` и `activeMode`).
- [ ] **Step 3:** живой прогон на 192.168.88.50: (а) `switch.delay.off=0` — пауза 25 с держит режим (как §32.5); (б) `=20000` — пауза 25 с возвращает 60 Гц, возобновление возвращает 24 Гц без замерного окна; (в) последовательность «стоп → плеер на экране» (§32.1) лечится пределом.
- [ ] **Step 4:** `sh tests/run_tests.sh`, `./mka`, `assembleRelease`; артефакты в `out/afrd2-0.3.3/` пересобрать, суммы зафиксировать.

---

### Task 6: Фиксация

- [ ] **Step 1:** новый раздел журнала (§32.9) с семантикой, тестами, живым протоколом.
- [ ] **Step 2:** коммит в `master` (`feat(atv14): switch.delay.off — предел удержания режима на паузе`).
- [ ] **Step 3:** сводка владельцу: что изменилось по умолчанию, как включить предел, что осталось непроверенным.
