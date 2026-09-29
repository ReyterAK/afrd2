# AFRd2 (демон) — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Научить форк AFRd переключать частоту обновления HDMI на Android TV 14 / Amlogic S905X5M
для любых приложений, сохранив работоспособность на старых ядрах (Android 6–9).

**Architecture:** Аддитивный форк upstream v0.3.2. Новые «моторчики» (поллинг декодера, измерение fps
по счётчику кадров, приёмник через backdoor SurfaceFlinger 1035) добавляются рядом со старыми
(netlink uevent'ы, `vdec_status`-поля, запись `mode.path`); выбор ветки — по детекту окружения.
Планировщик решений остаётся upstream-овским (таблица режимов, рейтинг, `frac_rate_policy`).

**Tech Stack:** C (bionic, armeabi-v7a), NDK (`ndk-build`), host-сборка gcc для юнит-тестов,
ADB-скрипты стенда, Android 14 (`service call SurfaceFlinger 1035`).

**Spec:** `docs/superpowers/specs/2026-09-29-afrd2-design.md` (журнал анализа:
`/home/reyter/AI/afrd2-journal.md`).

## Global Constraints

- Форк upstream, аддитивные изменения: поведение на ядрах 3.14/4.9 не ломать (старые ветки кода сохраняются).
- **Никогда** не открывать `/sys/class/vdec/dump_vdec_chunks`, `dump_vdec_blocks`, `dump_path_monitor`
  (причина kernel panic, журнал §4).
- Поля `frame rate` / `frame dur` из `vdec_status` **не использовать** как источник fps.
- Целевая сборка: `APP_ABI=armeabi-v7a`, `APP_PLATFORM=android-21`, запуск под root.
- Стенд: `192.168.88.50:5555`; тестовые клипы уже в `/sdcard/Download/afr/` (23.976/24/25/50/60 fps).
- Изменения в ОС бокса — только после согласования с владельцем (правило проекта).
- Текущий формат режимов: `display_mode_t { name, width, height, framerate, interlaced, fractional }`;
  частота — fixed-point 24.8 (`HZ_ARGS`).
- Стиль кода upstream: табы, `trace()` для логов, `snake_case`, заголовки в корне репозитория.

## Review Focus

Входы/ситуации, которые спека не описывает явно, но которые сломают пользователя (тест на каждую — в задаче-владельце):

1. **Пауза/буферизация** — счётчик кадров стоит: демон не должен «измерить» 0 fps и уйти в переключение/возврат. (Task 4)
2. **Очень короткий клип** (старт+стоп быстрее окна замера) — не должно быть переключения туда-обратно (дребезг). (Task 4/5)
3. **23.976 против 24** и **29.97 против 30** — разница 0.1 %, счётчик кадров её не различает: поведение должно быть детерминированным (по `frac_rate_policy`/настройке), без «качелей». (Task 4)
4. **Backdoor/`service` недоступны** (другая прошивка, будущий Android) — демон обязан деградировать безопасно и явно сообщать, не ломая систему. (Task 3/5)
5. **Два видео подряд с разными частотами / смена серии** — без лишних переключений и без «залипшего» флага backdoor. (Task 5/6)

---

## Структура файлов

Создаются:

| Файл | Ответственность |
|---|---|
| `fps_class.h/.c` | чистая логика: оценка fps по двум счётчикам, классификация в стандартный ряд |
| `sfmodes.h/.c` | разбор `dumpsys SurfaceFlinger` → таблица `{id, w, h, hz}` (+ поиск режима) |
| `sink.h/.c` | приёмник: построение команд backdoor, применение/сброс, состояние удержания |
| `guard.h/.c` | чистая логика решений: когда переключать/возвращать/снимать флаг |
| `vdec.h/.c` | чтение и разбор `/sys/class/vdec/vdec_status` |
| `platform.h/.c` | детект окружения (legacy vs ATV14), проба backdoor, `now_us()` |
| `exec.h/.c` | запуск команды и захват вывода (`service call …`, `dumpsys …`) |
| `tests/run_tests.sh` | host-раннер юнит-тестов (gcc, без Android) |
| `tests/test_fps_class.c`, `tests/test_sfmodes.c`, `tests/test_sink.c`, `tests/test_guard.c`, `tests/test_vdec.c` | юнит-тесты |
| `tests/fixtures/sf_modes.txt`, `tests/fixtures/vdec_status_playing.txt`, `tests/fixtures/vdec_status_idle.txt` | реальные образцы вывода с бокса |
| `config/afrd2.ini` | профиль конфига для Android 10–14 |
| `tools/stand/*.sh` | сценарии стенда (запуск клипа, проверка режима, матрица приёмки) |

Изменяются:

| Файл | Что |
|---|---|
| `afrd.c` | новый путь исполнения `run_atv14()`; `afrd_run()` выбирает путь по `platform_detect()`; обработка SIGTERM/SIGINT; стартовая очистка «залипшего» флага |
| `afrd.h` | объявления новых глобалов/структур состояния ATV14 |
| `GNUmakefile`, `jni/Android.mk` | новые `.c` в сборке |
| `jni/Application.mk`, `mka` | NDK-путь/ABI под текущий стенд |
| `README.md`, `ChangeLog-ru.txt` | описание порта |

Не трогаем: `cfg_parse/`, `colorspace.c`, `apisock.c`, `shmem.c`, `modes.c` (кроме точки входа),
`hdcp.c` (правится в Task 2), `uevent_filter.c` (остаётся для legacy-ветки).

---

### Task 1: Host-тесты + модуль классификации fps

**Files:**
- Create: `fps_class.h`, `fps_class.c`, `tests/run_tests.sh`, `tests/test_fps_class.c`
- Modify: `GNUmakefile` (цели `test`/`tests`), `.gitignore` (артефакты сборки)

**Interfaces:**
- Consumes: ничего.
- Produces:
  - `double fps_estimate(unsigned long long frames_a, unsigned long long ts_a_us, unsigned long long frames_b, unsigned long long ts_b_us);`
  - `int fps_classify(double measured);` → fixed-point 24.8 Гц или `0`
  - `int fps_rate_hz(int index); const char *fps_rate_name(int index); int fps_rate_index_for_hz(int hz);`
  - `#define FPS_RATE_COUNT 8`

- [ ] **Step 1: Написать падающий тест**

`tests/test_fps_class.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../fps_class.h"

static void t_estimate(void)
{
    /* 100 кадров за 4.000 c = 25 fps */
    assert(fps_estimate(1000, 0, 1100, 4000000) > 24.99 && fps_estimate(1000, 0, 1100, 4000000) < 25.01);
    /* 240 кадров за 10.010 c = 23.976 fps */
    double f = fps_estimate(0, 0, 240, 10010000);
    assert(f > 23.97 && f < 23.98);
    /* вырожденные случаи -> 0 */
    assert(fps_estimate(500, 0, 500, 2000000) == 0.0);   /* видео на паузе */
    assert(fps_estimate(500, 0, 501, 2000000) == 0.0);   /* 0.5 fps — ниже FPS_MIN */
    assert(fps_estimate(0, 0, 100000, 1000000) == 0.0);  /* мусор (100000 fps) */
}

static void t_classify(void)
{
    assert(fps_classify(25.0) == 25 * 256);
    assert(fps_classify(24.0) == 24 * 256);
    assert(fps_classify(23.976) == fps_rate_hz(0));
    assert(fps_classify(29.97) == fps_rate_hz(3));
    assert(fps_classify(59.94) == fps_rate_hz(6));
    assert(fps_classify(35.0) == 0);   /* отн. ошибка >6% и к 30, и к 25 */
    assert(fps_classify(0.0) == 0);
    assert(fps_classify(24.05) == 24 * 256); /* чуть выше 24 -> 24 */
    assert(fps_classify(23.5) == fps_rate_hz(0)); /* отн. ошибка меньше к 23.976, чем к 24 */
}

static void t_table(void)
{
    assert(strcmp(fps_rate_name(0), "23.976") == 0);
    assert(fps_rate_hz(0) == 6138);
    assert(fps_rate_hz(7) == 60 * 256);
    assert(fps_rate_index_for_hz(25 * 256) == 2);
}

int main(void)
{
    t_estimate();
    t_classify();
    t_table();
    printf("fps_class: OK\n");
    return 0;
}
```

- [ ] **Step 2: Убедиться, что тест падает**

Run: `cc -o /tmp/t_fps tests/test_fps_class.c fps_class.c`
Expected: ошибка компиляции (`fps_class.h: No such file`).

- [ ] **Step 3: Реализовать модуль**

`fps_class.h`:

```c
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
```

`fps_class.c`:

```c
#include <math.h>
#include <stdio.h>
#include "fps_class.h"

static const struct { const char *name; double fps; } k_rates [FPS_RATE_COUNT] =
{
	{ "23.976", 24000.0 / 1001.0 },
	{ "24",     24.0 },
	{ "25",     25.0 },
	{ "29.97",  30000.0 / 1001.0 },
	{ "30",     30.0 },
	{ "50",     50.0 },
	{ "59.94",  60000.0 / 1001.0 },
	{ "60",     60.0 },
};

/* Допуск 6% от частоты: окна соседних стандартных частот перекрываются, */
/* поэтому выбирается ближайшая по относительной ошибке (fps_classify).   */
/* 35 fps не классифицируется: отн. ошибка >6% и к 30 (16.7%), и к 25.    */
#define FPS_TOLERANCE	0.06
#define FPS_MIN		15.0
#define FPS_MAX		130.0

int fps_rate_hz (int index)
{
	return (int)(k_rates [index].fps * 256.0 + 0.5);
}

const char *fps_rate_name (int index)
{
	return k_rates [index].name;
}

double fps_estimate (unsigned long long frames_a, unsigned long long ts_a_us,
                     unsigned long long frames_b, unsigned long long ts_b_us)
{
	if (frames_b <= frames_a || ts_b_us <= ts_a_us)
		return 0.0;

	double dt = (double)(ts_b_us - ts_a_us) / 1000000.0;
	if (dt < 0.5)
		return 0.0;

	double fps = (double)(frames_b - frames_a) / dt;
	if (fps < FPS_MIN || fps > FPS_MAX)
		return 0.0;

	return fps;
}

int fps_classify (double measured)
{
	if (measured <= 0.0)
		return 0;

	int best = -1;
	double best_err = 1e9;
	for (int i = 0; i < FPS_RATE_COUNT; i++)
	{
		double err = fabs (measured - k_rates [i].fps) / k_rates [i].fps;
		if (err < best_err)
		{
			best_err = err;
			best = i;
		}
	}

	if ((best < 0) || (best_err > FPS_TOLERANCE))
		return 0;

	return fps_rate_hz (best);
}

int fps_rate_index_for_hz (int hz)
{
	int best = -1, best_d = 1 << 30;

	for (int i = 0; i < FPS_RATE_COUNT; i++)
	{
		int h = fps_rate_hz (i);
		int d = (hz > h) ? (hz - h) : (h - hz);
		if (d < best_d)
		{
			best_d = d;
			best = i;
		}
	}

	return best;
}
```

`tests/run_tests.sh`:

```sh
#!/bin/sh
# Host-юнит-тесты (сборка gcc, без Android)
set -e
cd "$(dirname "$0")/.."
mkdir -p out/host-tests
CC=${CC:-cc}
status=0
for t in tests/test_*.c; do
	name=$(basename "$t" .c)
	deps=$(sed -n 's/^#include "\.\.\/\(.*\.c\)".*/\1/p' "$t")
	"$CC" -Wall -o "out/host-tests/$name" "$t" $deps || { status=1; continue; }
	"./out/host-tests/$name" || status=1
done
exit $status
```

`.gitignore` (новый или дополнение):

```
out/
libs/
obj/
*.o
```

- [ ] **Step 4: Запустить тесты**

Run: `sh tests/run_tests.sh`
Expected: `fps_class: OK`, код возврата 0.

- [ ] **Step 5: Коммит**

```bash
git add fps_class.h fps_class.c tests/ .gitignore GNUmakefile
git commit -m "feat(tests): host-раннер тестов и модуль классификации fps"
```

---

### Task 2: Разбор списка режимов SurfaceFlinger (`sfmodes`)

**Files:**
- Create: `sfmodes.h`, `sfmodes.c`, `tests/test_sfmodes.c`, `tests/fixtures/sf_modes.txt`
- Modify: `GNUmakefile` (юнит-таргет уже общий), `afrd.h` (`sf_mode_t` не нужен — тип в `sfmodes.h`)

**Interfaces:**
- Consumes: ничего.
- Produces:
  - `typedef struct { int id; int width; int height; int hz; } sf_mode_t;` (`hz` — 24.8)
  - `#define SF_MODE_MAX 64`
  - `int sfmodes_parse (const char *dump, sf_mode_t *out, int max);`
  - `int sfmodes_find (const sf_mode_t *modes, int n, int hz, int width, int height);` → id или `-1`
  - `int sfmodes_find_hz (const sf_mode_t *modes, int n, int hz);` → id по одной частоте (допуск 0.125 Гц) или `-1`
  - `int sfmodes_current_hz (const sf_mode_t *modes, int n, int id);`

- [ ] **Step 1: Сохранить фикстуру (реальный вывод с бокса)**

`tests/fixtures/sf_modes.txt`:

```
        {id=0, hwcId=0, resolution=1920x1080, refreshRate=59.94 Hz, dpi=47.81x48.13, group=0}
        {id=1, hwcId=1, resolution=1920x1080, refreshRate=60.00 Hz, dpi=47.81x48.13, group=1}
        {id=2, hwcId=2, resolution=1920x1080, refreshRate=50.00 Hz, dpi=47.81x48.13, group=4}
        {id=3, hwcId=3, resolution=1920x1080, refreshRate=29.97 Hz, dpi=47.81x48.13, group=6}
        {id=4, hwcId=4, resolution=1920x1080, refreshRate=30.00 Hz, dpi=47.81x48.13, group=7}
        {id=5, hwcId=5, resolution=1920x1080, refreshRate=25.00 Hz, dpi=47.81x48.13, group=8}
        {id=6, hwcId=6, resolution=1920x1080, refreshRate=23.98 Hz, dpi=47.81x48.13, group=9}
        {id=7, hwcId=7, resolution=1920x1080, refreshRate=24.00 Hz, dpi=47.81x48.13, group=10}
        {id=8, hwcId=8, resolution=1280x720, refreshRate=59.94 Hz, dpi=31.87x32.08, group=11}
```

- [ ] **Step 2: Написать падающий тест**

`tests/test_sfmodes.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sfmodes.h"

static char *read_file (const char *path)
{
	FILE *f = fopen (path, "rb");
	assert (f);
	static char buf [65536];
	size_t n = fread (buf, 1, sizeof (buf) - 1, f);
	buf [n] = 0;
	fclose (f);
	return buf;
}

int main (void)
{
	char *dump = read_file ("tests/fixtures/sf_modes.txt");
	sf_mode_t modes [SF_MODE_MAX];
	int n = sfmodes_parse (dump, modes, SF_MODE_MAX);

	assert (n == 9);
	assert (modes [0].id == 0 && modes [0].hz == 59 * 256 + 241); /* 59.94 -> 15345 */
	assert (modes [6].id == 6 && modes [6].hz == 6139);           /* 23.98 Hz -> 23.98*256+0.5 */
	assert (modes [6].width == 1920 && modes [6].height == 1080);

	/* поиск: 23.976 в 1080p -> id 6 */
	assert (sfmodes_find (modes, n, 6138, 1920, 1080) == 6);
	/* 60 Гц в 1080p -> id 1 */
	assert (sfmodes_find (modes, n, 60 * 256, 1920, 1080) == 1);
	/* 24.0 Гц в 1080p -> id 7 (не 6: точное совпадение ближе) */
	assert (sfmodes_find (modes, n, 24 * 256, 1920, 1080) == 7);
	/* 50 Гц в 720p -> нет */
	assert (sfmodes_find (modes, n, 50 * 256, 1280, 720) == -1);
	/* текущая частота по id */
	assert (sfmodes_current_hz (modes, n, 2) == 50 * 256);
	/* поиск по одной частоте (для определения активного режима) */
	assert (sfmodes_find_hz (modes, n, 6138) == 6);
	assert (sfmodes_find_hz (modes, n, 50 * 256) == 2);
	assert (sfmodes_find_hz (modes, n, 100 * 256) == -1);	/* 100 Гц нет, допуск 0.125 Гц -> -1 */

	printf ("sfmodes: OK\n");
	return 0;
}
```

- [ ] **Step 3: Убедиться, что тест падает**

Run: `sh tests/run_tests.sh`
Expected: ошибка компиляции `sfmodes.h: No such file`.

- [ ] **Step 4: Реализовать парсер**

`sfmodes.h`:

```c
#ifndef SFMODES_H
#define SFMODES_H

#define SF_MODE_MAX	64
/* Допуск сравнения частот (0.125 Гц) живёт в sfmodes.c как SF_HZ_TOL */

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

/* Поиск id режима по одной частоте (без учёта разрешения). -1, если ничего не ближе SF_HZ_TOL. */
int sfmodes_find_hz (const sf_mode_t *modes, int n, int hz);

/* Частота (24.8) по id режима. 0, если id не найден. */
int sfmodes_current_hz (const sf_mode_t *modes, int n, int id);

#endif
```

`sfmodes.c`:

```c
#include <stdio.h>
#include <string.h>
#include "sfmodes.h"

static int parse_line (const char *line, sf_mode_t *out)
{
	/* формат: "{id=6, hwcId=6, resolution=1920x1080, refreshRate=23.98 Hz, ...}" */
	int id, w, h;
	float hz;
	if (sscanf (line, "{id=%d, hwcId=%*d, resolution=%dx%d, refreshRate=%f",
	            &id, &w, &h, &hz) != 4)
		return 0;

	out->id = id;
	out->width = w;
	out->height = h;
	out->hz = (int)(hz * 256.0 + 0.5);
	return 1;
}

int sfmodes_parse (const char *dump, sf_mode_t *out, int max)
{
	int n = 0;
	const char *cur = dump;

	while (cur && *cur && n < max)
	{
		const char *eol = strchr (cur, '\n');
		const char *brace = strchr (cur, '{');
		if (!eol)
			eol = cur + strlen (cur);

		if (brace && brace < eol && parse_line (brace, &out [n]))
			n++;

		cur = (*eol) ? eol + 1 : eol;
	}

	return n;
}

int sfmodes_find (const sf_mode_t *modes, int n, int hz, int width, int height)
{
	int best = -1, best_d = 1 << 30;

	for (int i = 0; i < n; i++)
	{
		if (modes [i].width != width || modes [i].height != height)
			continue;

		int d = modes [i].hz > hz ? modes [i].hz - hz : hz - modes [i].hz;
		if (d < best_d)
		{
			best_d = d;
			best = modes [i].id;
		}
	}

	/* допуск SF_HZ_TOL: иначе для 50 Гц в 720p вернём далёкий 59.94-режим */
	return (best_d <= SF_HZ_TOL) ? best : -1;
}

int sfmodes_current_hz (const sf_mode_t *modes, int n, int id)
{
	for (int i = 0; i < n; i++)
		if (modes [i].id == id)
			return modes [i].hz;

	return 0;
}

int sfmodes_find_hz (const sf_mode_t *modes, int n, int hz)
{
	int best = -1, best_d = 1 << 30;

	for (int i = 0; i < n; i++)
	{
		int d = modes [i].hz > hz ? modes [i].hz - hz : hz - modes [i].hz;
		if (d < best_d)
		{
			best_d = d;
			best = modes [i].id;
		}
	}

	/* допуск SF_HZ_TOL (0.125 Гц): частота активного режима должна совпасть
	 * практически точно, иначе это не наш режим (100 Гц в списке нет) */
	return (best_d <= SF_HZ_TOL) ? best : -1;
}
```

- [ ] **Step 5: Запустить тесты**

Run: `sh tests/run_tests.sh`
Expected: `sfmodes: OK`.

- [ ] **Step 6: Коммит**

```bash
git add sfmodes.h sfmodes.c tests/
git commit -m "feat: разбор таблицы режимов SurfaceFlinger (id<->частота)"
```

---

### Task 3: Приёмник (`sink`) и логика удержания (`guard`)

**Files:**
- Create: `sink.h`, `sink.c`, `guard.h`, `guard.c`, `tests/test_sink.c`, `tests/test_guard.c`

**Interfaces:**
- Consumes: `sf_mode_t` (Task 2), `sysfs_write()`/`g_mode_path` (upstream, `sysfs.c`/`afrd.h`).
- Produces:
  - `int sink_build_sf_set (char *buf, size_t n, int sf_mode_id);` → `"service call SurfaceFlinger 1035 i32 <id>"`
  - `int sink_build_sf_reset (char *buf, size_t n);` → `"service call SurfaceFlinger 1035 i32 -1"`
  - `typedef enum { SINK_LEGACY = 0, SINK_SF_BACKDOOR = 1 } sink_kind_t;`
  - `int sink_apply (sink_kind_t kind, const char *mode_path, int sf_mode_id, const char *legacy_mode_name);`
  - `typedef enum { GUARD_NONE = 0, GUARD_SWITCH, GUARD_RESTORE } guard_action_t;`
  - `guard_action_t guard_decide (bool video_active, int want_sf_id, int cur_sf_id, int *orig_sf_id, bool *held);`
  - `void guard_note_restored (int *held);`

- [ ] **Step 1: Написать падающие тесты**

`tests/test_sink.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../sink.h"

int main (void)
{
	char buf [128];

	assert (sink_build_sf_set (buf, sizeof (buf), 6) == 0);
	assert (strcmp (buf, "service call SurfaceFlinger 1035 i32 6") == 0);

	assert (sink_build_sf_set (buf, sizeof (buf), 0) == 0);
	assert (strcmp (buf, "service call SurfaceFlinger 1035 i32 0") == 0);

	assert (sink_build_sf_reset (buf, sizeof (buf)) == 0);
	assert (strcmp (buf, "service call SurfaceFlinger 1035 i32 -1") == 0);

	/* слишком маленький буфер -> ошибка, без переполнения */
	assert (sink_build_sf_set (buf, 10, 6) != 0);

	printf ("sink: OK\n");
	return 0;
}
```

`tests/test_guard.c`:

```c
#include <assert.h>
#include <stdio.h>
#include "../guard.h"

int main (void)
{
	int orig = -1;
	bool held = false;

	/* режим уже подходит -> ничего не делаем (важно для приложений со своим AFR) */
	assert (guard_decide (true, 6, 6, &orig, &held) == GUARD_NONE);
	assert (!held);

	/* режим не подходит -> переключаем и запоминаем исходный */
	assert (guard_decide (true, 6, 0, &orig, &held) == GUARD_SWITCH);
	assert (orig == 0 && held);

	/* повторный вызов с той же целью -> ничего */
	assert (guard_decide (true, 6, 6, &orig, &held) == GUARD_NONE);

	/* видео кончилось -> возвращаем */
	assert (guard_decide (false, 0, 6, &orig, &held) == GUARD_RESTORE);
	assert (orig == 0);

	/* после возврата флаг снят */
	guard_note_restored (&held);
	assert (!held);

	/* стоп без удержания -> ничего */
	assert (guard_decide (false, 0, 0, &orig, &held) == GUARD_NONE);

	printf ("guard: OK\n");
	return 0;
}
```

- [ ] **Step 2: Убедиться, что тесты падают**

Run: `sh tests/run_tests.sh`
Expected: ошибка компиляции `sink.h: No such file`.

- [ ] **Step 3: Реализовать `sink`**

`sink.h`:

```c
#ifndef SINK_H
#define SINK_H

#include <stddef.h>

typedef enum
{
	SINK_LEGACY = 0,	/* запись имени режима в /sys/class/display/mode (Android 6-9) */
	SINK_SF_BACKDOOR = 1,	/* service call SurfaceFlinger 1035 i32 <id> (Android 10-14) */
} sink_kind_t;

/* Строки команд (для тестов и для логов). Возврат: 0 = ок, -1 = не влезло. */
int sink_build_sf_set (char *buf, size_t n, int sf_mode_id);
int sink_build_sf_reset (char *buf, size_t n);

/* Применить режим. Возврат: 0 = ок, -1 = ошибка. */
int sink_apply (sink_kind_t kind, const char *mode_path, int sf_mode_id,
                const char *legacy_mode_name);

/* Снять флаг backdoor (безопасно и для legacy: no-op). Возврат: 0/-1. */
int sink_release_backdoor (sink_kind_t kind);

#endif
```

`sink.c`:

```c
#include <stdio.h>
#include "afrd.h"
#include "exec.h"
#include "sink.h"

int sink_build_sf_set (char *buf, size_t n, int sf_mode_id)
{
	int r = snprintf (buf, n, "service call SurfaceFlinger 1035 i32 %d", sf_mode_id);
	return (r < 0 || (size_t)r >= n) ? -1 : 0;
}

int sink_build_sf_reset (char *buf, size_t n)
{
	int r = snprintf (buf, n, "service call SurfaceFlinger 1035 i32 -1");
	return (r < 0 || (size_t)r >= n) ? -1 : 0;
}

int sink_release_backdoor (sink_kind_t kind)
{
	if (kind != SINK_SF_BACKDOOR)
		return 0;

	char cmd [128];
	if (sink_build_sf_reset (cmd, sizeof (cmd)) != 0)
		return -1;

	/* Ожидаемый ответ — ошибка EINVAL: это признак того, что backdoor есть,
	 * а флаг "режим задан снаружи" снят. Успех команды нам не нужен. */
	char out [256];
	if (exec_capture (cmd, out, sizeof (out)) != 0)
		trace (1, "backdoor reset: команда не выполнилась\n");
	else
		trace (2, "backdoor reset: %s\n", out);

	return 0;
}

int sink_apply (sink_kind_t kind, const char *mode_path, int sf_mode_id,
                const char *legacy_mode_name)
{
	if (kind == SINK_LEGACY)
	{
		if (!mode_path || !legacy_mode_name)
			return -1;

		trace (1, "Переключаю режим (legacy) на %s\n", legacy_mode_name);
		return sysfs_write (mode_path, legacy_mode_name);
	}

	char cmd [128], out [256];
	if (sink_build_sf_set (cmd, sizeof (cmd), sf_mode_id) != 0)
		return -1;

	trace (1, "Переключаю режим (backdoor) на mode id %d\n", sf_mode_id);
	if (exec_capture (cmd, out, sizeof (out)) != 0)
		return -1;

	/* Успех: "Result: Parcel(	00000000    '....')" (пустой ответ, код 0),
	 * либо NULL (Parcel(NULL) — тоже успех в этом API). */
	if (strstr (out, "Error:"))
	{
		trace (1, "backdoor вернул ошибку: %s\n", out);
		return -1;
	}

	return 0;
}
```

`guard.h`:

```c
#ifndef GUARD_H
#define GUARD_H

#include <stdbool.h>

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

/* Вызывается после успешного возврата исходного режима. */
void guard_note_restored (bool *held);

#endif
```

`guard.c`:

```c
#include "guard.h"

guard_action_t guard_decide (bool video_active, int want_sf_id, int cur_sf_id,
                             int *orig_sf_id, bool *held)
{
	if (!video_active)
	{
		if (*held)
		{
			/* вернуть исходный режим, только если мы его меняли */
			return (*orig_sf_id >= 0) ? GUARD_RESTORE : GUARD_NONE;
		}
		return GUARD_NONE;
	}

	if (want_sf_id < 0 || want_sf_id == cur_sf_id)
		return GUARD_NONE;

	if (!*held)
	{
		*orig_sf_id = cur_sf_id;	/* запомнить один раз */
		*held = true;
	}

	return GUARD_SWITCH;
}

void guard_note_restored (bool *held)
{
	*held = false;
}
```

- [ ] **Step 4: Запустить тесты**

Run: `sh tests/run_tests.sh`
Expected: `sink: OK`, `guard: OK`.
(Тест `sink` не линкует `sink.c` — только builder-функции; поэтому `test_sink.c` собирается с `sink.c`,
а `sink.c` в свою очередь тянет `exec.c`/`afrd.h`. Для host-теста добавляем в `tests/run_tests.sh`
линковку `exec.c` и заглушку: см. Step 5.)

- [ ] **Step 5: Разделить host- и target-зависимости в `sink.c`**

Чтобы `sink.c` собирался и на хосте, и на устройстве, вынести «грязную» часть в `exec.c`:

`exec.h`:

```c
#ifndef EXEC_H
#define EXEC_H

#include <stddef.h>

/* Выполнить команду через /system/bin/sh и захватить stdout+stderr.
 * Возврат: 0 = команда запущена, -1 = не удалось запустить. */
int exec_capture (const char *cmd, char *out, size_t out_sz);

#endif
```

`exec.c`:

```c
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "exec.h"

int exec_capture (const char *cmd, char *out, size_t out_sz)
{
	if (out_sz)
		out [0] = 0;

	FILE *p = popen (cmd, "r");
	if (!p)
		return -1;

	size_t used = 0;
	while (used + 1 < out_sz)
	{
		size_t n = fread (out + used, 1, out_sz - 1 - used, p);
		if (!n)
			break;
		used += n;
	}
	out [used] = 0;

	int rc = pclose (p);
	return (rc == -1) ? -1 : 0;
}
```

и в `tests/run_tests.sh` добавить в список зависимостей `exec.c` для `test_sink.c` (см. `deps`-эвристику:
в тесте дописать `#include "../exec.c"` **нельзя** — вместо этого расширить скрипт: собирать
`tests/test_sink.c` вместе с `sink.c exec.c` через явную таблицу в `run_tests.sh`).

`tests/run_tests.sh` (полная версия после правки):

```sh
#!/bin/sh
set -e
cd "$(dirname "$0")/.."
mkdir -p out/host-tests
CC=${CC:-cc}
status=0
run() { # run <test.c> <extra-sources...>
	name=$(basename "$1" .c)
	shift
	"$CC" -Wall -Wno-unused-function -o "out/host-tests/$name" "tests/$name.c" "$@" || { status=1; return; }
	"./out/host-tests/$name" || status=1
}
run test_fps_class fps_class.c
run test_vdec vdec.c
run test_sfmodes sfmodes.c
run test_guard guard.c
run test_sink sink.c exec.c
exit $status
```

- [ ] **Step 6: Прогнать тесты и закоммитить**

Run: `sh tests/run_tests.sh`
Expected: 5 `OK`-строк, код 0.

```bash
git add sink.h sink.c guard.h guard.c exec.h exec.c tests/
git commit -m "feat: приёмник режима (backdoor 1035) и логика удержания/возврата"
```

---

### Task 4: Чтение состояния декодера (`vdec`)

**Files:**
- Create: `vdec.h`, `vdec.c`, `tests/test_vdec.c`, `tests/fixtures/vdec_status_playing.txt`, `tests/fixtures/vdec_status_idle.txt`

**Interfaces:**
- Consumes: `sysfs_get_str()` (upstream `sysfs.c`).
- Produces:
  - `typedef struct { bool active; char device[40]; unsigned long long frames; } vdec_status_t;`
  - `int vdec_parse (const char *text, vdec_status_t *out);` → 0 = ок
  - `int vdec_read (const char *path, vdec_status_t *out);`
  - `unsigned long long now_us (void);` (реализация в `vdec.c`)

- [ ] **Step 1: Фикстуры (реальный вывод с бокса)**

`tests/fixtures/vdec_status_idle.txt`:

```
No vdec.
```

`tests/fixtures/vdec_status_playing.txt`:

```
vdec channel 0 statistics:
  device name : ammvdec_h264_v4
  frame width : 1920
 frame height : 1080
   frame rate : 24 fps
     bit rate : 3640 kbps
       status : 6
    frame dur : 4004
   frame data : 0 KB
  frame count : 13516
   drop count : 0
fra err count : 0
 hw err count : 0
   total data : 0 KB
ratio_control : 9000
```

- [ ] **Step 2: Написать падающий тест**

`tests/test_vdec.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../vdec.h"

static char *slurp (const char *p)
{
	static char b [8192];
	FILE *f = fopen (p, "rb");
	assert (f);
	size_t n = fread (b, 1, sizeof (b) - 1, f);
	b [n] = 0;
	fclose (f);
	return b;
}

int main (void)
{
	vdec_status_t st;

	assert (vdec_parse (slurp ("tests/fixtures/vdec_status_idle.txt"), &st) == 0);
	assert (!st.active);

	assert (vdec_parse (slurp ("tests/fixtures/vdec_status_playing.txt"), &st) == 0);
	assert (st.active);
	assert (strcmp (st.device, "ammvdec_h264_v4") == 0);
	assert (st.frames == 13516);

	/* мусор не должен "включать" видео */
	assert (vdec_parse ("vdec channel 0 statistics:\n  frame count : 5\n", &st) == 0);
	assert (!st.active);

	printf ("vdec: OK\n");
	return 0;
}
```

- [ ] **Step 3: Убедиться, что тест падает**

Run: `sh tests/run_tests.sh`
Expected: `vdec.h: No such file or directory`.

- [ ] **Step 4: Реализовать**

`vdec.h`:

```c
#ifndef VDEC_H
#define VDEC_H

#include <stdbool.h>

typedef struct
{
	bool active;			/* есть ли активный декодер */
	char device [40];		/* device name, напр. ammvdec_h264_v4 */
	unsigned long long frames;	/* frame count (для замера fps по дельте) */
} vdec_status_t;

int vdec_parse (const char *text, vdec_status_t *out);
int vdec_read (const char *path, vdec_status_t *out);

/* CLOCK_MONOTONIC в микросекундах. */
unsigned long long now_us (void);

#endif
```

`vdec.c`:

```c
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "vdec.h"
#include "sysfs.h"

unsigned long long now_us (void)
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}

int vdec_parse (const char *text, vdec_status_t *out)
{
	memset (out, 0, sizeof (*out));

	if (!text || strstr (text, "No vdec."))
		return 0;

	const char *p = strstr (text, "device name");
	if (!p)
		return 0;			/* без имени устройства видео не считаем */

	p = strchr (p, ':');
	if (!p)
		return 0;
	p++;
	while (*p == ' ' || *p == '\t')
		p++;

	size_t i = 0;
	while (i + 1 < sizeof (out->device) && p [i] && p [i] != '\n' && p [i] != '\r')
	{
		out->device [i] = p [i];
		i++;
	}
	out->device [i] = 0;

	p = strstr (text, "frame count");
	if (!p)
		return 0;
	p = strchr (p, ':');
	if (!p)
		return 0;

	if (sscanf (p + 1, "%llu", &out->frames) != 1)
		return 0;

	out->active = true;
	return 0;
}

int vdec_read (const char *path, vdec_status_t *out)
{
	char *text = sysfs_read (path);
	if (!text)
	{
		memset (out, 0, sizeof (*out));
		return -1;
	}

	vdec_parse (text, out);
	free (text);
	return 0;
}
```

- [ ] **Step 5: Запустить тесты**

Run: `sh tests/run_tests.sh`
Expected: `vdec: OK`.

- [ ] **Step 6: Коммит**

```bash
git add vdec.h vdec.c tests/
git commit -m "feat: чтение состояния декодера (vdec_status) без опасных узлов"
```

---

### Task 5: Детект окружения (`platform`) и профиль конфига ATV14

**Files:**
- Create: `platform.h`, `platform.c`, `config/afrd2.ini`
- Modify: `afrd.h`, `GNUmakefile` (`AFRD_SRC` += новые модули, `-lm`)

**Interfaces:**
- Consumes: `exec_capture()` (Task 3), `sink_build_sf_reset()` (Task 3), `sfmodes*` (Task 2).
- Produces:
  - `typedef enum { PLAT_LEGACY = 0, PLAT_ATV14 = 1 } platform_kind_t;`
  - `typedef struct { platform_kind_t kind; sink_kind_t sink; const char *vdec_status; const char *hdmi_state; const char *sf_dump_cmd; const char *display_dump_cmd; bool sf_backdoor; } platform_t;`
  - `platform_t platform_detect (void);`

- [ ] **Step 1: Написать модуль детекта**

`platform.h`:

```c
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include "sink.h"

typedef enum
{
	PLAT_LEGACY = 0,	/* ядра 3.14/4.9, Android 6-9 */
	PLAT_ATV14 = 1,		/* ядра 5.x, Android 10-14: поллинг + backdoor SF */
} platform_kind_t;

typedef struct
{
	platform_kind_t kind;
	sink_kind_t sink;
	const char *vdec_status;	/* /sys/class/vdec/vdec_status */
	const char *hdmi_state;		/* признак подключённого HDMI */
	const char *sf_dump_cmd;	/* команда получения таблицы режимов */
	const char *display_dump_cmd;	/* команда активного разрешения (dumpsys display) */
	bool sf_backdoor;		/* доступна ли транзакция 1035 */
} platform_t;

platform_t platform_detect (void);

#endif
```

`platform.c`:

```c
#include <unistd.h>
#include <string.h>
#include "afrd.h"
#include "exec.h"
#include "platform.h"
#include "sink.h"

#define VDVC_STATUS	"/sys/class/vdec/vdec_status"
#define HDMI_STATE_NEW	"/sys/class/extcon/extcon0/state"
#define HDMI_STATE_OLD	"/sys/class/switch/hdmi/state"
#define HDMI_STATE_OLD_CABLE	"/sys/class/switch/hdmi/cable.0/state"

static bool sf_backdoor_available (void)
{
	/* Проба: вызов с заведомо неверным id. Если backdoor есть — получим
	 * BAD_VALUE ("Invalid argument"), и флаг "режим задан снаружи" снимется.
	 * На старых Android этот код вернёт PERMISSION_DENIED/"unknown". */
	char cmd [128], out [256];
	sink_build_sf_reset (cmd, sizeof (cmd));
	if (exec_capture (cmd, out, sizeof (out)) != 0)
		return false;

	return strstr (out, "Invalid argument") != NULL;
}

platform_t platform_detect (void)
{
	platform_t p;
	memset (&p, 0, sizeof (p));

	p.vdec_status = VDVC_STATUS;
	p.sf_dump_cmd = "dumpsys SurfaceFlinger";
	p.display_dump_cmd = "dumpsys display";

	bool have_vdec = (sysfs_exists (VDVC_STATUS) == 0);
	/* Android 6/7: switch/hdmi/state; Android 8/9: switch/hdmi/cable.0/state.
	 * Оба пути обязаны увести на legacy-ветку: иначе живым A8/A9-боксам
	 * выключится их штатный AFR (R18). */
	bool have_old_hdmi = (sysfs_exists (HDMI_STATE_OLD) == 0) ||
	                     (sysfs_exists (HDMI_STATE_OLD_CABLE) == 0);

	if (have_vdec && !have_old_hdmi)
	{
		p.kind = PLAT_ATV14;
		p.sink = SINK_SF_BACKDOOR;
		p.hdmi_state = HDMI_STATE_NEW;
		p.sf_backdoor = sf_backdoor_available ();
	}
	else
	{
		p.kind = PLAT_LEGACY;
		p.sink = SINK_LEGACY;
		p.hdmi_state = (sysfs_exists (HDMI_STATE_OLD) == 0)
		             ? HDMI_STATE_OLD : HDMI_STATE_OLD_CABLE;
		p.sf_backdoor = false;
	}

	trace (1, "Окружение: %s (sink=%s, backdoor=%s)\n",
	       (p.kind == PLAT_ATV14) ? "ATV14" : "legacy",
	       (p.sink == SINK_SF_BACKDOOR) ? "sf-1035" : "sysfs",
	       p.sf_backdoor ? "да" : "нет");

	if (p.kind == PLAT_ATV14 && !p.sf_backdoor)
		trace (0, "ВНИМАНИЕ: backdoor SurfaceFlinger недоступен — AFR работать не будет\n");

	return p;
}
```

- [ ] **Step 2: Профиль конфига**

`config/afrd2.ini`:

```ini
# AFRd2: Android 10-14, ядро 5.x (S905X5M и подобные)

enable=1
log.file=/data/local/afrd.log
log.enable=1

# триггер
trigger.poll.ms=300
trigger.start.debounce.ms=500
trigger.stop.debounce.ms=1500

# определение частоты
fps.measure.window.ms=1500
fps.measure.retries=3
fps.prefer.api=1
#mode.blacklist.rates=29.97

# выбор режима
mode.prefer.exact=0
mode.use.fract=0

# переключение
switch.delay.on=250
switch.delay.off=3000
switch.restore.on.stop=1
pause.keep=1

# цвета (как в upstream)
cs.select=2160p[4-9].*=420 .*=444
cs.list.path=/sys/class/amhdmitx/amhdmitx0/dc_cap
cs.path=/sys/class/amhdmitx/amhdmitx0/attr

# Пути sysfs (vdec_status, состояние HDMI) задаются автодетектом окружения
# (platform_detect); в конфиге они не переопределяются.
```

- [ ] **Step 3: Сборка (host) + тесты не должны сломаться**

Run: `sh tests/run_tests.sh && make -f GNUmakefile MODE=debug`
Expected: тесты `OK`; сборка `make` создаёт `out/debug/afrd`.

Обязательно (R16): в `GNUmakefile` список `AFRD_SRC` дополняется **всеми** модулями задач 1–5
(`fps_class.c sfmodes.c sink.c guard.c exec.c vdec.c platform.c` — какие ещё не в списке),
иначе `make` не слинкуется (`undefined reference` на `exec_capture`, `sink_build_sf_reset`).
`fps_class.c` использует `math.h` → `-lm` нужен в команде линковки **после** списка объектов
(`$(LD) … -o $@ $^ -lm`): `LDFLAGS.local` подставляется до объектов, и при `--as-needed` библиотека
могла бы выпасть из `DT_NEEDED` (R29).
**`jni/Android.mk` не трогать** — его владелец Task 8 (R4).

- [ ] **Step 4: Коммит**

```bash
git add platform.h platform.c config/afrd2.ini GNUmakefile afrd.h
git commit -m "feat: автодетект окружения и конфиг-профиль для Android 10-14"
```

---

### Task 6: Новый путь исполнения `run_atv14()` в `afrd.c`

**Files:**
- Modify: `afrd.c` (новые функции; `afrd_run()` → диспетчер), `afrd.h`

**Interfaces:**
- Consumes: `platform_detect()`, `vdec_read()`, `fps_estimate()`, `fps_classify()`, `sfmodes_parse()`,
  `sfmodes_find()`, `sfmodes_current_hz()`, `sink_apply()`, `sink_release_backdoor()`, `guard_decide()`,
  `exec_capture()`, upstream: `display_modes_init()`, `display_mode_switch()`, `g_modes`, `g_cfg`.
- Produces: `static int run_atv14 (const platform_t *plat);` и сохранение поведения legacy-пути.

- [ ] **Step 1: Диспетчер в `afrd_run()`**

В начале `afrd_run()` (после инициализации конфига и до старой логики):

```c
static platform_t g_plat;

int afrd_run ()
{
	g_plat = platform_detect ();

	if (g_plat.kind == PLAT_ATV14)
		return run_atv14 (&g_plat);

	/* ... дальше существующий код upstream без изменений ... */
```

- [ ] **Step 2: Реализовать `run_atv14()` (полный код)**

```c
/* ---- ATV14: поллинг декодера + измерение fps + backdoor SurfaceFlinger ---- */

static sf_mode_t g_sf_modes [SF_MODE_MAX];
static int g_sf_modes_n = 0;

static void sf_modes_refresh (const platform_t *plat)
{
	char dump [64 * 1024];
	if (exec_capture (plat->sf_dump_cmd, dump, sizeof (dump)) != 0)
	{
		trace (1, "не удалось получить список режимов SurfaceFlinger\n");
		g_sf_modes_n = 0;
		return;
	}

	g_sf_modes_n = sfmodes_parse (dump, g_sf_modes, SF_MODE_MAX);
	trace (1, "режимов SurfaceFlinger: %d\n", g_sf_modes_n);
}

/* Активное разрешение из DisplayManager ("real 1920 x 1080").
 * Нужно независимо от частоты: в списке SurfaceFlinger одна и та же частота
 * встречается на разных разрешениях (напр. 59.94 Гц -> 1080p id 0 и 720p id 8). */
static int active_display_size (const platform_t *plat, int *w, int *h)
{
	static char dump [64 * 1024];

	if (exec_capture (plat->display_dump_cmd, dump, sizeof (dump)) != 0)
		return -1;

	const char *p = strstr (dump, "real ");
	if (!p)
		return -1;

	if (sscanf (p, "real %d x %d", w, h) != 2)
		return -1;

	return 0;
}

/* Активный режим: частота из SurfaceFlinger ("activeMode=23.98 Hz"),
 * разрешение — из DisplayManager. Совпадение по (частота, разрешение)
 * однозначно; если разрешение узнать не удалось, откатываемся на поиск по частоте. */
static int sf_active_mode (const platform_t *plat, sf_mode_t *out)
{
	static char dump [64 * 1024];

	if (exec_capture (plat->sf_dump_cmd, dump, sizeof (dump)) != 0)
		return -1;

	const char *p = strstr (dump, "activeMode=");
	if (!p)
		return -1;

	int hz = (int)(atof (p + strlen ("activeMode=")) * 256.0 + 0.5);
	if (g_sf_modes_n <= 0)
		sf_modes_refresh (plat);

	int w = 0, h = 0, id;
	if (active_display_size (plat, &w, &h) == 0)
		id = sfmodes_find (g_sf_modes, g_sf_modes_n, hz, w, h);
	else
	{
		trace (1, "не удалось определить активное разрешение, ищу по частоте\n");
		id = sfmodes_find_hz (g_sf_modes, g_sf_modes_n, hz);
	}

	if (id < 0)
		return -1;

	for (int i = 0; i < g_sf_modes_n; i++)
		if (g_sf_modes [i].id == id)
		{
			*out = g_sf_modes [i];
			return 0;
		}

	return -1;
}

/* Предпочтительный режим пользователя (настройка Android TV).
 * Возврат: mode id или -1, если пользователь его не задавал. */
static int preferred_mode_id (const platform_t *plat)
{
	static char dump [64 * 1024];

	if (exec_capture (plat->display_dump_cmd, dump, sizeof (dump)) != 0)
		return -1;

	const char *p = strstr (dump, "mUserPreferredModeId=");
	if (!p)
		return -1;

	int id = atoi (p + strlen ("mUserPreferredModeId="));
	return (id >= 0) ? id : -1;
}

int run_atv14 (const platform_t *plat)
{
	guard_action_t act;
	int orig_sf_id = -1, cur_sf_id = -1, want_sf_id = -1;
	bool held = false;
	bool video = false;
	vdec_status_t v0, v1;
	int retries = 0;
	int preferred_id = preferred_mode_id (plat);

	sf_modes_refresh (plat);

	trace (1, "предпочтительный режим ТВ: %d\n", preferred_id);

	/* страховка: снять «залипший» флаг прошлого запуска */
	sink_release_backdoor (plat->sink);

	int cur_hz = cfg_get_int ("trigger.poll.ms", 300);
	if (cur_hz < 100) cur_hz = 100;

	int window_ms = cfg_get_int ("fps.measure.window.ms", 1500);
	int max_retries = cfg_get_int ("fps.measure.retries", 3);
	int start_deb = cfg_get_int ("trigger.start.debounce.ms", 500);
	int stop_deb = cfg_get_int ("trigger.stop.debounce.ms", 1500);

	while (!g_shutdown)
	{
		msleep (cur_hz);

		if (vdec_read (plat->vdec_status, &v0) != 0)
			continue;

		bool new_video = v0.active;

		if (new_video != video)
		{
			msleep (new_video ? start_deb : stop_deb);

			vdec_status_t again;
			if (vdec_read (plat->vdec_status, &again) == 0 && again.active == new_video)
				video = new_video;
			else
				continue;	/* дребезг старта/стопа — игнорируем */
		}

		if (!video)
		{
			want_sf_id = -1;
		}
		else if (want_sf_id < 0)
		{
			/* замер: две выборки с окном window_ms */
			v1 = v0;
			unsigned long long t0 = now_us ();
			msleep (window_ms);
			if (vdec_read (plat->vdec_status, &v1) != 0)
				continue;

			double fps = fps_estimate (v0.frames, t0, v1.frames, now_us ());
			int hz = fps_classify (fps);

			if (!hz)
			{
				if (++retries >= max_retries)
				{
					trace (1, "частоту определить не удалось (%d попыток), пропускаю\n", retries);
					want_sf_id = -2;	/* до конца этого видео не пытаться */
				}
				continue;
			}

			retries = 0;
			trace (1, "видео: %.3f fps (%s), ищу режим\n", fps,
			       fps_rate_name (fps_rate_index_for_hz (hz)));

			/* текущий режим / исходный id */
			if (g_sf_modes_n <= 0)
				sf_modes_refresh (plat);

			sf_mode_t active;
			int w = 1920, h = 1080;
			if (sf_active_mode (plat, &active) == 0)
			{
				w = active.width;
				h = active.height;
			}

			cur_sf_id = sfmodes_find (g_sf_modes, g_sf_modes_n, hz, w, h);
			if (cur_sf_id < 0)
			{
				trace (1, "режим для %d.%02d Гц не найден в списке\n",
				       HZ_ARGS (hz));
				want_sf_id = -2;
				continue;
			}
			want_sf_id = cur_sf_id;
		}

		/* актуальный режим перечитываем у SurfaceFlinger: так мы видим и то,
		 * что переключило приложение со своим AFR, и не расходимся с реальностью */
		sf_mode_t active;
		if (sf_active_mode (plat, &active) == 0)
			cur_sf_id = active.id;

		act = guard_decide (video, want_sf_id, cur_sf_id, &orig_sf_id, &held);

		if (act == GUARD_SWITCH && (preferred_id >= 0))
			orig_sf_id = preferred_id;	/* возврат — в предпочтительный режим ТВ */

		if (act == GUARD_SWITCH)
		{
			if (sink_apply (plat->sink, g_mode_path, want_sf_id, NULL) == 0)
				cur_sf_id = want_sf_id;
			else
				trace (1, "переключение режима не удалось\n");
		}
		else if (act == GUARD_RESTORE)
		{
			if (sink_apply (plat->sink, g_mode_path, orig_sf_id, NULL) == 0)
			{
				guard_note_restored (&held);
				cur_sf_id = orig_sf_id;
				orig_sf_id = -1;
			}
			sink_release_backdoor (plat->sink);
			want_sf_id = -1;
			continue;
		}
		else if (act == GUARD_NONE && !video && held)
		{
			sink_release_backdoor (plat->sink);
			guard_note_restored (&held);
		}

		if (want_sf_id == -2)
		{
			/* не смогли определить: ждём конца видео, но флаг не держим */
			if (held)
			{
				sink_apply (plat->sink, g_mode_path, orig_sf_id, NULL);
				sink_release_backdoor (plat->sink);
				guard_note_restored (&held);
				cur_sf_id = orig_sf_id;
				orig_sf_id = -1;
			}
			want_sf_id = -1;
		}
	}

	/* выход: гарантированно вернуть систему в исходное состояние */
	if (held)
	{
		sink_apply (plat->sink, g_mode_path, orig_sf_id, NULL);
		sink_release_backdoor (plat->sink);
		guard_note_restored (&held);
	}

	return 0;
}
```

> Реализационно: `atof`/`atoi` требуют `<stdlib.h>`; `sf_active_mode()` и `preferred_mode_id()`
> читают `dumpsys` один раз за итерацию цикла — на 300-мс периоде это допустимо (проверено на стенде:
> вызов ~30–60 мс). Если профилирование покажет иначе, кэшировать `dumpsys display` на 5 с.

- [ ] **Step 2a: Поведенческие ключи профиля (R28): `fps.prefer.api`, `pause.keep`, `switch.restore.on.stop`**

Спека §3.2–3.3 (binding authority) требует эти ключи; Step 2 их не читал — ключи были бы инертными
(finding ревью Task 5). В начале `run_atv14()`:

```c
	int prefer_api = cfg_get_int ("fps.prefer.api", 1);
	int pause_keep = cfg_get_int ("pause.keep", 1);
	int restore_on_stop = cfg_get_int ("switch.restore.on.stop", 1);
```

1. **`fps.prefer.api` (по умолчанию 1)** — перед замером Δ`frames` проверить подсказку плеера
   (upstream-механизм живёт в этом же файле): `g_frame_rate_hint` заполняется `afrd_frame_rate_hint()`
   (afrd.c:1134) из API `frame_rate_hint`, значение уже fixed-point 24.8, срок жизни 1 с
   (`mstime_arm (&g_frame_rate_hint.stamp, 1000)`).
   ```c
   int api_hz = 0;
   if (prefer_api && mstime_running (&g_frame_rate_hint.stamp))
   	api_hz = g_frame_rate_hint.fps;
   ```
   Если `api_hz != 0` — не мерить: `hz = fps_rate_hz (fps_rate_index_for_hz (api_hz));`
   Подсказку не сбрасывать вручную (истечёт сама).
2. **`pause.keep` (по умолчанию 1)** — «пауза» = декодер активен, а счётчик кадров не растёт:
   `v0.frames` равен предыдущему значению на **двух** соседних опросах подряд. При `pause_keep==1` —
   ничего не делаем (текущее поведение). При `pause_keep==0` — обрабатываем паузу как стоп,
   передавая в `guard_decide()` `video == false` (тогда вернётся `GUARD_RESTORE`).
   Замечание: во время окна замера (`msleep (window_ms)`) счётчик на паузе стоит → `fps_estimate()` == 0,
   срабатывает ветка `retries` — она и раньше была безопасна.
3. **`switch.restore.on.stop` (по умолчанию 1)** — в ветке `GUARD_RESTORE` при **настоящем стопе**
   (декодер исчез, `v0.active == false`), а не при паузе: если `restore_on_stop == 0` — `sink_apply()`
   для возврата **не** вызываем, но `sink_release_backdoor()`, `guard_note_restored()`,
   `orig_sf_id = -1`, `want_sf_id = -1` выполняем обязательно (флаг backdoor не залипает).
   Возврат по паузе (`pause_keep==0`) выполняется всегда.

- [ ] **Step 3: Заглушки (`msleep`, `g_shutdown`)**

`msleep()` реализовать рядом (обёртка `usleep`):

```c
static void msleep (int ms)
{
	while (ms > 0 && !g_shutdown)
	{
		int chunk = ms > 200 ? 200 : ms;
		usleep (chunk * 1000);
		ms -= chunk;
	}
}
```

`g_shutdown` выставляется в `1` обработчиком `SIGTERM`/`SIGINT` (Task 7).

- [ ] **Step 4: Сборка + прогон на стенде (smoke)**

Run:
```bash
make -f GNUmakefile MODE=debug            # host-сборка (проверить, что собралось)
# далее target-сборка: см. Task 8
```
Expected: сборка без ошибок; на стенде (после Task 8) — в логе строки
`Окружение: ATV14 (sink=sf-1035, backdoor=да)` и `режимов SurfaceFlinger: 14`.

- [ ] **Step 5: Коммит**

```bash
git add afrd.c afrd.h
git commit -m "feat(atv14): новый путь run_atv14 — поллинг декодера, замер fps, backdoor-переключение"
```

---

**Фактическое состояние после Task 6** (проверено ревью и re-review; коммиты `487f6c8`, `d55a135`, `6320135`):
код Step 2 выше — черновик; реализация отличается в пяти местах, и именно она прошла ревью
(4 Important + 1 регрессия закрыты) и смоук 52/52:

1. `v0` перечитывается непосредственно перед окном замера (иначе `trigger.start.debounce.ms` входит в
   дельту кадров, но не в `dt`: 23.976 → ~31 fps → 30 Гц на весь фильм).
2. Окно замера бьётся на 3 под-среза; выборка принимается, только если декодер активен на краях и
   счётчик строго растёт в каждом под-срезе, иначе — в `retries`.
3. Сентинел `want_sf_id = -2` удалён; вместо него `bool measure_failed` + `atv14_give_up()`;
   `measure_failed` и `retries` сбрасываются на конце видео.
4. Ожидание в цикле — `atv14_wait(ms)` (`apisock_prep_poll` + `poll` + `apisock_handle`) вместо
   `msleep`, плюс `mstime_update()` в цикле; `afrd_refresh_rate()` при `PLAT_ATV14` рано выходит с логом.
5. `!plat->sf_backdoor` → пассивный режим (лог уровня 0 + `atv14_wait` в цикле, без `sink_apply`).

Источник истины по коду — `afrd.c` (`run_atv14`, `atv14_wait`, `atv14_give_up`); этот блок — карта отличий.

---

### Task 7: Guard-обработчики, стартовая страховка, инвалидация таблицы режимов

**Files:**
- Modify: `afrd.c` (обработчики сигналов, `main()`), `main.c` (регистрация SIGTERM/SIGINT)

**Interfaces:**
- Consumes: `run_atv14()` (Task 6), `sink_release_backdoor()` (Task 3), `sf_modes_refresh()` (Task 6).
- Produces: гарантия «после выхода/падения ничего не залипло», перечитывание таблицы режимов при
  рестарте SurfaceFlinger.

- [ ] **Step 1: Обработчики сигналов**

В `main.c` (рядом с существующим обработчиком завершения):

```c
static void on_term (int sig)
{
	(void)sig;
	g_shutdown = 1;		/* основной цикл вернёт режим и снимет флаг backdoor */
	/* второй сигнал — немедленный выход */
	signal (sig, SIG_DFL);
}
...
	signal (SIGTERM, on_term);
	signal (SIGINT, on_term);
```

- [ ] **Step 1a: Страховки в ATV14-цикле (R34): «потерянное видео», статистика для API, новое видео внутри `stop_deb`**

1. **Страховочный таймаут «видео потерялось»** (спека §3.1: guard снимает флаг в том числе «при
   истечении страховочного таймаута, если видео потерялось»). Сейчас при устойчивом отказе
   `vdec_read()` цикл делает `continue`, поэтому `held` и флаг backdoor могут залипнуть навсегда
   (сценарий: драйвер перестал отдавать `vdec_status`). Реализация: помнить время последнего
   **успешного** чтения, в котором декодер был активен (`last_ok_us`), и при
   `held && (now_us() - last_ok_us > timeout_ms)` выполнить `atv14_give_up()`, лог уровня 0
   («видео потеряно: N мс без данных декодера»), `video = false`, `want_sf_id = -1`.
   `timeout_ms` — существующий upstream-ключ `switch.timeout` (дефолт 3000; в профиль добавлять не
   нужно, `cfg_get_int` вернёт дефолт).
2. **Статистика для API `status`**: `update_stats()`/`shmem_update()` вызываются только в legacy-цикле,
   поэтому `afrd -s`/`status` в ATV14 отвечает нулями. Вызывать в ATV14-цикле по тикам (напр.
   `ticks % 3 == 0`) — с той же частотой, что и в legacy.
3. **Новое видео внутри `stop_deb`**: если следующее видео начнётся раньше истечения
   `trigger.stop.debounce.ms`, `vdec_read()` не отдаст «пусто», `video` не переключится, и старое
   `want_sf_id`/`measure_failed` останутся — новое видео отыграет в прежнем режиме без AFR.
   Детект: `v0.frames < prev_frames` (декодер поднял новый инстанс и счётчик сбросился) → сбросить
   `want_sf_id = -1`, `measure_failed = false`, `retries = 0`. `held`/`orig_sf_id` не трогать: режим
   уже наш. Допущение «счётчик сбрасывается на новом инстансе» подтвердить на стенде (Task 9); при
   ложном срабатывании цена мала — повторный замер даст тот же режим и `GUARD_NONE`.
4. **Отказ чтения `vdec_status`**: вести счётчик подряд идущих отказов и один раз логировать уровень 0,
   если серия превысила `switch.timeout / trigger.poll.ms`. Backoff — задача на будущее (§5 спеки),
   здесь не делаем.

- [ ] **Step 2: Инвалидация таблицы режимов и предпочтительного режима при рестарте SurfaceFlinger**

Реальный вид цикла после Task 6 (`afrd.c`, `run_atv14`):

```c
	while (!g_shutdown)
	{
		mstime_update ();
		if (atv14_wait (cur_hz) != 0)		/* poll + apisock_handle */
			msleep (cur_hz);
		…
	}
```

Счётчик тиков объявляется среди локальных `run_atv14()` (R5) — `int ticks = 0;`:

```c
		if (++ticks % 16 == 0)		/* ~каждые 5 с при 300 мс */
		{
			static pid_t last_sf_pid = -1;
			pid_t sf_pid = pidof ("surfaceflinger");
			if (sf_pid != last_sf_pid)
			{
				last_sf_pid = sf_pid;
				sf_modes_refresh (plat);
				preferred_id = preferred_mode_id (plat);	/* id тоже кэширован (спека §7) */
			}
		}
```

`preferred_id` в `run_atv14()` сейчас вычисляется один раз и не меняется — сделать его изменяемой
локальной, которую инвалидация перезаписывает. В пассивном режиме (нет backdoor) инвалидировать
нечего (переключений нет), но цикл обязан продолжать обслуживать API через `atv14_wait()`.

`pidof()` — обёртка в ATV14-секции:

```c
static pid_t pidof (const char *name)
{
	char cmd [64], out [64];
	snprintf (cmd, sizeof (cmd), "pidof %s", name);
	if (exec_capture (cmd, out, sizeof (out)) != 0)
		return -1;
	return (pid_t)atoi (out);
}
```

- [ ] **Step 3: Проверка на стенде (после Task 8)**

Run (скрипт `tools/stand/guard_test.sh`):
```sh
adb -s 192.168.88.50:5555 shell 'am start -n org.videolan.vlc/.StartActivity -a android.intent.action.VIEW \
  -d file:///sdcard/Download/afr/long_24000_1001.mp4 -t video/mp4'
sleep 8
adb -s 192.168.88.50:5555 shell '/data/local/tmp/afrd -k -D /data/local/tmp/afrd.ini &'
sleep 3
adb -s 192.168.88.50:5555 shell 'kill -TERM $(pidof afrd)'
sleep 2
adb -s 192.168.88.50:5555 shell 'cat /sys/class/display/mode; dumpsys SurfaceFlinger | grep debugDisplayModeSetByBackdoor'
```
Expected: режим вернулся к исходному (`1080p60hz`), `debugDisplayModeSetByBackdoor=false`.

**Если стенд недоступен** (проверено 2026-09-29: `adb connect 192.168.88.50:5555` → `No route to host`,
`ping` без ответа) — шаг не блокирует задачу: отметить в отчёте как непроверенное, живая приёмка
переносится в Task 9 (нужен включённый бокс).

- [ ] **Step 4: Коммит**

```bash
git add afrd.c main.c tools/stand/
git commit -m "feat(atv14): корректное завершение (SIGTERM/SIGINT) и инвалидация режимов при рестарте SF"
```

---

### Task 8: Сборка под устройство (NDK, armeabi-v7a) и запуск на стенде

**Files:**
- Modify: `jni/Application.mk`, `jni/Android.mk`, `mka`, `mkad`
- Create: `tools/stand/build_push.sh`

**Interfaces:**
- Consumes: исходники демона.
- Produces: `libs/armeabi-v7a/afrd`, залитый в `/data/local/tmp/afrd` на стенде.

- [ ] **Step 1: NDK и путь к нему**

NDK r26d **уже установлен** (2026-09-29) в `/home/reyter/Android/Sdk/android-ndk-r26d`
(`ndk-build --version` → GNU Make 4.3). Повторить установку при необходимости:

```bash
cd $HOME/Android/Sdk && curl -LO https://dl.google.com/android/repository/android-ndk-r26d-linux.zip && unzip -q android-ndk-r26d-linux.zip
```

`jni/Application.mk`:
```
APP_OPTIM := release
APP_ABI := armeabi-v7a
APP_PLATFORM := android-21
APP_CFLAGS := -Wall
```
`jni/Android.mk` — дописать новые исходники:
```
LOCAL_SRC_FILES := $(addprefix ../,main.c afrd.c sysfs.c cfg_parse/cfg_parse.c \
	cfg.c modes.c mstime.c uevent_filter.c colorspace.c strfun.c shmem.c \
	apisock.c crc32.c androp.c hdcp.c fps_class.c sfmodes.c sink.c guard.c \
	vdec.c platform.c exec.c)
```
`mka`/`mkad` — путь NDK по умолчанию:
```sh
test -z "$NDK" && export NDK=$HOME/Android/Sdk/android-ndk-r26d
```

- [ ] **Step 2: Собрать**

Run: `NDK=$HOME/Android/Sdk/android-ndk-r26d ./mka`
Expected: `libs/armeabi-v7a/afrd` создан; `file libs/armeabi-v7a/afrd` → `ELF 32-bit LSB ... ARM`.

- [ ] **Step 3: Залить и запустить на стенде**

`tools/stand/build_push.sh`:
```sh
#!/bin/sh
set -e
D=${D:-192.168.88.50:5555}
adb -s $D push libs/armeabi-v7a/afrd /data/local/tmp/afrd
adb -s $D push config/afrd2.ini /data/local/tmp/afrd.ini
adb -s $D shell 'chmod 755 /data/local/tmp/afrd'
adb -s $D shell '/data/local/tmp/afrd -v -D /data/local/tmp/afrd.ini' &
sleep 3
adb -s $D shell 'pidof afrd && tail -20 /data/local/afrd.log'
```
Expected: демон поднялся, в логе — строка `Окружение: ATV14 (sink=sf-1035, backdoor=да)`.

- [ ] **Step 4: Коммит**

```bash
git add jni/ mka mkad tools/stand/build_push.sh libs 2>/dev/null || git add jni/ mka mkad tools/stand/build_push.sh
git commit -m "build: сборка armeabi-v7a через NDK r26d, скрипт доставки на стенд"
```

---

### Task 9: Матрица приёмки на стенде

**Files:**
- Create: `tools/stand/play_clip.sh`, `tools/stand/check_mode.sh`, `tools/stand/acceptance_matrix.sh`
- Modify: `README.md`, `ChangeLog-ru.txt`, `docs/superpowers/specs/2026-09-29-afrd2-design.md` (отметка о статусе)

**Interfaces:**
- Consumes: собранный демон на стенде (Task 8).
- Produces: протокол приёмки (лог + запись в журнал).

- [ ] **Step 1: Скрипты проверки**

`tools/stand/check_mode.sh`:
```sh
#!/bin/sh
# Печатает: kernel mode | HDMI timing | SF activeMode | backdoor flag
D=${D:-192.168.88.50:5555}
adb -s $D shell 'printf "%s | %s | %s | %s\n" \
  "$(cat /sys/class/display/mode)" \
  "$(cat /sys/class/amhdmitx/amhdmitx0/disp_mode | grep sname)" \
  "$(dumpsys SurfaceFlinger | grep -oE "activeMode=[0-9.]* Hz" | head -1)" \
  "$(dumpsys SurfaceFlinger | grep -oE "debugDisplayModeSetByBackdoor=[a-z]*" | head -1)"'
```

`tools/stand/acceptance_matrix.sh`:
```sh
#!/bin/sh
# VLC без встроенного AFR; проверяем переключение и возврат для каждого клипа
D=${D:-192.168.88.50:5555}
for clip in long_24000_1001 long_25 long_50; do
	echo "=== $clip"
	adb -s $D shell "am force-stop org.videolan.vlc"
	sleep 2
	adb -s $D shell "am start -n org.videolan.vlc/.StartActivity -a android.intent.action.VIEW \
		-d file:///sdcard/Download/afr/$clip.mp4 -t video/mp4" >/dev/null
	sleep 8
	sh "$(dirname "$0")/check_mode.sh"
	adb -s $D shell "am force-stop org.videolan.vlc"
	sleep 5
	sh "$(dirname "$0")/check_mode.sh"
done
```

- [ ] **Step 2: Прогнать матрицу**

Run: `sh tools/stand/acceptance_matrix.sh`
Expected (эталон):
| клип | во время видео | после стопа |
|---|---|---|
| 23.976 | `1080p24hz` + `activeMode=23.98 Hz` | `1080p60hz` + `59.94` + `backdoor=false` |
| 25 | `1080p50hz` + `50.00 Hz` | `1080p60hz` + `59.94` + `backdoor=false` |
| 50 | `1080p50hz` + `50.00 Hz` | `1080p60hz` + `59.94` + `backdoor=false` |

- [ ] **Step 2a: Пауза (`pause.keep`)**

Run (на стенде, `long_24000_1001` играет через VLC):
```sh
D=192.168.88.50:5555
adb -s $D shell 'input keyevent KEYCODE_MEDIA_PAUSE'; sleep 4; sh tools/stand/check_mode.sh
adb -s $D shell 'input keyevent KEYCODE_MEDIA_PLAY';  sleep 4; sh tools/stand/check_mode.sh
```
Expected: при `pause.keep=1` (профиль по умолчанию) режим на паузе **не** меняется (остаётся `1080p24hz`),
после возобновления — тоже; при `pause.keep=0` на паузе режим возвращается к исходному
(`1080p60hz`, `backdoor=false`), после возобновления снова `1080p24hz`.
Проверить оба значения: правка `config/afrd2.ini` на стенде (`/data/local/tmp/afrd.ini`) + рестарт демона.

- [ ] **Step 3a: Проверки ATV14-специфики (API, подсказка плеера, сброс счётчика)**

Run (стенд; демон запущен, режим удержан или воспроизведение активно):
```sh
D=192.168.88.50:5555
# 1) статус через API: не нули (Task 7 научил update_stats() состоянию ATV14)
adb -s $D shell 'echo status | nc -u -w1 127.0.0.1 50505'
# 2) подсказка плеера должна опережать замер (окно замера + дебаунс ~2 с, подсказка живёт 1 с)
adb -s $D shell 'echo "frame_rate_hint 23976" | nc -u -w1 127.0.0.1 50505'
sleep 1; adb -s $D shell 'grep -E "по подсказке|видео:" /data/local/afrd.log | tail -3'
# 3) допущение R34в: счётчик кадров нового инстанса декодера начинается с нуля
adb -s $D shell 'cat /sys/class/vdec/vdec_status'   # до и после старта нового файла
```
Expected: (1) в ответе есть ненулевые поля (`current hz`/`enabled`); (2) в логе строка
«видео: 23.98 Гц по подсказке плеера» и **отсутствует** ветка замера для этого видео;
(3) `frame count` после старта нового файла меньше, чем в конце предыдущего (подтверждает детект
«нового видео внутри `stop_deb`»); если счётчик **не** сбрасывается — записать это как
опровергнутое допущение и убрать детект сброса (R34в) в отдельную заявку.

- [ ] **Step 3b: Починить выдачу в `build_push.sh` (находка re-review Task 8)**

`pkill -f /data/local/tmp/afrd` убивает и саму оболочку, выполняющую команду (`adb shell` запускает
строку через `sh -c`, шаблон совпадает с её собственной командной строкой), поэтому следующий
`rm -f` не выполняется и риск `ETXTBSY` при повторном `push` остаётся. Разнести на две команды с
якорем, например:
```sh
adb -s "$D" shell 'pkill -f "^/data/local/tmp/afrd"' || true
adb -s "$D" shell 'rm -f /data/local/tmp/afrd'
```

- [ ] **Step 3: Совместимость с приложением со своим AFR**

Run:
```sh
adb -s 192.168.88.50:5555 shell 'am start -n net.gtvbox.videoplayer/.PlayerActivity -a android.intent.action.VIEW \
  -d file:///sdcard/Download/afr/long_24000_1001.mp4 -t video/mp4'
sleep 8; sh tools/stand/check_mode.sh
```
Expected: `1080p24hz`, переключил сам плеер (в логе демона — «режим уже подходит, не вмешиваюсь»).

- [ ] **Step 4: Документация и коммит**

- `README.md`: раздел «Android 10–14 (ATV14)» — как включить профиль, как проверить, какие ограничения.
  В разделе «Ограничения» перечислить **явно** (иначе пользователь считает это багом):
  (а) цветовые пространства/HDR (`attr`/`dc_cap`) на ATV14-пути не применяются — цвета не
  переключаются вместе с режимом (R35); (б) HDCP не управляется; (в) `reconf` (перечитка конфига) и
  `color_space` через API в ATV14 не действуют — смена конфига требует рестарта демона; (г) без
  backdoor SurfaceFlinger (другие прошивки/будущие Android) демон уходит в пассивный режим и AFR не
  работает; (д) цвета/HDR и HDCP — темы отдельных этапов.
- `ChangeLog-ru.txt`: запись `0.4.0` о порте.
- Спека: пометить §6 как выполненный (дата, результаты).

```bash
git add tools/stand/ README.md ChangeLog-ru.txt docs/
git commit -m "test(atv14): матрица приёмки на стенде + документация порта"
```

---

## Self-Review (выполнено автором плана)

**1. Покрытие спеки:**
- §1 критерии приёмки → Task 9 (матрица), Task 7 (SIGTERM), Task 5 (автостарт — покрывается приложением, отдельный план).
- §2 факты → реализация в Task 4 (vdec без dump-узлов), Task 3 (backdoor), Task 5 (детект).
- §3 архитектура → Task 6 (оркестрация), Task 1-4 (модули), §3.3 конфиг → Task 5.
- §4 аддитивность → Task 5 (диспетчер `platform_detect`), Task 6 (новая ветка, старая не тронута).
- §5 упаковка APK → **отдельный план** (объявлено в спеке §8.1 и в шапке этого плана).
- §6 тесты → Task 9 (+ юнит-тесты в Task 1-4).
- §7 риски → Task 3 (снятие флага), Task 7 (страховки), Task 6 (`-2` ветка при неудачном замере).
- §8 решения → учтены: пауза (R28: `pause.keep=1` — на паузе счётчик стоит, `fps_estimate` вернёт 0 и
  переключения не будет; `pause.keep=0` — пауза обрабатывается как стоп, Task 6 Step 2a),
  затемнения нет (нет вызова `display_mode_null`),
  preferred mode — `preferred_mode_id()` (Task 6) и возврат в него в ветке `GUARD_SWITCH`; `fps.prefer.api`
  и `switch.restore.on.stop` — Task 6 Step 2a.

**2. Плейсхолдеры:** отсутствуют (проверено поиском по «TODO/TBD/позже/implement»).

**3. Согласованность типов:** `sf_mode_t.{id,width,height,hz}`, `sink_kind_t`, `guard_action_t`,
`vdec_status_t`, `platform_t` — используются одинаково во всех задачах; `sink_apply()` принимает
`(kind, mode_path, sf_mode_id, legacy_mode_name)`, `guard_decide()` — `(video, want, cur, &orig, &held)`.

**4. Review Focus → тесты:** (1) пауза → `test_fps_class.c::t_estimate` (`frames` равны → 0);
(2) короткий клип → `run_atv14()` дебаунсы + Task 9 (проверка на 12-секундных клипах);
(3) 23.976/24 → `test_fps_class.c::t_classify`, `test_sfmodes.c` (id 6 vs 7);
(4) нет backdoor → `platform_detect()` + предупреждение в логе (Task 5, Step 1), проверка на стенде — Task 9;
(5) два видео подряд → `run_atv14()` `guard_decide` + `sink_release_backdoor` (Task 6/7).
