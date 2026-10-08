# AFR на Android 10/11 (вендорный SystemControl) + быстрый путь по заявленной частоте

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **Статус: план выполнен (2026-10-08).** Все задачи 1–9 закрыты; запись о выполнении и решения
> по ходу — `progress.md` рядом со спекой (плюс коммиты `bf23bc5`…`67b25f4`). Живая приёмка на
> стенде `.45` (Android 11) — `OK=24 FAIL=0` (`tools/stand/acceptance_atv11.sh`), находки
> независимого ревью и прогона — журнал §33.20–§33.21. Открыто только то, что требует стендов с
> ТВ: регрессия Android 12–14 и старый путь с включённым системным AFR (задачи 1–8 самих шагов
> закрыты, см. примечания в Task 9).

**Goal:** Научить AFRd2 применять режим на Android 10/11 вендорным HIDL-механизмом Amlogic и решать о режиме за 2–4 с по заявленной частоте, подтверждая замером.

**Architecture:** Один цикл решения (`run_atv14`). Различия платформ — в двух точках: выбор целевого режима (`mode_target_t`: id таблицы SurfaceFlinger для ATV14, имя из `disp_cap` для ATV11) и применение (`sink`: транзакция 1035, запись в sysfs или вендорный помощник). Быстрый путь — отдельная ветка под ключом `fps.declare.fast` (1 для ATV11, 0 для ATV14): решение по заявленной частоте сразу, замер продолжает работать и правит режим при расхождении.

**Tech Stack:** C (host-тесты через `cc`, `tests/run_tests.sh`), NDK-сборка `./mka`, Android-приложение (Java/XML, gradle), `app_process` + dex для вендорного HIDL.

**Spec:** `docs/superpowers/specs/2026-10-08-afrd2-android10-11-vendor-afr-design.md`

## Global Constraints

- Версия продукта — `1.0.0`; версия живёт в `main.c` (`g_version`) и синхронизируется `./mka`; расхождение ловит `tests/test_versions.sh`.
- Пути и значения по умолчанию — из спеки, дословно: `vendor.disp.cli` = `/data/local/tmp/afrd-disp.sh`, `vendor.disp.cap` = `/sys/class/amhdmitx/amhdmitx0/disp_cap`, `fps.declare.fast` = 1 для ATV11 и 0 для ATV14, `fps.declared.crosscheck.pct` = 5 (существующее).
- Список ключей применимости — единственный источник правды `cfgkeys.c`; новый ключ без записи там считается аудитом опечаткой (`tests/test_cfgkeys.c`).
- Строки GUI: и `values/strings.xml`, и `values-ru/strings.xml` — иначе `tests/test_inactive.c`/сборка расходятся с ЧАВО.
- Все host-тесты обязаны быть зелёными (`sh tests/run_tests.sh`); сборка `./mka` — без предупреждений.
- Стенд `.45` (Android 11) можно не возвращать в исходное состояние — он для отладки.
- ATV14 по умолчанию не меняет поведения: быстрый путь выключен, выбор режима по id таблицы SF сохраняется.

## Review Focus

- Вендорный помощник недоступен или вернул ошибку → демон **не трогает** режим и пишет предупреждение (тот же пассивный режим, что сегодня); ни одного падения или зависания.
- Заявленная частота — мусор (0, 255, 1000) → быстрый путь пропускается, решение принимает замер.
- Заявленная частота врёт (наш синтетический клип: 30 при 23.976) → быстрый путь даёт ложный режим, замер обязан его исправить, обе строки видны в журнале.
- ATV14 при `fps.declare.fast=0` идёт прежним кодом (ветка не исполняется).
- Применение одинакового режима повторно не вызывает sink (нет лишних миганий).

## Файловая структура

| Файл | Ответственность |
|---|---|
| `vdec.h`, `vdec.c` | разбор заявленной частоты (`frame rate`) рядом с `frame dur` |
| `fps_class.h`, `fps_class.c` | класс/группа частот (уже есть), новый помощник «годится ли заявленная» |
| `sink.h`, `sink.c` | три способа применить режим, включая вендорный помощник |
| `modes.c`, `modes.h` | (новый) чистый выбор режима по частоте из списка имён + разбор `disp_cap` |
| `platform.h`, `platform.c` | определение пути, включая `PLAT_ATV11` |
| `afrd.c` | цикл: `mode_target_t`, ATV11-ветка, быстрый путь |
| `cfg.c`, `afrd.h` | ключи и умолчания |
| `cfgkeys.c` | применимость новых ключей |
| `tools/droidlogic/` | помощник (есть), упаковка |
| `android/...` | строка настройки, извлечение помощника из APK |
| `tests/` | `test_vdec`, `test_sink`, `test_modes`, новый `test_declare` |

---

### Task 1: Заявленная частота кадра в `vdec_status`

**Files:**
- Modify: `vdec.h` (структуры `vdec_status_t` и `vdec_chan_t`)
- Modify: `vdec.c` (разбор в `vdec_parse_channels`, отдача в `vdec_status`)
- Test: `tests/test_vdec.c`

**Interfaces:**
- Produces: поле `unsigned decl_fps;` в `vdec_status_t` и `vdec_chan_t` — заявленная частота кадра (целые fps), `0` если поля нет.

- [x] **Step 1: Тест, который падает**

Добавить в `tests/test_vdec.c` (рядом с проверками `frame dur`):

```c
/* Заявленная частота кадра: строка "frame rate : 24 fps" (поле драйвера).
 * Нужна быстрому пути решения; 0 — поля нет или значение не разобралось. */
static void test_declared_fps (void)
{
	static const char text [] =
		"vdec channel 0 statistics:\n"
		"  device name : ammvdec_h264_v4\n"
		"  frame width : 1920\n"
		"  frame height : 1080\n"
		"  frame rate : 24 fps\n"
		"  frame count : 42\n";

	vdec_chan_t ch [VDEC_MAX_CHANNELS];
	int n = vdec_parse_channels (text, ch, VDEC_MAX_CHANNELS);
	assert (n == 1);
	assert (ch [0].decl_fps == 24);

	/* Поля нет — ноль, а не мусор */
	static const char nofps [] =
		"vdec channel 0 statistics:\n"
		"  device name : ammvdec_h264_v4\n"
		"  frame count : 7\n";
	n = vdec_parse_channels (nofps, ch, VDEC_MAX_CHANNELS);
	assert (n == 1);
	assert (ch [0].decl_fps == 0);

	/* Мусор в поле — ноль */
	static const char junk [] =
		"vdec channel 0 statistics:\n"
		"  device name : ammvdec_h264_v4\n"
		"  frame rate : n/a\n"
		"  frame count : 7\n";
	n = vdec_parse_channels (junk, ch, VDEC_MAX_CHANNELS);
	assert (n == 1);
	assert (ch [0].decl_fps == 0);
}
```

и вызвать `test_declared_fps ();` в `main()`.

- [x] **Step 2: Убедиться, что падает**

Run: `sh tests/run_tests.sh 2>&1 | grep -A3 vdec`
Expected: ошибка компиляции `decl_fps` не объявлен либо assert-падение.

- [x] **Step 3: Реализация**

`vdec.h`, в `vdec_status_t` после `decl_dur`:

```c
	unsigned decl_fps;		/* frame rate: заявленная частота кадра (целые fps),
					 * 0 — поля нет. Источник быстрого решения: системный
					 * AFR Amlogic берёт именно это значение (§33.18–33.19
					 * журнала), поэтому оно разбирается и нами. */
```

и такое же поле в `vdec_chan_t`.

`vdec.c`, в `vdec_parse_channels` — сразу после блока `frame dur` (внутри `if (fc …)`):

```c
			/* frame rate — заявленная частота кадра: системный AFR Amlogic
			 * берёт её из этого же поля, поэтому читаем и мы (быстрый путь).
			 * Мусор ("n/a") оставляет ноль — решает замер. */
			const char *fr = strstr (p, "frame rate");
			if (fr && fr < end && (fr = strchr (fr, ':')))
			{
				unsigned f = 0;
				if (parse_uint (fr + 1, end, &f))
					ch.decl_fps = f;
			}
```

и в `vdec_status` (функция отдачи выбранного канала, рядом с `out->decl_dur = …`):

```c
	out->decl_fps = chans [idx].decl_fps;
```

- [x] **Step 4: Зелёные тесты**

Run: `sh tests/run_tests.sh 2>&1 | grep -E 'vdec|FAIL'`
Expected: `vdec: OK`, строк `FAIL` нет.

- [x] **Step 5: Коммит**

```bash
git add vdec.h vdec.c tests/test_vdec.c
git commit -m "feat(vdec): разбор заявленной частоты кадра (frame rate) в vdec_status"
```

---

### Task 2: Вендорный sink (применение режима по имени)

**Files:**
- Modify: `sink.h`, `sink.c`
- Test: `tests/test_sink.c`

**Interfaces:**
- Consumes: ничего (только `exec_capture` из `exec.h`).
- Produces: `SINK_VENDOR_MODE` в `sink_kind_t`; `sink_build_vendor_set (char *buf, size_t n, const char *cli, const char *mode_name)` — сборка команды; `sink_apply` для вендорного вида выполняет её; `sink_release_backdoor (SINK_VENDOR_MODE)` — no-op (возврат 0).

- [x] **Step 1: Тест, который падает**

Добавить в `tests/test_sink.c`:

```c
/* Вендорный путь (Android 10/11): режим применяется помощником по ИМЕНИ.
 * Команда — "sh <cli> <имя>"; пустое имя или путь — отказ. */
static void test_vendor_build (void)
{
	char buf [256];

	assert (sink_build_vendor_set (buf, sizeof (buf), "/data/local/tmp/afrd-disp.sh", "1080p24hz") == 0);
	assert (strcmp (buf, "sh /data/local/tmp/afrd-disp.sh 1080p24hz") == 0);

	/* нет имени — отказ (применять нечего) */
	assert (sink_build_vendor_set (buf, sizeof (buf), "/data/local/tmp/afrd-disp.sh", NULL) == -1);
	assert (sink_build_vendor_set (buf, sizeof (buf), "/data/local/tmp/afrd-disp.sh", "") == -1);

	/* нет пути к помощнику — отказ */
	assert (sink_build_vendor_set (buf, sizeof (buf), NULL, "1080p24hz") == -1);

	/* не влезло — отказ, а не обрезанная команда */
	assert (sink_build_vendor_set (buf, 8, "/data/local/tmp/afrd-disp.sh", "1080p24hz") == -1);

	/* снятие удержания для этого вида — no-op: режим держит вендорный сервис,
	 * «флага» нет, возврат делается обычным применением исходного режима */
	assert (sink_release_backdoor (SINK_VENDOR_MODE) == 0);
}
```

и вызов в `main()`.

- [x] **Step 2: Убедиться, что падает**

Run: `sh tests/run_tests.sh 2>&1 | grep -A3 sink`
Expected: ошибка компиляции (`SINK_VENDOR_MODE` не объявлен).

- [x] **Step 3: Реализация**

`sink.h`: добавить в перечисление и объявление:

```c
	SINK_VENDOR_MODE = 2,	/* вендорный SystemControl Amlogic (Android 10/11):
				 * app_process + dex, метод setActiveDispMode(<имя>) */
```

```c
/* Строка вызова вендорного помощника: "sh <cli> <имя режима>".
 * Пустое имя или пустой путь — отказ (применять нечего). */
int sink_build_vendor_set (char *buf, size_t n, const char *cli, const char *mode_name);
```

`sink.c`:

```c
int sink_build_vendor_set (char *buf, size_t n, const char *cli, const char *mode_name)
{
	if (!cli || !*cli || !mode_name || !*mode_name)
		return -1;

	int r = snprintf (buf, n, "sh %s %s", cli, mode_name);
	return (r < 0 || (size_t)r >= n) ? -1 : 0;
}
```

в `sink_apply` — новая ветка перед `SINK_SF_BACKDOOR`:

```c
	if (kind == SINK_VENDOR_MODE)
	{
		if (!legacy_mode_name || !*legacy_mode_name)
			return -1;

		/* cli берётся из конфига тем же вызовом, что и для цветового
		 * пространства на ATV14 (см. afrd.c: vendor_cli) */
		char cmd [256], out [256];
		if (sink_build_vendor_set (cmd, sizeof (cmd), g_vendor_disp_cli, legacy_mode_name) != 0)
			return -1;

		trace (1, "Переключаю режим (вендорный SystemControl) на %s\n", legacy_mode_name);
		return (exec_capture (cmd, out, sizeof (out)) == 0) ? 0 : -1;
	}
```

и no-op в `sink_release_backdoor` (в начале функции, до проверки `SINK_SF_BACKDOOR`):

```c
	if (kind == SINK_VENDOR_MODE)
		return 0;	/* «флага» нет: ни снимать, ни освобождать нечего */
```

`g_vendor_disp_cli` объявить в `afrd.h` (`extern const char *g_vendor_disp_cli;`) и завести в `afrd.c` (Task 3 заполнит из конфига).

- [x] **Step 4: Зелёные тесты**

Run: `sh tests/run_tests.sh 2>&1 | grep -E 'sink|FAIL'`
Expected: `sink: OK`.

- [x] **Step 5: Коммит**

```bash
git add sink.h sink.c afrd.h afrd.c tests/test_sink.c
git commit -m "feat(sink): вендорный способ применения режима по имени (Android 10/11)"
```

---

### Task 3: Определение платформы `PLAT_ATV11`

**Files:**
- Modify: `platform.h`, `platform.c`, `afrd.h` (умолчания), `cfg.c` (если требуется регистрация)
- Modify: `cfgkeys.c` (новые ключи), `config/afrd2.ini`, `config/afrd-android8+.ini`
- Test: `tests/test_cfgkeys.c` (уже проверяет таблицу на наших профилях)

**Interfaces:**
- Consumes: `sink_kind_t` (Task 2).
- Produces: `platform_kind_t PLAT_ATV11`; поля `platform_t`: `const char *vendor_mode_cli;`, `const char *disp_cap;`; глобал `const char *g_vendor_disp_cli;`.

- [x] **Step 1: Тест (падающий)**

В `tests/test_cfgkeys.c` (тест аудита профилей) новый ключ проверяется автоматически после добавления в профиль — сначала добавить в профили, тест покажет расхождение:

`config/afrd2.ini` (в разделе общих ключей) и `config/afrd-android8+.ini`:

```ini
# Путь к вендорному помощнику смены режима (Android 10/11, Amlogic).
# Пишут приложение и Magisk-модуль, раскладывая помощник в свои каталоги.
vendor.disp.cli=/data/local/tmp/afrd-disp.sh
# Список режимов ТВ для нового пути на Android 10/11 (имена и частоты).
vendor.disp.cap=/sys/class/amhdmitx/amhdmitx0/disp_cap
# Быстрое решение по заявленной частоте кадра (vdec_status: frame rate):
# 1 — применить режим сразу по заявленной частоте и проверить замером,
# 0 — решать только по замеру (как было). На ATV14 по умолчанию 0.
fps.declare.fast=0
```

Run: `sh tests/run_tests.sh 2>&1 | grep -A2 cfgkeys`
Expected: FAIL — «неизвестный ключ: fps.declare.fast».

- [x] **Step 2: Реализация**

`cfgkeys.c` — в таблицу:

```c
	{ "vendor.disp.cli",		CFGKEY_BOTH, NULL },
	{ "vendor.disp.cap",		CFGKEY_BOTH, NULL },
	{ "fps.declare.fast",		CFGKEY_BOTH, NULL },
```

`afrd.h` — умолчания и внешний путь:

```c
#define DEFAULT_VENDOR_DISP_CLI		"/data/local/tmp/afrd-disp.sh"
#define DEFAULT_VENDOR_DISP_CAP		"/sys/class/amhdmitx/amhdmitx0/disp_cap"
#define DEFAULT_FPS_DECLARE_FAST_ATV11	1
#define DEFAULT_FPS_DECLARE_FAST_ATV14	0
extern const char *g_vendor_disp_cli;
```

`platform.h`:

```c
typedef enum
{
	PLAT_LEGACY = 0,	/* ядра 3.14/4.9, Android 6-9 */
	PLAT_ATV14 = 1,		/* ядра 5.x, Android 10-14: поллинг + backdoor SF */
	PLAT_ATV11 = 2,		/* ядро 5.x, Android 10/11 (Amlogic): вендорный SystemControl */
} platform_kind_t;
```

и в `platform_t`:

```c
	const char *vendor_mode_cli;	/* помощник смены режима (Android 10/11) */
	const char *disp_cap;		/* список режимов ТВ (имена) */
```

`platform.c` — определение (после `have_old_hdmi`), заменить развилку:

```c
	if (have_vdec && !have_old_hdmi)
	{
		p.kind = PLAT_ATV14;
		p.sink = SINK_SF_BACKDOOR;
		p.disp_cap = cfg_get_str ("vendor.disp.cap", DEFAULT_VENDOR_DISP_CAP);
		p.vendor_mode_cli = cfg_get_str ("vendor.disp.cli", DEFAULT_VENDOR_DISP_CLI);
		p.sf_backdoor = sf_backdoor_available ();

		/* Android 10/11 (Amlogic): транзакция 1035 там означает другую
		 * операцию (на сборке 11 вызов с одним int роняет SurfaceFlinger),
		 * но режим переключается вендорным SystemControl — им пользуется
		 * штатный системный AFR. Путь выбираем, только если помощник и
		 * вендорный сервис на месте; иначе — прежний пассивный режим. */
		if (!p.sf_backdoor &&
		    sysfs_exists ("/vendor/bin/systemcontrol") == 0 &&
		    sysfs_exists ("/system_ext/priv-app/droidlogic-res/droidlogic-res.apk") == 0 &&
		    sysfs_exists (p.vendor_mode_cli) == 0)
		{
			p.kind = PLAT_ATV11;
			p.sink = SINK_VENDOR_MODE;
		}
	}
```

лог: в строку «Окружение» добавить вид и sink:

```c
	trace (1, "Окружение: %s (sink=%s, backdoor=%s)\n",
	       (p.kind == PLAT_LEGACY) ? "legacy" :
	       (p.kind == PLAT_ATV11) ? "ATV11" : "ATV14",
	       (p.sink == SINK_SF_BACKDOOR) ? "sf-1035" :
	       (p.sink == SINK_VENDOR_MODE) ? "vendor-mode" : "sysfs",
	       p.sf_backdoor ? "да" : "нет");
```

предупреждение о пассивном режиме — только для ATV14:

```c
	if ((p.kind == PLAT_ATV14) && !p.sf_backdoor)
		trace (0, "ВНИМАНИЕ: backdoor SurfaceFlinger недоступен — AFR работать не будет\n");
```

`afrd.c` (в `afrd_init`, рядом с `g_mode_path`):

```c
	g_vendor_disp_cli = cfg_get_str ("vendor.disp.cli", DEFAULT_VENDOR_DISP_CLI);
```

`afrd_run` — умолчание быстрого пути (по образцу `switch.delay.off`):

```c
	if (!cfg_get (g_cfg, "fps.declare.fast"))
	{
		int def = (g_plat.kind == PLAT_ATV11) ? DEFAULT_FPS_DECLARE_FAST_ATV11
		                                      : DEFAULT_FPS_DECLARE_FAST_ATV14;
		g_fps_declare_fast = def;
		if (def)
			trace (1, "fps.declare.fast не задан — на этом пути беру %d\n", def);
	}
	else
		g_fps_declare_fast = cfg_get_int ("fps.declare.fast", 0);
```

`afrd.c` — глобал `int g_fps_declare_fast = 0;` и `extern` в `afrd.h`.

- [x] **Step 3: Зелёные тесты**

Run: `sh tests/run_tests.sh 2>&1 | grep -E 'cfgkeys|inactive|FAIL'`
Expected: `cfgkeys: OK`, `inactive: OK`.

- [x] **Step 4: Коммит**

```bash
git add platform.h platform.c afrd.h afrd.c cfgkeys.c config/afrd2.ini config/afrd-android8+.ini
git commit -m "feat(platform): вид PLAT_ATV11 (вендорный SystemControl) и ключи нового пути"
```

---

### Task 4: Чистый выбор режима по частоте (имена из `disp_cap`)

**Files:**
- Create: `modes_sel.c`, `modes_sel.h`
- Test: `tests/test_modes_sel.c` (новый набор; подключить в `tests/run_tests.sh`)
- Modify: `modes.c` — вызвать новый модуль из `select_mode` (без смены поведения)

**Interfaces:**
- Produces: `int modes_select_by_hz (const display_mode_t *modes, int n, int hz, int width, int height, bool interlaced, int prefer_exact, int floor_hz, bool (*banned)(int hz, void *ctx), void *ctx, char *name_out, size_t name_sz, int *hz_out)` — возврат 0 при успехе, -1 если режима нет.
- Consumes: `display_mode_t` (`afrd.h`), `display_mode_hz`, `display_mode_set_hz`.

- [x] **Step 1: Тест, который падает**

`tests/test_modes_sel.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../afrd.h"
#include "../modes_sel.h"

/* Правила те же, что в legacy-пути (afrd.c: select_mode): кратные частоты
 * предпочитаются при prefer_exact=0, точное совпадение — при 1. */
static const display_mode_t g_modes [] =
{
	{ "1080p60hz", 1920, 1080, false, 60 * 256 },
	{ "1080p50hz", 1920, 1080, false, 50 * 256 },
	{ "1080p25hz", 1920, 1080, false, 25 * 256 },
	{ "1080p24hz", 1920, 1080, false, 24 * 256 },
	{ "1080p30hz", 1920, 1080, false, 30 * 256 },
	{ "720p50hz",  1280,  720, false, 50 * 256 },
};

static void test_select_25 (void)
{
	char name [32]; int hz = 0;

	/* 25 fps при prefer_exact=0 -> удвоение 2:1, режим 50 Гц */
	assert (modes_select_by_hz (g_modes, 6, 25 * 256, 1920, 1080, false, 0, 0,
	                            NULL, NULL, name, sizeof (name), &hz) == 0);
	assert (strcmp (name, "1080p50hz") == 0);
	assert (hz == 50 * 256);

	/* prefer_exact=1 -> точная 25 Гц */
	assert (modes_select_by_hz (g_modes, 6, 25 * 256, 1920, 1080, false, 1, 0,
	                            NULL, NULL, name, sizeof (name), &hz) == 0);
	assert (strcmp (name, "1080p25hz") == 0);
}

static void test_select_24 (void)
{
	char name [32]; int hz = 0;

	/* 23.976 (6144 в 24.8 не наш случай: класс даёт 6138) -> режим 24 Гц */
	assert (modes_select_by_hz (g_modes, 6, 6138, 1920, 1080, false, 0, 0,
	                            NULL, NULL, name, sizeof (name), &hz) == 0);
	assert (strcmp (name, "1080p24hz") == 0);
}

static void test_geometry_and_floor (void)
{
	char name [32]; int hz = 0;

	/* другой геометрии при 25 fps нет -> 720p-режим не годится */
	assert (modes_select_by_hz (g_modes, 6, 25 * 256, 1920, 1080, true, 0, 0,
	                            NULL, NULL, name, sizeof (name), &hz) == -1);

	/* пол (mode.min.hz.list) отсекает 25 Гц, остаётся удвоение 50 */
	assert (modes_select_by_hz (g_modes, 6, 25 * 256, 1920, 1080, false, 1, 30 * 256,
	                            NULL, NULL, name, sizeof (name), &hz) == 0);
	assert (strcmp (name, "1080p50hz") == 0);
}

static void test_banned (void)
{
	char name [32]; int hz = 0;
	/* 50 Гц запрещена -> при 25 fps остаётся точная 25 */
	static int banned_hz [] = { 50 * 256 };
	extern bool test_banned_cb (int hz, void *ctx);
	assert (modes_select_by_hz (g_modes, 6, 25 * 256, 1920, 1080, false, 0, 0,
	                            test_banned_cb, banned_hz, name, sizeof (name), &hz) == 0);
	assert (strcmp (name, "1080p25hz") == 0);
}

int main (void)
{
	test_select_25 ();
	test_select_24 ();
	test_geometry_and_floor ();
	test_banned ();
	printf ("modes_sel: OK\n");
	return 0;
}
```

(функция `test_banned_cb` — в самом тесте, `bool test_banned_cb (int hz, void *ctx) { int *b = ctx; return hz != *b; }`)

- [x] **Step 2: Убедиться, что падает**

Run: `sh tests/run_tests.sh 2>&1 | grep -A3 modes_sel`
Expected: тест не подключён/не компилируется.

- [x] **Step 3: Реализация**

`modes_sel.h`:

```c
#ifndef MODES_SEL_H
#define MODES_SEL_H

#include <stdbool.h>
#include <stddef.h>
#include "afrd.h"

/* Фильтр частот (mode.blacklist.rates): false — частота запрещена. */
typedef bool (*modes_hz_filter_t) (int hz, void *ctx);

/* Выбор режима по частоте контента из списка имён (disp_cap).
 *
 * Правила — те же, что в legacy-пути (afrd.c: select_mode): геометрия и
 * чересстрочность обязаны совпасть, ошибка частоты не больше 4.3 %, кратность
 * не выше 3; при prefer_exact=0 предпочитаются кратные (25 -> 50), при 1 —
 * точное совпадение. floor_hz (24.8, 0 — нет) отсекает режимы ниже порога
 * (mode.min.hz.list). Нужен новому пути на Android 10/11: там режим задаётся
 * ИМЕНЕМ, а не id таблицы SurfaceFlinger.
 *
 * Возврат: 0 — найден (*name_out, *hz_out), -1 — подходящего режима нет. */
int modes_select_by_hz (const display_mode_t *modes, int n, int hz,
                        int width, int height, bool interlaced,
                        int prefer_exact, int floor_hz,
                        modes_hz_filter_t allowed, void *ctx,
                        char *name_out, size_t name_sz, int *hz_out);

#endif
```

`modes_sel.c` — перенести логику рейтинга из `afrd.c: select_mode`, заменив глобальные `g_modes/g_current_mode/g_state.hz/g_mode_prefer_exact` на параметры. Тело — дословно прежний алгоритм (rating, delta, rate_n, blacklist через `allowed`).

`modes.c` — `select_mode` теперь вызывает модуль:

```c
static bool select_mode (int floor, display_mode_t *best)
{
	char name [32];
	int hz = 0;
	if (modes_select_by_hz (g_modes, g_modes_n, g_state.hz,
	                        g_current_mode.width, g_current_mode.height,
	                        g_current_mode.interlaced, g_mode_prefer_exact, floor,
	                        rate_allowed_cb, NULL, name, sizeof (name), &hz) != 0)
	{
		best->name [0] = 0;
		return false;
	}

	/* имя и частота — в структуру режима (её ждут вызовы ниже) */
	for (int i = 0; i < g_modes_n; i++)
		if (strcmp (g_modes [i].name, name) == 0)
		{
			*best = g_modes [i];
			display_mode_set_hz (best, g_state.hz);
			best->fractional = ((hz & 255) != 0);
			return true;
		}

	best->name [0] = 0;
	return false;
}
```

(`rate_allowed_cb` — обёртка над существующей `rate_is_blacklisted`.)

`tests/run_tests.sh`: добавить `run test_modes_sel modes_sel.c`.

- [x] **Step 4: Зелёные тесты**

Run: `sh tests/run_tests.sh 2>&1 | grep -E 'modes_sel|FAIL'`
Expected: `modes_sel: OK`.

- [x] **Step 5: Коммит**

```bash
git add modes_sel.h modes_sel.c tests/test_modes_sel.c tests/run_tests.sh modes.c
git commit -m "refactor(modes): чистый выбор режима по частоте в отдельном модуле + рейтинг legacy"
```

---

### Task 5: Ветка ATV11 в цикле (применение по имени)

**Files:**
- Modify: `afrd.c` (точка решения в `run_atv14`, ~2749–2790; `atv14_apply_mode`)
- Modify: `afrd.h` (тип `mode_target_t`)

**Interfaces:**
- Consumes: `modes_select_by_hz` (Task 4), `SINK_VENDOR_MODE` (Task 2), `PLAT_ATV11` (Task 3).
- Produces: `typedef struct { int sf_id; char name [32]; int hz; } mode_target_t;` и функция `static int atv_apply_target (const platform_t *plat, const mode_target_t *t, const char *skipped)`.

- [x] **Step 1: Тип и применение**

`afrd.h`:

```c
/* Целевой режим: id — для транзакции 1035 (ATV14), имя — для sysfs и вендорного
 * SystemControl (ATV11). Заполняется ровно то поле, которое нужно виду sink. */
typedef struct
{
	int  sf_id;
	char name [32];
	int  hz;
} mode_target_t;
```

`afrd.c` — обобщить существующую `atv14_apply_mode` в `atv_apply_target`:

```c
static int atv_apply_target (const platform_t *plat, const mode_target_t *t, const char *skipped)
{
	if (plat->sink == SINK_VENDOR_MODE)
	{
		if (!t->name [0])
			return -1;
		if (sink_apply (plat->sink, g_mode_path, -1, t->name) != 0)
		{
			trace (1, "вендорный SystemControl не применил режим %s\n", t->name);
			return -2;
		}
	}
	else
	{
		if (t->sf_id < 0)
			return -1;
		if ((g_sf_modes_n <= 0) ||
		    (sfmodes_current_hz (g_sf_modes, g_sf_modes_n, t->sf_id) == 0))
		{
			trace (1, "id %d отсутствует в текущей таблице режимов — %s\n", t->sf_id, skipped);
			return -1;
		}
		if (sink_apply (plat->sink, g_mode_path, t->sf_id, NULL) != 0)
		{
			trace (1, "backdoor не применил режим id %d\n", t->sf_id);
			return -2;
		}
	}

	/* цветовое пространство по правилам cs.select — как в legacy (modes.c) */
	char *cm = sysfs_read (g_mode_path);
	…
	return 0;
}
```

все прежние вызовы `atv14_apply_mode (plat, want_sf_id, …)` заменяются на сборку `mode_target_t` (для ATV14 `sf_id = want_sf_id`, `name[0] = 0`) и вызов `atv_apply_target`.

- [x] **Step 2: Выбор режима для ATV11**

В точке решения (после того как замер дал `hz`, ~2755) добавить:

```c
	/* Новый путь на Android 10/11: режим выбирается по ИМЕНИ из disp_cap
	 * (таблицы SurfaceFlinger там нет), применяется вендорным SystemControl. */
	if (plat->sink == SINK_VENDOR_MODE)
	{
		mode_target_t t;
		memset (&t, 0, sizeof (t));
		t.sf_id = -1;

		if (modes_select_by_hz (g_modes, g_modes_n, hz,
		                        g_current_mode.width, g_current_mode.height,
		                        g_current_mode.interlaced, g_mode_prefer_exact,
		                        min_hz_floor, rate_allowed_cb, NULL,
		                        t.name, sizeof (t.name), &t.hz) != 0)
		{
			trace (1, "режим под %d.%02u Гц не найден — пропускаю\n", hz >> 8, (100 * (hz & 255) + 128) >> 8);
		}
		else if (atv_apply_target (plat, &t, "режим не найден") == 0)
			held = true;
	}
	else
		…существующая ветка с want_sf_id…
```

выбор списка режимов — `g_modes` на ATV11 заполняется тем же разбором, что у legacy, но из `plat->disp_cap`:

```c
	/* на ATV11 список режимов — из disp_cap нового пути */
	if (plat->sink == SINK_VENDOR_MODE && !g_modes_n)
		video_modes_load (plat->disp_cap);
```

(функция `video_modes_load` — тонкая обёртка в `modes.c` над существующим разбором `disp_cap`, принимающая путь параметром; сейчас путь берётся из `g_hdmi_dev`.)

- [x] **Step 3: Проверка на стенде `.45`**

```bash
cd /home/reyter/AI/afrd2 && ./mka && sh tools/stand/build_push.sh   # но с нашим профилем на .45
# затем: запустить демон вручную с логом и сыграть реальный 23.976
```

Expected (лог): `Окружение: ATV11 (sink=vendor-mode, backdoor=нет)`, затем
`Переключаю режим (вендорный SystemControl) на 1080p24hz` и `vinfo`/SF на 23.976.

- [x] **Step 4: Коммит**

```bash
git add afrd.c afrd.h modes.c
git commit -m "feat(afrd): ветка ATV11 — выбор режима по имени и вендорное применение"
```

---

### Task 6: Быстрый путь по заявленной частоте

**Files:**
- Modify: `fps_class.h`, `fps_class.c` (чистый помощник), `afrd.c` (ветка быстрого пути), `afrd.h`
- Test: `tests/test_declare.c` (новый), `tests/run_tests.sh`

**Interfaces:**
- Consumes: `decl_fps` (Task 1), `fps_classify`, `fps_rate_group`, `g_fps_declare_fast` (Task 3).
- Produces: `int fps_declared_class (int declared_fps)` → класс 24.8 или 0; `bool fps_group_differs (int a_hz, int b_hz)`.

- [x] **Step 1: Тест, который падает**

`tests/test_declare.c`:

```c
#include <assert.h>
#include <stdio.h>
#include "../fps_class.h"

/* Заявленная частота от драйвера — целые fps (24, 25, 30). Класс берётся тем
 * же классификатором, что и замер, поэтому «24» и «23.976» попадают в одну
 * группу и не считаются расхождением. */
static void test_class (void)
{
	assert (fps_declared_class (24) == fps_classify (24.0));
	assert (fps_declared_class (25) == fps_classify (25.0));
	assert (fps_declared_class (30) == fps_classify (30.0));
	/* мусор и ноль -> 0 (быстрый путь пропускается) */
	assert (fps_declared_class (0) == 0);
	assert (fps_declared_class (255) == 0);
}

static void test_group (void)
{
	/* 23.976 и 24 — одна группа: расхождением не считаем */
	assert (!fps_group_differs (fps_classify (23.976), fps_classify (24.0)));
	assert (!fps_group_differs (fps_classify (29.97), fps_classify (30.0)));
	/* 24 и 30 — разные группы: замер обязан поправить */
	assert (fps_group_differs (fps_classify (24.0), fps_classify (30.0)));
	/* ноль не считается расхождением (нечего сравнивать) */
	assert (!fps_group_differs (0, fps_classify (24.0)));
}

int main (void)
{
	test_class ();
	test_group ();
	printf ("declare: OK\n");
	return 0;
}
```

- [x] **Step 2: Убедиться, что падает**

Run: `sh tests/run_tests.sh 2>&1 | grep -A3 declare`
Expected: `fps_declared_class` не объявлен.

- [x] **Step 3: Реализация**

`fps_class.h`:

```c
/* Класс частоты по заявленной драйвером частоте кадра (целые fps: 24, 25, 30).
 * Возврат: fixed-point 24.8 Гц или 0, если значение нестандартное/мусор.
 * Нужен быстрому пути решения (fps.declare.fast): системный AFR Amlogic берёт
 * именно заявленную частоту, поэтому и мы можем решить сразу, а замер потом
 * подтвердит или поправит. */
int fps_declared_class (int declared_fps);

/* Разные ли группы у двух частот (fps_rate_group): 23.976 и 24 — одна группа,
 * 24 и 30 — разные. Ноль слева означает «сравнивать нечего» -> false. */
bool fps_group_differs (int a_hz, int b_hz);
```

`fps_class.c`:

```c
int fps_declared_class (int declared_fps)
{
	if ((declared_fps <= 0) || (declared_fps > 120))
		return 0;	/* мусор: драйвер так не заявляет */
	return fps_classify ((double)declared_fps);
}

bool fps_group_differs (int a_hz, int b_hz)
{
	int ga = fps_rate_group (a_hz), gb = fps_rate_group (b_hz);
	if (!ga || !gb)
		return false;
	return ga != gb;
}
```

- [x] **Step 4: Ветка быстрого пути в `afrd.c`**

до блока огибающей:

```c
		/* Быстрый путь: системный AFR Amlogic решает по заявленной частоте
		 * (vdec_status: frame rate) и потому переключает за 2–4 с. Делаем так
		 * же, но помечаем решение предварительным: замер ниже подтвердит его
		 * или поправит (fps.recheck.delay.ms). */
		if (g_fps_declare_fast && !fast_applied && v0.decl_fps > 0)
		{
			int fast_hz = fps_declared_class ((int)v0.decl_fps);
			if (fast_hz)
			{
				mode_target_t ft;
				… выбор режима как в Task 5 (ATV11) или подбор id (ATV14) …
				if (atv_apply_target (plat, &ft, "быстрое решение") == 0)
				{
					fast_applied = true;
					fast_class = fast_hz;
					trace (1, "быстрое решение по заявленной частоте: %d fps -> %s\n",
					       (int)v0.decl_fps, ft.name [0] ? ft.name : "id");
				}
			}
		}
```

после решения огибающей (там же, где сейчас `want_sf_id = id`):

```c
			if (fast_applied && fps_group_differs (fast_class, hz))
				trace (0, "заявлено %d класс, замер дал %d.%02u Гц — правлю режим\n",
				       fast_class >> 8, hz >> 8, (100 * (hz & 255) + 128) >> 8);
```

(сброс `fast_applied = false; fast_class = 0;` — при новом видео, на стопе и в `atv14_invalidate`, рядом с существующими сбросами `want_sf_id`.)

- [x] **Step 5: Зелёные тесты**

Run: `sh tests/run_tests.sh 2>&1 | grep -E 'declare|FAIL'`
Expected: `declare: OK`.

- [x] **Step 6: Коммит**

```bash
git add fps_class.h fps_class.c afrd.c afrd.h tests/test_declare.c tests/run_tests.sh
git commit -m "feat(afrd): быстрый путь решения по заявленной частоте с проверкой замером"
```

---

### Task 7: Настройка в GUI, профилях и документации

**Files:**
- Modify: `android/app/src/main/res/values/strings.xml`, `values-ru/strings.xml`
- Modify: `android/app/src/main/res/xml/preferences.xml` (строка в группе `atv14`)
- Modify: `SettingsFragment.java` (пометка «не действует на Android 6-9»)
- Modify: `README.md`, `ChangeLog-ru.txt`, `ChangeLog-en.txt`, `FAQ-ru.txt`, `FAQ-en.txt`
- Test: `tests/test_inactive.c`, `tests/test_faq.sh`

- [x] **Step 1: Строки**

`values/strings.xml`:
```xml
<string name="prefs_fps_declare_fast">Fast decision by declared frame rate</string>
<string name="prefsum_fps_declare_fast">Apply the mode immediately from the frame rate declared by the decoder (vdec_status: frame rate) — as the built-in system AFR does — and then verify it by measurement: on mismatch the mode is corrected (fps.recheck.delay.ms). 0 disables the fast path.</string>
```

`values-ru/strings.xml`:
```xml
<string name="prefs_fps_declare_fast">Быстрое решение по заявленной частоте</string>
<string name="prefsum_fps_declare_fast">Применять режим сразу по частоте, которую заявил декодер (vdec_status: frame rate), — так делает встроенная системная АПЧ. Затем решение проверяется замером: при расхождении режим исправляется (fps.recheck.delay.ms). 0 — решать только по замеру.</string>
```

- [x] **Step 2: preferences.xml**

в группе `atv14`, первой строкой после «Доверять подсказке плеера»:

```xml
<SwitchPreference android:defaultValue="0" android:key="fps_declare_fast"
                  android:summary="@string/prefsum_fps_declare_fast"
                  android:title="@string/prefs_fps_declare_fast"/>
```

- [x] **Step 3: Пометка для старого пути**

`SettingsFragment.java` — в `kInactiveOnLegacy` добавить `"fps_declare_fast"` (как остальные ключи нового пути).

- [x] **Step 4: Документация**

- `README.md`: в таблицу ключей — строка `fps.declare.fast`; в раздел про Android 10/11 — заменить «в демон ещё не встроен» на описание работы через вендорный SystemControl; в «Статус приёмки» — прогон на `.45`.
- `ChangeLog-ru.txt`/`ChangeLog-en.txt`: раздел `1.0.0` дополнить: AFR на Android 10/11 через вендорный SystemControl; быстрое решение по заявленной частоте.
- `FAQ-ru.txt`/`FAQ-en.txt`: в ответ про системную АПЧ добавить абзац про Android 10/11 (демон использует тот же вендорный механизм, поэтому одновременная работа даёт гонку за режим). Число вопросов не меняется (`tests/test_faq.sh`).

- [x] **Step 5: Тесты**

Run: `sh tests/run_tests.sh 2>&1 | grep -E 'inactive|faq|versions|FAIL'`
Expected: все `OK`.

- [x] **Step 6: Коммит**

```bash
git add android/app/src/main/res/values/strings.xml android/app/src/main/res/values-ru/strings.xml \
        android/app/src/main/res/xml/preferences.xml \
        android/app/src/main/java/ru/cobra/zap/afrd/gui/SettingsFragment.java \
        README.md ChangeLog-ru.txt ChangeLog-en.txt FAQ-ru.txt FAQ-en.txt
git commit -m "docs+gui: строка быстрого решения, описание пути Android 10/11"
```

---

### Task 8: Упаковка вендорного помощника (APK и Magisk)

**Files:**
- Modify: `android/app/src/main/java/ru/cobra/zap/afrd/Control.java` (извлечение помощника)
- Modify: `mka` (копирование dex/sh в `res/raw`)
- Modify: `install/magisk/service.sh`, `install/magisk/build-module.sh`

**Interfaces:**
- Consumes: `tools/droidlogic/afrd-disp.{sh,dex}`.
- Produces: помощник в `cacheDir` приложения и в каталоге модуля; ключ `vendor.disp.cli` указывает на него.

- [x] **Step 1: Копирование в ресурсы (`mka`)**

```sh
# Вендорный помощник (Android 10/11): dex и обёртка — в ресурсы APK,
# как демон и профили. Собирается tools/droidlogic/build-dex.sh.
sh tools/droidlogic/build-dex.sh
cp tools/droidlogic/afrd-disp.dex android/app/src/main/res/raw/afrd_disp.dex
cp tools/droidlogic/afrd-disp.sh  android/app/src/main/res/raw/afrd_disp.sh
```

- [x] **Step 2: Извлечение в приложении**

`Control.java` — рядом с `extractDaemon`:

```java
    /** Помощник смены режима для Android 10/11 (dex + обёртка). */
    public boolean extractVendorCli (Context ctx)
    {
        File dex = new File (ctx.getCacheDir (), "afrd-disp.dex");
        File sh = new File (ctx.getCacheDir (), "afrd-disp.sh");
        boolean ok = jfun.extractFile (ctx, R.raw.afrd_disp, dex);
        ok = ok && jfun.extractFile (ctx, R.raw.afrd_disp_sh, sh);
        return ok;
    }
```

вызвать там же, где `extractDaemon`, и записать путь в конфиг приложения перед запуском демона:

```java
        mIni …  /* уже пишется */
        // vendor.disp.cli=<cacheDir>/afrd-disp.sh
```

- [x] **Step 3: Magisk-модуль**

`install/magisk/build-module.sh`: копировать `tools/droidlogic/afrd-disp.dex` и `afrd-disp.sh` в модуль (`$OUT/afrd-disp.dex`, `$OUT/afrd-disp.sh`), `service.sh` — раскладывать их в `/data/local/tmp/` (умолчание профиля) либо писать путь в свой `afrd.ini`.

- [x] **Step 4: Проверка на `.45`**

```bash
cd /home/reyter/AI/afrd2 && ./mka && cd android && ./gradlew assembleRelease
adb -s 192.168.88.45:5555 install -r app/build/outputs/apk/release/ru.reyter.afrd2-1.0.0-release.apk
# после запуска приложения: проверить /data/data/ru.reyter.afrd2/cache/afrd-disp.*
```

Expected: оба файла на месте, `vendor.disp.cli` в `afrd.ini` указывает на них, демон в логе пишет `Окружение: ATV11 (sink=vendor-mode…)`.

- [x] **Step 5: Коммит**

```bash
git add mka android/app/src/main/java/ru/cobra/zap/afrd/Control.java install/magisk/
git commit -m "feat(pack): вендорный помощник в APK и Magisk-модуле"
```

---

### Task 9: Живая приёмка на `.45` (Android 11)

**Files:**
- Modify: `docs/publication-4pda.md` (суммы артефактов), журнал `../afrd2-journal.md` (§33.20)

> **Выполнено 2026-10-08 (ночь):** прогон вынесен в воспроизводимый скрипт
> `tools/stand/acceptance_atv11.sh` (8 пунктов, включая зависшего помощника и
> нечитаемый `disp.cap`): **OK=23 FAIL=0**. Перед прогоном код прошёл независимое
> ревью — найдено 7 дефектов, все исправлены и проверены живьём; ещё два дефекта
> нашла сама приёмка. Подробности — журнал §33.21, README «Статус приёмки».

- [x] **Step 1: Прогон по таблице приёмки спеки**

| Проверка | Как | Ожидание |
|---|---|---|
| 23.976 | `am start … real_23976.mkv` (Just Player) | `1080p24hz` за ≤5 с; `vinfo` и SF 23.976 |
| 25 | `real_25.mkv` | `1080p50hz` за ≤5 с |
| 60 | `t60.mp4` | `1080p60hz` |
| синтетика 23.976 | `t24000_1001.mp4` | сначала 30-й класс, после огибающей правка на 24; в журнале обе строки |
| стоп | `am force-stop` плеера | возврат исходного режима |
| `fps.declare.fast=0` | правка `afrd.ini` + `reconf` | поведение прежнее (только огибающая) |
| помощник недоступен | переименовать `afrd-disp.dex` | предупреждение, режим не тронут |

- [x] **Step 2: Сравнение с системным AFR**

Системный AFR (`persist.sys.autoframemode=2`) оставить включённым, как у владельца, и снять те же
замеры времени; расхождение описать в журнале.

Сделано в §33.19: замеры системного AFR сняты отдельно, наш путь — отдельно, итог «паритет 2–4 с»
описан в журнале. Осталось владельцу (нужен бокс с ТВ, где штатный AFR включён вместе с нашим):
прогон обоих путей одновременно — сценарий «как у владельца»; в приёмке `.45` системный AFR на
время прогона выключается, чтобы видеть только наш путь.

- [x] **Step 3: Документация и коммит**

Сделано: журнал §33.21 (находки ревью, правки, прогон), README («Статус приёмки», описание
вендорного пути и `switch.timeout`, строка про скрипт приёмки), ChangeLog ru/en (блок
исправлений внутри записи 1.0.0 — релиз ещё не опубликован), артефакты `out/afrd2-1.0.0/`
пересобраны и проверены дым-прогоном. Коммит `e0c06c7`.

**Суммы в теме 4PDA и дату публикации проставляет владелец** — артефакты пересобраны,
новые md5/размеры:

| артефакт | размер | md5 |
|---|---|---|
| `ru.reyter.afrd2-1.0.0-release.apk` | 406451 | `62f25f077430a1c538d3390e97e203c9` |
| `afrd2-magisk-armeabi-v7a.zip` | 55213 | `fa97cb00ec6b5f5787d7d16c05fe029a` |
| `afrd2-magisk-arm64-v8a.zip` | 64608 | `58d5078616fde257afb40c5f1c66a942` |

---

## Self-Review

**Покрытие спеки:** §4.1 компоненты → задачи 2–6; §4.2 определение → задача 3; §4.3 sink → 2;
§4.4 выбор режима → 4; §4.5 быстрый путь → 1, 6; §4.6 поток → 5, 6; §4.7 упаковка → 8;
§5 краевые случаи → 2 (нет помощника), 6 (мусор), 8 (нет dex); §6 файлы → все задачи;
§7 приёмка → 9; §9 решения → 3 (ключи), 7 (GUI, ЧАВО).

**Порядок:** 1 (данные) → 2 (sink) → 3 (платформа/ключи) → 4 (выбор режима) → 5 (встройка) →
6 (быстрый путь) → 7 (GUI/доки) → 8 (упаковка) → 9 (приёмка). Каждая задача оставляет зелёные тесты.

**Типы согласованы:** `SINK_VENDOR_MODE`, `PLAT_ATV11`, `mode_target_t`, `decl_fps`,
`fps_declared_class`, `fps_group_differs`, `modes_select_by_hz`, `g_vendor_disp_cli`,
`g_fps_declare_fast` — объявлены там, где впервые используются, и совпадают по именам далее.
