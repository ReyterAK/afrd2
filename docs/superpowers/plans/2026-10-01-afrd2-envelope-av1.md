# AFRd2: нижняя огибающая частоты кадров и кодековая поправка AV1 — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** На ATV14-пути демона принимать решение о частоте контента по нижней огибающей
подокон (устойчиво к одностороннему завышению счётчика кадров) и добавить опциональную
аддитивную поправку для AV1-тракта — выключенную по умолчанию.

**Architecture:** Новый модуль `fps_env` (кольцо выборок «время, счётчик, длительность чтения»
и решение по минимуму частоты среди подокон) + аддитивная/кодековая поправка в существующем
`fps_class` (расширение `window_decide`). `run_atv14()` в `afrd.c` перестаёт крутить блокирующее
окно замера с расширением: он копит выборки в кольце (то же единственное чтение `vdec_status` за
итерацию) и решает, когда набрался горизонт наблюдения.

**Tech Stack:** C (bionic, armeabi-v7a), NDK (`ndk-build`), host-сборка gcc для юнит-тестов,
ADB-скрипты стенда, Android 14 / Amlogic S905X5M.

**Spec:** `docs/superpowers/specs/2026-10-01-afrd2-envelope-av1-spec.md` (измерения — там же;
журнал: `/home/reyter/AI/afrd2-journal.md`, §14–§16).

## Global Constraints

- Форк аддитивный: legacy-путь (Android 6–9) не менять; новые ключи действуют только на ATV14.
- **Никогда** не открывать `/sys/class/vdec/dump_vdec_chunks`, `dump_vdec_blocks`, `dump_path_monitor`
  (kernel panic, журнал §4).
- Поля `frame rate` / `frame dur` из `vdec_status` не использовать как источник fps
  (`frame dur` — только как уточнение рядом с замером, как сейчас).
- Целевая сборка: `APP_ABI=armeabi-v7a` (и arm64), `APP_PLATFORM=android-21`, запуск под root.
- Хостовые тесты: `sh tests/run_tests.sh` — все наборы зелёные; сборка `make MODE=debug` и
  полная `rm -rf obj libs && ./mka` — без предупреждений.
- Стенд: `192.168.88.50:5555`; клипы в `/sdcard/Download/afr/`; изменения в ОС бокса — только
  после согласования с владельцем.
- `cfgkeys.c` — единственный источник правды о применимости ключей: новый ключ без строки в
  таблице роняет host-тест `test_cfgkeys` и пишет «неизвестный ключ» в лог.
- Стиль upstream: табы, `trace()`, `snake_case`, модули в корне репозитория.
- Частота — fixed-point 24.8 (`HZ_FMT`/`HZ_ARGS`), представитель группы — дробный
  (`fps_rate_hz(fps_rate_index_for_hz(hz))`).

## Review Focus

Входы/ситуации, которые спека не расписывает построчно, но которые сломают пользователя
(тест на каждую — в задаче-владельце):

1. **Пауза/буферизация внутри наблюдения** — счётчик стоит: окно с простоем не должно давать
   решение, наблюдение не должно «зависнуть» навсегда, а после возобновления — начаться заново. (Task 2/3)
2. **Сброс счётчика (новое видео раньше stop_debounce)** — кадры двух видео не должны попасть
   в одну огибающую. (Task 2/3)
3. **Короткий клип / видео короче горизонта** — переключения быть не должно вообще (и уж точно
   не «туда-обратно»). (Task 3)
4. **Провал в первые секунды (VLC-набор буфера: 60 fps мерился как 50.3)** — огибающая не должна
   уехать вниз вслед за стартовым провалом; отсюда `skip.ms`. (Task 2/3)
5. **Устаревшие ключи в конфиге** (`fps.measure.window.ms`) — пользователь должен увидеть
   «громкий» отказ в логе, а не молча потерянную настройку. (Task 3)

---

## Структура файлов

Создаются:

| Файл | Ответственность |
|---|---|
| `fps_env.h/.c` | кольцо выборок счётчика + решение по нижней огибающей подокон |
| `tests/test_fps_env.c` | юнит-тесты огибающей (синтетика + живые фикстуры) |
| `tests/fixtures/fps_env_24av1_ok.txt`, `fps_env_24av1_high.txt`, `fps_env_25av1.txt`, `fps_env_24vp9.txt` | реальные записи счётчика (штатный YouTube, 2026-10-01) |

Изменяются:

| Файл | Что |
|---|---|
| `fps_class.h/.c` | `fps_classify_window_corrected()` + аддитивная кодековая поправка (`fps.correct.av1.*`) |
| `tests/test_fps_class.c` | тесты поправок |
| `tests/run_tests.sh`, `GNUmakefile` | `fps_env.c` в сборке и в тестах |
| `afrd.c` | `run_atv14()`: кольцо выборок вместо блокирующего окна; чтение новых ключей; логи |
| `cfgkeys.h/.c` | новые ключи + примечание об устаревших (`fps.measure.window.*`) |
| `tests/test_cfgkeys.c` | новые ключи в таблице/профилях |
| `config/afrd2.ini` | новые ключи с комментариями, устаревшие удалены |
| `android/.../Config.java`, `res/xml/preferences.xml`, `res/values/strings.xml` | пункты GUI для новых ключей |
| `tools/stand/acceptance_matrix.sh`, `verify_latency.sh`, `verify_pause_early.sh`, `verify_fix.sh` | ожидания под новую латентность |
| `README.md`, `ChangeLog-ru.txt` | документация |

Не трогаем: legacy-ветки (`modes.c`, `uevent_filter.c`, `colorspace.c`), приёмник (`sink.c`),
`sfmodes.c`, `guard.c`, `platform.c`, `vdec.c`.

---

### Task 1: `fps_class` — аддитивная поправка и кодековая поправка AV1

**Files:**
- Modify: `fps_class.h`, `fps_class.c`
- Test: `tests/test_fps_class.c`

**Interfaces:**
- Consumes: существующие `window_decide()`, `fps_correct_clear()`, `fps_correct_load()`
- Produces (для Task 2/3):
  - `int fps_classify_window_corrected (unsigned long long frames_delta, unsigned long long dt_us, unsigned long long dt_unc_us, double factor, double offset, int *hz_out);`
    `frames_adj = frames_delta / factor - offset * dt_s`; `factor <= 0` → 1.0.
  - `void fps_correct_set_av1 (double offset_fps, const char *package);`
  - `void fps_correct_clear_av1 (void);`
  - `double fps_correct_av1_offset (const char *device, const char *package);` — 0.0, если поправка
    не задана, либо `device` не содержит «av1», либо задан `package` и он не совпал.
  - `fps_correct_clear()` дополнительно сбрасывает и AV1-поправку.

- [ ] **Step 1: Написать падающие тесты**

Добавить в `tests/test_fps_class.c` (перед `int main(void)`) и вызвать из `main`:

```c
/* Аддитивная поправка (fps.correct.av1.offset) и множитель плеера применяются
 * к числу кадров окна, а не к уже готовой частоте. */
static void t_corrected(void)
{
	int hz = -1;

	/* 24 fps за 5 с (120 кадров): без поправок — группа 24 */
	assert(fps_classify_window_corrected(120, 5000000, 0, 1.0, 0.0, &hz) == 1);
	assert(hz == fps_rate_hz(0));

	/* завышение AV1 на 0.5 кадра/с: 122.5 кадра за 5 с — всё ещё группа 24 */
	assert(fps_classify_window_corrected(122, 5000000, 0, 1.0, 0.5, &hz) == 1);
	assert(hz == fps_rate_hz(0));

	/* 128 кадров за 5 с (25.6 fps) без поправки — это уже 25 */
	assert(fps_classify_window_corrected(128, 5000000, 0, 1.0, 0.0, &hz) == 1);
	assert(hz == fps_rate_hz(2));

	/* 125 кадров за 5 с = 25.0; поправка 0.5 даёт 24.5, а ±1 кадр накрывает
	 * и 24, и 25 -> решения нет */
	assert(fps_classify_window_corrected(125, 5000000, 0, 1.0, 0.5, &hz) == 0);

	/* множитель (fps.correct.list) работает как раньше: 126/1.05 = 120 -> 24 */
	assert(fps_classify_window_corrected(126, 5000000, 0, 1.05, 0.0, &hz) == 1);
	assert(hz == fps_rate_hz(0));

	/* factor <= 0 трактуется как «без поправки» */
	assert(fps_classify_window_corrected(120, 5000000, 0, 0.0, 0.0, &hz) == 1);
	assert(hz == fps_rate_hz(0));
}

static void t_correct_av1(void)
{
	fps_correct_clear ();

	/* поправка не задана — ноль для любого устройства */
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", "com.google.android.youtube.tv") == 0.0);

	/* поправка без пакета действует для любого плеера, но только на AV1 */
	fps_correct_set_av1 (0.5, NULL);
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", "org.videolan.vlc") == 0.5);
	assert(fps_correct_av1_offset ("ammvdec_h264_v4", "org.videolan.vlc") == 0.0);
	assert(fps_correct_av1_offset ("", "org.videolan.vlc") == 0.0);

	/* с пакетом — только для него (пакет в конфиге бывает с пробелами) */
	fps_correct_set_av1 (1.05, " com.google.android.youtube.tv ");
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", "com.google.android.youtube.tv") == 1.05);
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", "org.videolan.vlc") == 0.0);

	/* fps_correct_clear сбрасывает и её */
	fps_correct_set_av1 (0.5, NULL);
	fps_correct_clear ();
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", NULL) == 0.0);

	/* выключенная поправка (0) — не действует */
	fps_correct_set_av1 (0.0, NULL);
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", NULL) == 0.0);
}
```

**Замечание по значениям:** 125 кадров за 5 с = 25.0 fps; с поправкой −0.5 → 24.5 fps, а ±1 кадр
накрывает и 24, и 25 — ожидаем `0`. Если фактическое поведение отличается — не подгонять тест под
код, а разбираться: значения посчитаны по формуле `window_decide`.

- [ ] **Step 2: Убедиться, что тесты не компилируются**

Run: `sh tests/run_tests.sh`
Expected: ошибка компиляции/линковки `fps_classify_window_corrected` (остальные наборы проходят).

- [ ] **Step 3: Реализовать**

В `fps_class.h` после `fps_classify_window_scaled`:

```c
/* Решение по окну с поправками: frames_adj = frames_delta / factor - offset * dt_с.
 *   factor — относительная поправка плеера (fps.correct.list), 1.0 — нет;
 *   offset — аддитивная поправка в кадрах/с (fps.correct.av1.offset), 0.0 — нет.
 * Аддитивная нужна отдельно от множителя: завышение AV1-тракта на стенде
 * постоянно в кадрах/с (24.52 вместо 24.00), а не в процентах.
 * Возврат: 1 — решение есть (*hz_out), 0 — окно мало. */
int fps_classify_window_corrected (unsigned long long frames_delta, unsigned long long dt_us,
                                   unsigned long long dt_unc_us,
                                   double factor, double offset, int *hz_out);

/* ---- Поправка на завышение счётчика кодеком (fps.correct.av1.*) ----
 *
 * AV1-тракт декодера S905X5M отдаёт больше кадров, чем номинал: живой замер
 * 2026-10-01 в стороннем плеере (Just Player, один и тот же файл Sintel
 * 24.000 fps): H.264 — 24.002 fps, AV1 — 24.520 fps (+0.52 кадра/с, n=3/2).
 * Штатный клиент YouTube добавляет к этому своё (границы DASH-сегментов).
 *
 * offset_fps <= 0 — поправка выключена. package == NULL или "" — поправка для
 * любого плеера; иначе — только для указанного. */
void fps_correct_set_av1 (double offset_fps, const char *package);

/* Сбросить поправку (используется fps_correct_clear и при enable=0). */
void fps_correct_clear_av1 (void);

/* Поправка, применимая к этому декодеру и плееру (0.0 — не применять). */
double fps_correct_av1_offset (const char *device, const char *package);
```

В `fps_class.c`:

```c
int fps_classify_window_corrected (unsigned long long frames_delta, unsigned long long dt_us,
                                   unsigned long long dt_unc_us,
                                   double factor, double offset, int *hz_out)
{
	if (factor <= 0.0)
		factor = 1.0;

	double dt_s = (double)dt_us / 1000000.0;
	double adj = (double)frames_delta / factor - offset * dt_s;

	return window_decide (adj, dt_us, dt_unc_us, hz_out);
}
```

`fps_classify_window()` и `fps_classify_window_scaled()` переписать через неё:

```c
int fps_classify_window (unsigned long long frames_delta, unsigned long long dt_us,
                         unsigned long long dt_unc_us, int *hz_out)
{
	return fps_classify_window_corrected (frames_delta, dt_us, dt_unc_us, 1.0, 0.0, hz_out);
}

int fps_classify_window_scaled (unsigned long long frames_delta, unsigned long long dt_us,
                                unsigned long long dt_unc_us, double correct, int *hz_out)
{
	return fps_classify_window_corrected (frames_delta, dt_us, dt_unc_us, correct, 0.0, hz_out);
}
```

Хранение поправки и поиск AV1 (регистронезависимо, без `strcasestr` — он есть не везде):

```c
static struct
{
	double	offset;
	char	pkg [64];
} k_av1;

void fps_correct_clear_av1 (void)
{
	k_av1.offset = 0.0;
	k_av1.pkg [0] = 0;
}

void fps_correct_set_av1 (double offset_fps, const char *package)
{
	fps_correct_clear_av1 ();
	if (offset_fps <= 0.0)
		return;

	k_av1.offset = offset_fps;
	if (package)
	{
		while ((*package == ' ') || (*package == '\t'))
			package++;

		size_t len = strcspn (package, " \t");
		if (len && (len < sizeof (k_av1.pkg)))
		{
			memcpy (k_av1.pkg, package, len);
			k_av1.pkg [len] = 0;
		}
	}
}

static bool contains_ci (const char *hay, const char *needle)
{
	if (!hay || !needle || !*needle)
		return false;

	for (; *hay; hay++)
	{
		const char *h = hay, *n = needle;
		while (*h && *n && (tolower ((unsigned char)*h) == tolower ((unsigned char)*n)))
		{
			h++;
			n++;
		}
		if (!*n)
			return true;
	}

	return false;
}

double fps_correct_av1_offset (const char *device, const char *package)
{
	if (k_av1.offset <= 0.0)
		return 0.0;

	if (k_av1.pkg [0] && (!package || (strcmp (k_av1.pkg, package) != 0)))
		return 0.0;

	if (!contains_ci (device, "av1"))
		return 0.0;

	return k_av1.offset;
}
```

`#include <ctype.h>` в начало `fps_class.c`; в `fps_correct_clear()` добавить `fps_correct_clear_av1 ();`.

- [ ] **Step 4: Прогнать тесты**

Run: `sh tests/run_tests.sh`
Expected: все наборы зелёные (в т.ч. `test_fps_class: OK`).

- [ ] **Step 5: Коммит**

```bash
git add fps_class.h fps_class.c tests/test_fps_class.c
git commit -m "feat(fps): аддитивная кодековая поправка (fps.correct.av1.*) и fps_classify_window_corrected"
```

---

### Task 2: модуль `fps_env` — нижняя огибающая по подокнам

**Files:**
- Create: `fps_env.h`, `fps_env.c`, `tests/test_fps_env.c`
- Create: `tests/fixtures/fps_env_24av1_ok.txt`, `tests/fixtures/fps_env_24av1_high.txt`,
  `tests/fixtures/fps_env_25av1.txt`, `tests/fixtures/fps_env_24vp9.txt`
- Modify: `tests/run_tests.sh` (строка `run test_fps_env fps_env.c fps_class.c`),
  `GNUmakefile` (`AFRD_SRC`)

**Interfaces:**
- Consumes: `fps_classify_window_corrected()` (Task 1), `fps_rate_hz()`/`fps_rate_index_for_hz()` (есть)
- Produces (для Task 3):
  - `void fps_env_reset (fps_env_t *e);`
  - `bool fps_env_push (fps_env_t *e, unsigned long long t_us, unsigned long long frames, unsigned long long unc_us);`
  - `bool fps_env_ready (const fps_env_t *e, unsigned long long horizon_us);`
  - `int fps_env_decide (const fps_env_t *e, unsigned long long horizon_us, unsigned long long skip_us,
    unsigned long long sub_us, double factor, double offset, int *hz_out);`

- [ ] **Step 1: Создать фикстуры (реальные записи счётчика со стенда)**

Формат: `<относительное время, мс><TAB><прирост frame count>`, строки с `#` — комментарий.

`tests/fixtures/fps_env_24av1_ok.txt` — ролик `6ifgtTCexgs` (номинал 24 fps, AV1,
огибающая даёт верный класс):

```
# 24-fps AV1 (штатный YouTube, 2026-10-01): огибающая даёт группу 24
0	0
1080	27
2150	51
3220	79
4290	107
5350	131
6420	158
7480	183
8550	210
9610	237
10680	267
11750	291
12820	316
13900	341
14970	367
16050	395
17120	422
18190	451
19260	478
20330	502
21410	531
22480	556
23550	582
24620	611
25680	635
26750	662
27820	687
28900	716
29960	741
31050	770
```

`tests/fixtures/fps_env_24av1_high.txt` — ролик `80bkVen5CJI` (номинал 24 fps, AV1,
огибающая всё равно даёт 25 — известное ограничение, тест его фиксирует):

```
# 24-fps AV1 (штатный YouTube, 2026-10-01): огибающая даёт 25 — известное ограничение
0	0
1070	28
2160	55
3230	84
4300	108
5370	136
6450	163
7520	191
8590	220
9650	249
10720	273
11790	301
12850	327
13920	355
14990	382
16060	411
17150	438
18220	465
19290	494
20360	520
21430	549
22500	577
23560	600
24640	629
25710	656
26780	685
27850	711
28920	737
29980	764
31050	793
```

`tests/fixtures/fps_env_25av1.txt` — ролик `Q7NZK6h9Tvo` (номинал 25 fps, AV1):

```
# 25-fps AV1 (штатный YouTube, 2026-10-01)
0	0
1070	28
2140	55
3210	83
4280	113
5350	138
6420	169
7490	197
8560	224
9620	252
10690	280
11760	307
12840	337
13900	362
14970	392
16040	418
17100	446
18170	476
19240	505
20310	531
21410	563
22480	587
23550	616
24620	644
25690	674
26760	699
27830	730
28900	756
29970	784
31030	813
```

`tests/fixtures/fps_env_24vp9.txt` — ролик `Du481sOHopU` (номинал 24 fps, VP9; счётчик точен):

```
# 24-fps VP9 (штатный YouTube, 2026-10-01): счётчик точен
0	0
1070	25
2140	53
3200	78
4260	101
5340	127
6410	153
7470	179
8550	206
9620	232
10690	258
11760	282
12830	308
13900	332
14970	359
16050	385
17110	412
18180	435
19260	461
20330	487
21390	513
22450	539
23520	563
24590	588
25660	616
26730	641
27790	666
28860	691
29930	717
31000	743
```

- [ ] **Step 2: Написать падающий тест**

`tests/test_fps_env.c`:

```c
#include <assert.h>
#include <stdio.h>
#include "../fps_env.h"
#include "../fps_class.h"

#define ENV_HORIZON	20000000ULL	/* профиль: fps.measure.envelope.ms */
#define ENV_SUB		5000000ULL	/* профиль: fps.measure.envelope.sub.ms */
#define ENV_SKIP	2000000ULL	/* профиль: fps.measure.envelope.skip.ms */

/* Фикстура: строки "<мс>\t<прирост счётчика>". */
static int load_fixture (const char *path, fps_env_t *env)
{
	FILE *f = fopen (path, "r");
	assert (f);

	fps_env_reset (env);
	char line [128];
	unsigned long long frames = 0;
	while (fgets (line, sizeof (line), f))
	{
		if (line [0] == '#' || line [0] == '\n')
			continue;

		unsigned long long ms = 0, d = 0;
		if (sscanf (line, "%llu\t%llu", &ms, &d) != 2)
			continue;

		frames += d;
		fps_env_push (env, ms * 1000, frames, 2000);	/* чтение ~2 мс */
	}

	fclose (f);
	return env->n;
}

static void t_fixtures (void)
{
	fps_env_t env;
	int hz = -1;

	assert (load_fixture ("tests/fixtures/fps_env_24av1_ok.txt", &env) > 20);
	assert (fps_env_ready (&env, ENV_HORIZON));
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 1);
	assert (hz == fps_rate_hz (0));		/* 23.976 -> группа 24 */

	/* AV1 соврал настолько, что огибающая без поправки даёт 25 — фиксируем
	 * известное ограничение; с поправкой 0.5 кадра/с ряд даёт группу 24 */
	assert (load_fixture ("tests/fixtures/fps_env_24av1_high.txt", &env) > 20);
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 1);
	assert (hz == fps_rate_hz (2));
	/* тот же ряд с поправкой AV1 0.5 кадра/с даёт 24.41 fps, а доверительный
	 * интервал (±1 кадр = ±0.2 fps) пересекает границу 24.49: решения нет.
	 * Поправка не «спасает» худший случай, а уводит его из чужого класса
	 * в «не определилось» — так и должно быть. */
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.5, &hz) == 0);
	assert (hz == 0);

	assert (load_fixture ("tests/fixtures/fps_env_25av1.txt", &env) > 20);
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 1);
	assert (hz == fps_rate_hz (2));
	/* поправка -0.5 не должна сломать честный 25-фпс ряд */
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.5, &hz) == 1);
	assert (hz == fps_rate_hz (2));

	assert (load_fixture ("tests/fixtures/fps_env_24vp9.txt", &env) > 20);
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 1);
	assert (hz == fps_rate_hz (0));
}

/* Синтетика: 60 fps, первые 2 с — 50 fps (набор буфера). Прогрев 2 с
 * выбрасывает провальный участок, поэтому решение — 59.94, а не 50.
 * (Живой случай: журнал §13.9 — первое окно 50.332 fps при эталоне 60.086.) */
static void t_startup_dip (void)
{
	fps_env_t env;
	fps_env_reset (&env);

	unsigned long long t = 0, f = 0;
	for (int i = 1; i <= 31; i++)
	{
		t += 1000000;
		f += (i <= 2) ? 50 : 60;
		fps_env_push (&env, t, f, 2000);
	}

	int hz = -1;
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 1);
	assert (hz == fps_rate_hz (6));		/* 59.94 */
}

/* Пауза: окно с простоем не рассматривается, наблюдение начинается заново. */
static void t_pause (void)
{
	fps_env_t env;
	fps_env_reset (&env);

	unsigned long long t = 0, f = 0;
	for (int i = 0; i < 8; i++)
	{
		t += 1000000;
		f += 25;
		fps_env_push (&env, t, f, 2000);
	}
	assert (fps_env_push (&env, t + 1000000, f, 2000) == false);	/* счётчик стоит */
	assert (env.n == 1);						/* наблюдение заново */

	for (int i = 0; i < 12; i++)
	{
		t += 1000000;
		f += 25;
		fps_env_push (&env, t, f, 2000);
	}
	assert (fps_env_ready (&env, ENV_HORIZON - 9000000));	/* 11 с из 13 выборок */

	int hz = -1;
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 1);
	assert (hz == fps_rate_hz (2));		/* 25 */
}

/* Сброс счётчика (новое видео): кадры двух видео не попадают в одну огибающую. */
static void t_counter_reset (void)
{
	fps_env_t env;
	fps_env_reset (&env);

	unsigned long long t = 0;
	for (unsigned long long f = 0; f <= 120000; f += 60000)	/* 60 fps, 2 с */
	{
		t += 1000000;
		fps_env_push (&env, t, f, 2000);
	}
	assert (fps_env_push (&env, t + 1000000, 12, 2000) == false);	/* счётчик сброшен */
	assert (env.n == 1);

	for (int i = 1; i <= 12; i++)				/* дальше 25 fps */
		fps_env_push (&env, t + 1000000 + i * 1000000ULL, 12 + 25 * (unsigned)i, 2000);

	int hz = -1;
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 1);
	assert (hz == fps_rate_hz (2));		/* 25, а не смесь 60 и 25 */
}

/* Пока горизонт не набран — решения нет. */
static void t_ready (void)
{
	fps_env_t env;
	fps_env_reset (&env);

	unsigned long long t = 0;
	for (int i = 1; i <= 5; i++)
	{
		t += 1000000;
		fps_env_push (&env, t, (unsigned long long)(i * 25), 2000);
	}

	assert (!fps_env_ready (&env, ENV_HORIZON));

	int hz = -1;
	assert (fps_env_decide (&env, ENV_HORIZON, 0, ENV_SUB, 1.0, 0.0, &hz) == 0);
}

/* Кольцо: при переполнении старые выборки вытесняются, решение всё равно есть. */
static void t_ring_overflow (void)
{
	fps_env_t env;
	fps_env_reset (&env);

	unsigned long long t = 0;
	for (int i = 1; i <= FPS_ENV_MAX * 2 + 5; i++)
	{
		t += 300000;						/* 300 мс — штатный опрос */
		fps_env_push (&env, t, (unsigned long long)(i * 7), 2000);	/* 23.3 fps */
	}
	assert (env.n <= FPS_ENV_MAX);

	int hz = -1;
	assert (fps_env_decide (&env, ENV_HORIZON, 0, ENV_SUB, 1.0, 0.0, &hz) == 1);
}

int main (void)
{
	t_fixtures ();
	t_startup_dip ();
	t_pause ();
	t_counter_reset ();
	t_ready ();
	t_ring_overflow ();
	printf ("fps_env: OK\n");
	return 0;
}
```

- [ ] **Step 3: Убедиться, что тест падает**

Run: `sh tests/run_tests.sh`
Expected: ошибка компиляции (`fps_env.h` не найден) — модуля ещё нет.

- [ ] **Step 4: Реализовать `fps_env.h`**

```c
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
```

- [ ] **Step 5: Реализовать `fps_env.c`**

```c
/*
 * Нижняя огибающая частоты кадров: см. fps_env.h.
 */

#include <string.h>
#include "fps_env.h"
#include "fps_class.h"

void fps_env_reset (fps_env_t *e)
{
	e->n = 0;
}

bool fps_env_push (fps_env_t *e, unsigned long long t_us,
                   unsigned long long frames, unsigned long long unc_us)
{
	if (e->n > 0 && (frames <= e->s [e->n - 1].frames))
	{
		/* пауза (счётчик стоит) или новое видео (счётчик сброшен):
		 * прежние выборки к текущему наблюдению не относятся */
		e->n = 0;
	}

	if (e->n >= FPS_ENV_MAX)
	{
		/* кольцо полно: вытесняем самую старую выборку */
		memmove (&e->s [0], &e->s [1], sizeof (e->s [0]) * (FPS_ENV_MAX - 1));
		e->n = FPS_ENV_MAX - 1;
	}

	e->s [e->n].t_us = t_us;
	e->s [e->n].frames = frames;
	e->s [e->n].unc_us = unc_us;
	e->n++;

	return (e->n > 1) && (frames > e->s [e->n - 2].frames);
}

bool fps_env_ready (const fps_env_t *e, unsigned long long horizon_us)
{
	if (e->n < 2)
		return false;

	return (e->s [e->n - 1].t_us - e->s [0].t_us) >= horizon_us;
}

/* Все выборки между i и j должны строго расти по счётчику. */
static bool window_clean (const fps_env_t *e, int i, int j)
{
	for (int k = i; k < j; k++)
		if (e->s [k + 1].frames <= e->s [k].frames)
			return false;

	return true;
}

int fps_env_decide (const fps_env_t *e, unsigned long long horizon_us, unsigned long long skip_us,
                    unsigned long long sub_us, double factor, double offset, int *hz_out)
{
	if (hz_out)
		*hz_out = 0;

	if ((e->n < 2) || (sub_us == 0))
		return 0;

	if (skip_us >= horizon_us)
		skip_us = 0;	/* опечатка в конфиге не должна гасить замер */

	unsigned long long t0 = e->s [0].t_us;
	unsigned long long best_us = 0;		/* выбранное окно: мкс на кадр */
	unsigned long long best_delta = 0, best_dt = 0, best_unc = 0;
	bool have = false;

	for (int i = 0; (i + 1) < e->n; i++)
	{
		unsigned long long ti = e->s [i].t_us;

		if ((ti - t0) < skip_us)
			continue;		/* прогрев */

		int j = -1;
		for (int k = i + 1; k < e->n; k++)
		{
			if ((e->s [k].t_us - ti) < sub_us)
				continue;
			if (!window_clean (e, i, k))
				break;		/* внутри окна пауза — шире брать нельзя */
			j = k;
			break;
		}

		if (j < 0)
			continue;

		if ((e->s [j].t_us - t0) > horizon_us)
			break;			/* горизонт исчерпан */

		unsigned long long delta = e->s [j].frames - e->s [i].frames;
		unsigned long long dt = e->s [j].t_us - e->s [i].t_us;
		unsigned long long rate_us = dt / delta;	/* меньше — быстрее */

		if (!have || (rate_us > best_us))
		{
			have = true;
			best_us = rate_us;
			best_delta = delta;
			best_dt = dt;
			best_unc = (e->s [i].unc_us + e->s [j].unc_us) / 2;
		}
	}

	if (!have)
		return 0;

	return fps_classify_window_corrected (best_delta, best_dt, best_unc, factor, offset, hz_out);
}
```

**Замечание:** окно сравнивается по целым мкс-на-кадр; разница в 1 мкс/кадр при 25 fps — это
0.005 fps, много меньше точности окна (±1 кадр). При равных `rate_us` остаётся первое окно.

- [ ] **Step 6: Подключить к сборке и тестам**

`tests/run_tests.sh` — после `run test_fps_class fps_class.c`:

```sh
run test_fps_env fps_env.c fps_class.c
```

`GNUmakefile` — в `AFRD_SRC` добавить `fps_env.c` рядом с `fps_class.c`.

- [ ] **Step 7: Прогнать тесты**

Run: `sh tests/run_tests.sh`
Expected: `fps_env: OK` и остальные наборы зелёные.

- [ ] **Step 8: Коммит**

```bash
git add fps_env.h fps_env.c tests/test_fps_env.c tests/fixtures/fps_env_*.txt tests/run_tests.sh GNUmakefile
git commit -m "feat(fps): нижняя огибающая частоты по подокнам (fps_env)"
```

**Замечание по сборке устройства:** `fps_env.c` нужен и в `jni/Android.mk`
(`LOCAL_SRC_FILES`, рядом с `fps_class.c`) — иначе NDK-сборка не слинкуется.

---

### Task 3: `run_atv14()` — наблюдение вместо окна, ключи, «громкие» устаревшие ключи

**Files:**
- Modify: `afrd.c` (функция `run_atv14()`, чтение конфига)
- Modify: `cfgkeys.h`, `cfgkeys.c`, `tests/test_cfgkeys.c`
- Modify: `config/afrd2.ini`

**Interfaces:**
- Consumes: `fps_env_reset/push/ready/decide` (Task 2), `fps_correct_av1_offset/set_av1` (Task 1)
- Produces: новые ключи конфига (таблица ниже), строка лога решения по огибающей

**Новые ключи (все — CFGKEY_ATV14):**

| ключ | профиль | смысл |
|---|---|---|
| `fps.measure.envelope.ms` | 20000 | горизонт наблюдения; 0 — решение по первому подокну |
| `fps.measure.envelope.max.ms` | 30000 | предел расширения, если окна неоднозначны |
| `fps.measure.envelope.sub.ms` | 5000 | длина подокна |
| `fps.measure.envelope.skip.ms` | 2000 | прогрев с начала наблюдения |
| `fps.correct.av1.offset` | 0 | аддитивная поправка AV1, кадр/с (0 — выключена) |
| `fps.correct.av1.pkg` | (пусто) | ограничение поправки по пакету плеера |

**Устаревшие ключи** (`fps.measure.window.ms`, `fps.measure.window.max.ms`): `paths = 0` +
примечание «устарел: заменён fps.measure.envelope.ms (Android 10-14)».
`fps.measure.retries` остаётся.

- [ ] **Step 1: Тест на примечания `cfgkeys` (падающий)**

В `tests/test_cfgkeys.c` добавить в подходящую функцию:

```c
	/* новые ключи огибающей действуют на новом пути */
	assert (cfgkeys_note ("fps.measure.envelope.ms", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("fps.measure.envelope.max.ms", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("fps.measure.envelope.sub.ms", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("fps.measure.envelope.skip.ms", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("fps.correct.av1.offset", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("fps.correct.av1.pkg", CFGKEY_ATV14) == NULL);

	/* устаревшие ключи названы устаревшими, а не «не действуют» */
	assert (cfgkeys_note ("fps.measure.window.ms", CFGKEY_ATV14) != NULL);
	assert (strstr (cfgkeys_note ("fps.measure.window.ms", CFGKEY_ATV14), "устарел") != NULL);
	assert (strstr (cfgkeys_note ("fps.measure.window.max.ms", CFGKEY_ATV14), "устарел") != NULL);
```

Run: `sh tests/run_tests.sh`
Expected: FAIL — `fps.measure.envelope.ms` опознаётся как «неизвестный ключ».

- [ ] **Step 2: Реализовать таблицу `cfgkeys`**

В `cfgkeys.c` у структуры `k_keys` появляется третье поле:

```c
static const struct
{
	const char *key;
	unsigned paths;
	const char *note;	/* примечание вместо стандартной формулировки (NULL — обычная) */
} k_keys [] =
{
```

Записи (в разделе «только новый путь»):

```c
	{ "fps.measure.envelope.ms",	CFGKEY_ATV14, NULL },
	{ "fps.measure.envelope.max.ms", CFGKEY_ATV14, NULL },
	{ "fps.measure.envelope.sub.ms", CFGKEY_ATV14, NULL },
	{ "fps.measure.envelope.skip.ms", CFGKEY_ATV14, NULL },
	{ "fps.correct.av1.offset",	CFGKEY_ATV14, NULL },
	{ "fps.correct.av1.pkg",	CFGKEY_ATV14, NULL },
```

и вместо двух прежних записей окна замера:

```c
	/* заменены огибающей (наблюдение 20 с): ключ известен, но на новом пути
	 * не действует — старый конфиг получает внятное сообщение, а не
	 * «неизвестный ключ (опечатка?)» */
	{ "fps.measure.window.ms",	0, "устарел: заменён fps.measure.envelope.ms (Android 10-14)" },
	{ "fps.measure.window.max.ms",	0, "устарел: заменён fps.measure.envelope.ms (Android 10-14)" },
```

Все существующие строки таблицы дополнить третьим полем `NULL`.

В `cfgkeys_note()` порядок проверок: `paths & path` → NULL; затем `note` → вернуть его; затем
`CFGKEY_APP`; затем стандартные формулировки:

```c
			if (k_keys [i].paths & path)
				return NULL;

			if (k_keys [i].note)
				return k_keys [i].note;

			if (k_keys [i].paths & CFGKEY_APP)
				return "ключ настройки приложения — демон его не читает";
```

Run: `sh tests/run_tests.sh`
Expected: все наборы зелёные.

- [ ] **Step 3: Профиль `config/afrd2.ini`**

Удалить `fps.measure.window.ms` и `fps.measure.window.max.ms`, вместо них:

```ini
# Наблюдение перед решением: счётчик кадров завышен односторонне (плеер
# повторно декодирует кадры на границах DASH/HLS-сегментов, AV1-тракт
# добавляет свои), поэтому частота берётся по НИЖНЕЙ ОГИБАЮЩЕЙ подокон:
# минимум из частот подокон длиной sub.ms. Живой замер 01.10 (13 роликов
# YouTube): решение по одному окну верно для 24-фпс роликов в 14 % случаев,
# огибающая за 20 с — в 86 %, для 25-фпс — 100 %; ошибки только в сторону
# 25 Гц (пропуск кадра заметнее повтора).
#
# Цена: режим переключается не раньше чем через skip.ms + envelope.ms после
# старта видео (было 1.5-6 с). Видео короче этого — просто не переключается.
fps.measure.envelope.ms=20000
# Предел наблюдения, если окна неоднозначны (доверительный интервал ±1 кадр
# не укладывается в одну группу частот). Меньше envelope.ms — не бывает.
fps.measure.envelope.max.ms=30000
# Длина подокна. 5 с — компромисс: за 5 с ±1 кадр даёт ±0.2 fps (для 24 fps).
fps.measure.envelope.sub.ms=5000
# Прогрев: подокна, начинающиеся раньше, не рассматриваются. Набор буфера
# у некоторых плееров занижает первые секунды (60-фпс канал через VLC давал
# по первому окну 50.3 fps), а минимум чувствителен к провалу.
fps.measure.envelope.skip.ms=2000
```

и в блок калибровки (после `fps.correct.list=`):

```ini
# Поправка на завышение счётчика AV1-трактом декодера (аддитивная, кадр/с).
# Измерено 01.10 в стороннем плеере (Just Player, один и тот же файл Sintel
# 24.000 fps): H.264 — 24.002, AV1 — 24.520, то есть +0.5 кадра/с; штатный
# клиент YouTube добавляет к этому своё. ВЫКЛЮЧЕНА по умолчанию: огибающая
# (fps.measure.envelope.*) уже снимает большую часть завышения, а поправка
# «переусиливает» честный 25-фпс контент (проверено: 25-фпс ролики уходили
# в 24 Гц). Включать, только если у конкретного плеера смещение больше
# ~1 кадра/с и огибающей не хватает.
fps.correct.av1.offset=0
# Ограничить поправку одним плеером (пусто — любой). Пакет виден в логе
# строкой «плеер <пакет>: поправка замера ...».
fps.correct.av1.pkg=
```

- [ ] **Step 4: `afrd.c` — состояние и чтение ключей**

В объявления `run_atv14()` (рядом с `bool pkg_probed = false;`) добавить:

```c
	/* Наблюдение частоты: кольцо выборок счётчика (см. fps_env.h). Решение
	 * принимается по нижней огибающей, а не по одному окну. */
	fps_env_t env;
	fps_env_reset (&env);
```

После чтения `recheck_ms` добавить ключи огибающей:

```c
	/* Огибающая: горизонт наблюдения, предел расширения, подокно, прогрев
	 * (см. fps_env.h и config/afrd2.ini). envelope.ms=0 — решение по первому
	 * подокну (быстрое переключение, как было до 01.10). */
	int env_ms = cfg_get_int ("fps.measure.envelope.ms", 20000);
	if (env_ms < 0) env_ms = 0;
	int env_max_ms = cfg_get_int ("fps.measure.envelope.max.ms", 30000);
	if (env_max_ms < env_ms) env_max_ms = env_ms;
	int env_sub_ms = cfg_get_int ("fps.measure.envelope.sub.ms", 5000);
	if (env_sub_ms < 200) env_sub_ms = 200;
	int env_skip_ms = cfg_get_int ("fps.measure.envelope.skip.ms", 2000);
	if (env_skip_ms < 0) env_skip_ms = 0;

	unsigned long long env_us = (unsigned long long)env_ms * 1000;
	unsigned long long env_max_us = (unsigned long long)env_max_ms * 1000;
	unsigned long long env_sub_us = (unsigned long long)env_sub_ms * 1000;
	unsigned long long env_skip_us = (unsigned long long)env_skip_ms * 1000;
	/* горизонт короче подокна не даёт ни одного окна — приравниваем к подокну */
	if (env_us && (env_us < env_sub_us)) env_us = env_sub_us;
	if (env_max_us < env_sub_us) env_max_us = env_sub_us;
```

В блок чтения поправки плеера (`if (correct_enable)`) добавить:

```c
		const char *av1_val = cfg_get_str ("fps.correct.av1.offset", NULL);
		const char *av1_pkg = cfg_get_str ("fps.correct.av1.pkg", NULL);
		double av1_off_cfg = av1_val ? atof (av1_val) : 0.0;
		fps_correct_set_av1 (av1_off_cfg, av1_pkg);
		if (av1_off_cfg > 0.0)
		{
			int p = (int)(av1_off_cfg * 100.0 + 0.5);
			trace (1, "поправка AV1: минус %d.%02d кадра/с%s%s\n", p / 100, p % 100,
			       (av1_pkg && *av1_pkg) ? ", только для " : "",
			       (av1_pkg && *av1_pkg) ? av1_pkg : "");
		}
```

(в ветке `else fps_correct_clear ();` поправка AV1 сбрасывается самим `fps_correct_clear`).

- [ ] **Step 5: `afrd.c` — метка времени чтения в основном цикле**

Заменить в цикле

```c
		if (vdec_poll (plat->vdec_status, &v0) != 0)
		{
```

на

```c
		/* Метка времени выборки — середина чтения (само чтение под нагрузкой
		 * занимает миллисекунды, а окна считаются по крайним выборкам) и его
		 * длительность как неопределённость интервала. */
		unsigned long long read_a = now_us ();
		int poll_rc = vdec_poll (plat->vdec_status, &v0);
		unsigned long long read_b = now_us ();
		unsigned long long sample_t = read_a + (read_b - read_a) / 2;
		unsigned long long sample_unc = read_b - read_a;

		if (poll_rc != 0)
		{
```

Тело прежнего `if` не меняется (`vdec_fails++` / страховка / `continue`).

- [ ] **Step 6: `afrd.c` — сброс наблюдения там, где сбрасывается состояние видео**

* в ветке сброса счётчика («счётчик кадров сброшен — новое видео, мерю заново») добавить `fps_env_reset (&env);`
* в ветке `if (!video_eff)` (пауза при `pause.keep=0` / конец видео) добавить `fps_env_reset (&env);`

- [ ] **Step 7: `afrd.c` — решение по огибающей вместо блокирующего окна**

В блоке `else if (!measure_failed && (want_sf_id < 0))` заменить всё, что находится в ветке
`else` после проверки подсказки плеера (то есть блок с `unsigned long long read_a = now_us ();`,
циклом `while (grown) { ... }` и последующей обработкой `if (!hz)`) на:

```c
			else if (v0.active)
			{
				/* Наблюдение (fps_env): счётчик кадров на ATV14 завышен
				 * односторонне, поэтому решение берётся по нижней огибающей
				 * подокон. Пока горизонт не набран — режим не трогаем. */
				if (!fps_env_push (&env, sample_t, v0.frames, sample_unc))
					trace (1, "счётчик кадров не растёт — наблюдение начато заново\n");

				if (!fps_env_ready (&env, env_us))
					continue;

				double av1_off = correct_enable
				                 ? fps_correct_av1_offset (v0.device, g_cur_pkg) : 0.0;

				int env_hz = 0;
				if (!fps_env_decide (&env, env_us, env_skip_us, env_sub_us,
				                     video_corr, av1_off, &env_hz))
				{
					/* окна неоднозначны: наблюдаем дальше, но не дольше предела */
					if (!fps_env_ready (&env, env_max_us))
						continue;

					trace (1, "огибающая не дала решения за %d с наблюдения\n",
					       (int)(env_max_us / 1000000));
					if (++retries >= max_retries)
					{
						trace (1, "частоту определить не удалось (%d попыток), пропускаю\n", retries);
						measure_failed = true;
						want_sf_id = -1;
						if (!g_sf_table_changed)
							atv14_give_up (plat, &orig_sf_id, &cur_sf_id, &held);
					}

					fps_env_reset (&env);
					continue;
				}

				hz = env_hz;

				if (av1_off > 0.0)
					trace (1, "решение уточнено поправкой AV1: минус %.2f кадра/с\n", av1_off);

				/* заявленная длительность кадра (frame dur) уточняет решение —
				 * как и раньше, но теперь уточняется решение огибающей */
				if (v0.decl_dur)
				{
					int refined = 0;
					if (fps_refine_declared (hz, 96000.0 / (double)v0.decl_dur,
					                         (double)declared_pct, &refined))
					{
						if (refined != hz)
							trace (1, "частота уточнена по frame dur (%u): "
							       HZ_FMT " Гц вместо " HZ_FMT " Гц\n",
							       v0.decl_dur, HZ_ARGS (refined), HZ_ARGS (hz));

						hz = refined;
					}
				}

				trace (1, "видео: " HZ_FMT " Гц по огибающей (%d с наблюдения), ищу режим\n",
				       HZ_ARGS (hz),
				       (int)((env.s [env.n - 1].t_us - env.s [0].t_us) / 1000000));

				/* следующее измерение (перепроверка, новое видео) — заново */
				fps_env_reset (&env);
			}
			else
				continue;
```

Удалить ставшие ненужными чтения конфига `window_ms` / `window_max_ms` и локальные переменные
`slice_ms`, `window_us`, `window_max_us`, `grown`, `stalled`, `decided_ms`, `t1`, `dt_us`,
`dt_unc_us`, `frames_delta`, `fps`, `read0_us` — они относились к удалённому блоку.
`fps.measure.retries` (переменная `max_retries`) остаётся.

**Проверить, что не осталось ссылок** на `window_ms` / `window_max_ms` / `fps.measure.window`:
`grep -n "window_ms\|fps.measure.window" afrd.c` — пусто (кроме, возможно, комментариев, которые
тоже надо убрать).

- [ ] **Step 8: Сборка и тесты**

Run: `sh tests/run_tests.sh && make MODE=debug`
Expected: тесты зелёные, сборка без предупреждений.

Run: `rm -rf obj libs && ./mka`
Expected: обе ABI собраны, предупреждений нет.

- [ ] **Step 9: Коммит**

```bash
git add afrd.c cfgkeys.c cfgkeys.h tests/test_cfgkeys.c config/afrd2.ini
git commit -m "feat(atv14): решение по нижней огибающей (fps.measure.envelope.*) вместо окна замера; ключи fps.correct.av1.*"
```

---

### Task 4: GUI приложения, README/ChangeLog, скрипты стенда

**Files:**
- Modify: `android/app/src/main/res/xml/preferences.xml`, `res/values/strings.xml`,
  `android/app/src/main/java/ru/cobra/zap/afrd/Config.java`
- Modify: `README.md`, `ChangeLog-ru.txt`
- Modify: `tools/stand/acceptance_matrix.sh`, `tools/stand/verify_latency.sh`,
  `tools/stand/verify_pause_early.sh`, `tools/stand/verify_fix.sh`

**Interfaces:**
- Consumes: имена ключей из Task 3 (таблица «Новые ключи»)
- Produces: рабочий GUI-экран настроек, документация, приёмочные скрипты под новую латентность

- [ ] **Step 1: `Config.java` — типы новых ключей**

В статический блок `itemType` (раздел «Новый путь (Android 10-14…)») добавить:

```java
        itemType.put ("fps.measure.envelope.ms", TYPE_INT);
        itemType.put ("fps.measure.envelope.max.ms", TYPE_INT);
        itemType.put ("fps.measure.envelope.sub.ms", TYPE_INT);
        itemType.put ("fps.measure.envelope.skip.ms", TYPE_INT);
        itemType.put ("fps.correct.av1.offset", TYPE_STR);
        itemType.put ("fps.correct.av1.pkg", TYPE_STR);
```

и **удалить** строки `fps.measure.window.ms` / `fps.measure.window.max.ms` (ключи устарели —
приложение не должно писать их в ini).

- [ ] **Step 2: `preferences.xml` — пункты настроек**

Удалить пункты `fps_measure_window_ms` и `fps_measure_window_max_ms`. Добавить (в тот же раздел,
что `fps_recheck_delay_ms` / `fps_correct_*`), по образцу существующих
`ru.cobra.zap.xprefs.XEditTextPreference`:

```xml
        <ru.cobra.zap.xprefs.XEditTextPreference android:defaultValue="20000" android:key="fps_measure_envelope_ms"
                          android:singleLine="true" android:summary="@string/prefsum_fps_measure_envelope"
                          android:title="@string/prefs_fps_measure_envelope"/>
        <ru.cobra.zap.xprefs.XEditTextPreference android:defaultValue="30000" android:key="fps_measure_envelope_max_ms"
                          android:singleLine="true" android:summary="@string/prefsum_fps_measure_envelope_max"
                          android:title="@string/prefs_fps_measure_envelope_max"/>
        <ru.cobra.zap.xprefs.XEditTextPreference android:defaultValue="5000" android:key="fps_measure_envelope_sub_ms"
                          android:singleLine="true" android:summary="@string/prefsum_fps_measure_envelope_sub"
                          android:title="@string/prefs_fps_measure_envelope_sub"/>
        <ru.cobra.zap.xprefs.XEditTextPreference android:defaultValue="2000" android:key="fps_measure_envelope_skip_ms"
                          android:singleLine="true" android:summary="@string/prefsum_fps_measure_envelope_skip"
                          android:title="@string/prefs_fps_measure_envelope_skip"/>
        <ru.cobra.zap.xprefs.XEditTextPreference android:key="fps_correct_av1_offset"
                          android:singleLine="true" android:summary="@string/prefsum_fps_correct_av1_offset"
                          android:title="@string/prefs_fps_correct_av1_offset"/>
        <ru.cobra.zap.xprefs.XEditTextPreference android:key="fps_correct_av1_pkg"
                          android:singleLine="true" android:summary="@string/prefsum_fps_correct_av1_pkg"
                          android:title="@string/prefs_fps_correct_av1_pkg"/>
```

- [ ] **Step 3: `strings.xml` — подписи**

Добавить строки `prefs_fps_measure_envelope` / `prefsum_fps_measure_envelope` и т.д. для всех
шести пунктов (удалить строки старых `fps_measure_window*`, если они больше нигде не используются —
проверить `grep -rn fps_measure_window android/`). Смысл подписей:

* «Наблюдение перед переключением, мс» — «Демон не переключает режим, пока не наберёт столько
  миллисекунд наблюдения. Частота берётся как минимум из частот подокон (5 с), поэтому короткие
  всплески и повторные кадры на границах сегментов не сбивают решение. 20000 — измеренный
  оптимум: 24-фпс ролики YouTube определяются верно в 86 % случаев; 0 — переключать сразу по
  первому подокну.» (и аналогично кратко для max/sub/skip);
* «Поправка AV1 (кадр/с)» — «Аддитивная поправка на завышение счётчика AV1-трактом декодера.
  Измерено: AV1 отдаёт +0.5 кадра/с против H.264 на том же файле. По умолчанию 0 (выключено):
  наблюдение по огибающей уже снимает большую часть завышения. Для Android 10-14.»;
* «Поправка AV1: только для пакета» — «Пусто — для любого плеера.»

- [ ] **Step 4: Сборка APK**

Run: `cd android && ./gradlew --offline assembleDebug`
Expected: `BUILD SUCCESSFUL` (при необходимости `mka` обновляет профиль в `res/raw`).

- [ ] **Step 5: Скрипты стенда — латентность**

`tools/stand/acceptance_matrix.sh`:

* `HOLD_WAIT=${HOLD_WAIT:-8}` → `HOLD_WAIT=${HOLD_WAIT:-26}`
  (в шапке файла: «паузы в секундах (8 / 5)» → «(26 / 5)»; там же пояснить, почему: решение по
  огибающей требует `fps.measure.envelope.ms` + `skip.ms`).
* комментарий в шапке про `HOLD_WAIT` дополнить: «должен быть больше
  `fps.measure.envelope.skip.ms + fps.measure.envelope.ms`».

`tools/stand/verify_latency.sh`:

* цикл опроса `while [ "$i" -lt 30 ]` (15 с) → `-lt 120` (60 с), чтобы увидеть переключение
  через ~22 с.

`tools/stand/verify_pause_early.sh` и `tools/stand/verify_fix.sh`:

* увеличить паузы ожидания переключения до 26–30 с; в комментариях указать новую причину задержки
  (наблюдение по огибающей), а не «дебаунс старта».

- [ ] **Step 6: README**

* В таблице «Профиль config/afrd2.ini» (применимость ключей) заменить строки `fps.measure.window.ms`,
  `fps.measure.window.max.ms` на `fps.measure.envelope.*`; добавить `fps.correct.av1.offset`,
  `fps.correct.av1.pkg`; отметить устаревшие ключи как недействующие (аудит их называет).
* Раздел про fps: описать нижнюю огибающую, её цену (задержка первого переключения
  `skip.ms + envelope.ms`), измеренную точность (86 %/100 % на 13 роликах; ошибки — только в
  сторону 25 Гц) и когда включать `fps.correct.av1.offset`.
* «Статус приёмки»: заменить ожидания `1080p25hz`-латентности; добавить новый прогон (Task 4 →
  живая приёмка контроллера), пометить непроверенное.
* Убрать/поправить упоминания `fps.measure.window.ms` в разделе о перепроверке.

- [ ] **Step 7: ChangeLog-ru**

Добавить запись о новом поведении ATV14-пути: решение по нижней огибающей (наблюдение
`fps.measure.envelope.ms`, задержка первого переключения), новые ключи, устаревшие ключи окна,
поправка `fps.correct.av1.*` (выключена по умолчанию), измеренные цифры.

- [ ] **Step 8: Проверка «громких ключей» на профиле**

Run: `sh tests/run_tests.sh` (набор `test_cfgkeys` проверяет и наши профили на чистоте их пути).
Expected: зелёные.

- [ ] **Step 9: Коммит**

```bash
git add android README.md ChangeLog-ru.txt tools/stand
git commit -m "feat(app): настройки огибающей и поправки AV1; README/ChangeLog; скрипты стенда под новую латентность"
```

---

## Живая приёмка (выполняет контроллер после Task 3–4)

1. Собрать и запушить стенд: `sh tools/stand/build_push.sh`.
2. `sh tools/stand/verify_latency.sh` — режим должен появиться примерно через `skip.ms + envelope.ms`
   (≈22 с) после старта клипа; в логе — строка «видео: … по огибающей».
3. `PAUSE_TEST=1 API_TEST=1 OWN_AFR_TEST=1 sh tools/stand/acceptance_matrix.sh` — ожидания
   `1080p24hz` / `1080p50hz` / `1080p50hz` (профиль `mode.prefer.exact=0`), возврат в `1080p60hz`.
4. YouTube (штатный клиент, AV1), 24-фпс ролик: без поправки — один переход в 25/50 Гц без
   «перескока» (наблюдение ≥60 с); с `fps.correct.av1.offset=0.5` и `fps.correct.enable=1` —
   `1080p24hz`; 25-фпс ролик с поправкой — `1080p50hz` (поправка не должна ломать честный 25).
5. Смена ролика/пауза — наблюдение начинается заново, режим не «залипает».

## Self-review плана

* Спека покрыта: огибающая (Task 2), ключи и латентность (Task 3), поправка AV1 (Task 1, 3),
  GUI/доки/скрипты (Task 4), приёмка (раздел выше).
* Review Focus: пауза и сброс счётчика — `t_pause`/`t_counter_reset` (Task 2) и сбросы в Task 3,
  шаг 6; короткий клип — отсутствие решения при `!fps_env_ready` (Task 2 `t_ready`, Task 3 шаг 7);
  стартовый провал — `t_startup_dip` + `skip.ms`; устаревшие ключи — Task 3 шаг 1–2.
* Имена согласованы: `fps_env_t`, `fps_env_push/ready/decide/reset`,
  `fps_classify_window_corrected`, `fps_correct_av1_offset/set_av1`, ключи конфига — как в Task 3.

