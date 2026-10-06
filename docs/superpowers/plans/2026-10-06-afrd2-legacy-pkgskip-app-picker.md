# AFRd2: перенос чёрного списка плееров на Android 6–9 и экран выбора приложений — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `switch.skip.pkg` и `mode.min.hz.list` начинают действовать и на старом пути (Android 6–9),
а значения-списки пакетов в GUI задаются выбором из списка установленных приложений вместо ввода
текста с пульта.

**Architecture:** Сначала общий для обоих путей разбор «пакета приложения на переднем плане»
(новый модуль `pkgdet.c`, команда дампа одна на обе платформы), затем гейт в двух точках решения
старого пути. Отдельно в APK: строка настройки `XAppsPreference` + полноэкранная
`AppsPickerActivity`, результат которой применяется существующим путём `applyPrefs()`.

**Tech Stack:** C (bionic, NDK, `afrd` + host-тесты обычным `cc`), Java (framework `android.preference`,
AGP 3.3.2, Gradle 4.10.1, compileSdk 27, minSdk 23, targetSdk 28), adb-стенды: `.50` (ATV14,
S905X5M) и `.76` (ATV9, UGOOS AM6).

**Spec:** `docs/superpowers/specs/2026-10-06-afrd2-app-picker-design.md`

## Global Constraints

* Тестовый путь запуска host-тестов: `sh tests/run_tests.sh` (сейчас 10 наборов; после Task 1 — 11).
* Сборка APK: `cd android && JAVA_HOME=/usr/lib/jvm/java-11-openjdk-amd64 ./gradlew --offline assembleDebug`
  (SDK: `~/Android/Sdk` — `platforms/android-27`, `build-tools/28.0.3`). Java-компилятор включён с
  `-Xlint:deprecation -Xlint:unchecked`; предупреждений быть не должно.
* Сборка демона: `./mka` (NDK r26d, armeabi-v7a + arm64-v8a; на выходе `libs/<abi>/afrd`) —
  предупреждений быть не должно. `mka` также копирует профили и ЧАВО в `android/app/src/main/res/raw*`.
* Значение настроек-списков — **имена пакетов через пробел**; разделитель в демоне — любой из
  `" \t\r\n"` (`strfun.c:12`), запятые не понимаются.
* Старый путь трогаем **аддитивно**: при пустых `switch.skip.pkg` и `mode.min.hz.list` поведение
  демона на Android 6–9 обязано остаться побайтово тем же (ни одного лишнего `fork/exec`, ни одной
  смены решения).
* Ключи `switch.skip.pkg` и `mode.min.hz.list` объявляются `CFGKEY_BOTH` (`cfgkeys.c`); остальные
  ATV14-ключи не трогаем.
* Стенд `.76` занят рабочим upstream-AFRd владельца: эксклюзивный доступ к демону и полный откат —
  по процедуре §27.1 журнала (`journal.md`). Перезагрузка боксов и выключение ТВ — только с
  согласия владельца.

## Review Focus

Пять классов входных данных и отказов, которые вероятнее всего укусят пользователя и которые
тесты ниже пиннят явно:

1. Дамп активности в формате Android 6–9 (`ResumedActivity: ActivityRecord{...}`) — пакет должен
   разбираться так же, как на ATV14 (Task 1, шаг 1).
2. Дамп без разобранной записи (`topResumedActivity=null`, обрезанная строка, пустой вывод) —
   пустая строка, а не мусор в имени пакета и не падение (Task 1, шаг 1).
3. Пустой `switch.skip.pkg` (умолчание всех существующих пользователей старого пути) — демон не
   запускает ни одного `dumpsys`, решения и логи не меняются (Task 2 шаги 3–5; живая — Task 5).
4. Уже установленное приложение: ключа ещё нет в `ini_options` — профильное значение
   (`com.google.android.youtube.tv`) не должно потеряться при первой же правке настроек
   (Task 3, шаг 8; живая — Task 5, п.12).
5. Пакет в настройке, которого нет в системе или который перестал быть плеером — строка остаётся
   в списке и снимается отметкой, значение не вычищается молча (Task 3, шаг 6; живая — Task 5, п.10).

---

### Task 1: Общий разбор пакета на переднем плане (`pkgdet`)

**Files:**
- Create: `pkgdet.h`, `pkgdet.c`
- Create: `tests/test_pkgdet.c`
- Modify: `tests/run_tests.sh` (добавить набор)
- Modify: `platform.c` (одна команда дампа для обоих путей)
- Modify: `afrd.c` (замена `parse_top_package`/`atv14_top_package` на `pkgdet_parse`/`pkg_probe`)
- Modify: `GNUmakefile`, `jni/Android.mk` (новый файл `pkgdet.c` в списке исходников — без этого демон не линкуется)

**Interfaces:**
- Produces: `bool pkgdet_parse (const char *dump, char *out, size_t out_sz)` — чистая функция, знает
  оба формата поля; `true` — пакет скопирован в `out` (всегда NUL-терминирован).
- Produces (в `afrd.c`, статические): `static int pkg_probe (const platform_t *plat, char *out, size_t out_sz)`
  (0 — разобрано, −1 — команда не удалась, −2 — вывод не разобран) и
  `static void pkg_probe_logged (const platform_t *plat)`.
- Consumes: `platform_t::activity_dump_cmd` (заполняется на обоих путях).

- [ ] **Step 1: Написать падающий тест**

Создать `tests/test_pkgdet.c`. Строки-фикстуры — **реальные** строки со стендов (сняты 2026-10-06):

```c
/*
 * Разбор «пакета на переднем плане» из вывода
 * `dumpsys activity activities | grep -m1 ResumedActivity`.
 * Фикстуры — дословные строки со стендов: .50 (ATV14) и .76 (Android 9).
 */
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "../pkgdet.h"

/* Реальные строки: ATV14 (X88 Pro X5M) и Android 9 (UGOBO AM6). */
static const char *ATV14_LINE =
	"    topResumedActivity=ActivityRecord{d34eeb2 u0 studio.scillarium.ottnavigator/.MainActivity t1590}";
static const char *ATV9_LINE =
	"  ResumedActivity: ActivityRecord{b4570ac u0 com.google.android.tvlauncher/.BootHelperEmptyActivity t3427}";

/* На ATV14 в дампе есть оба поля, и grep отдаёт первым topResumedActivity;
 * проверяем, что при обоих строках результат тот же. */
static const char *ATV14_BOTH =
	"    topResumedActivity=ActivityRecord{d34eeb2 u0 studio.scillarium.ottnavigator/.MainActivity t1590}\n"
	"  ResumedActivity: ActivityRecord{d34eeb2 u0 studio.scillarium.ottnavigator/.MainActivity t1590}\n";

static void test_format (void)
{
	char pkg [64];

	assert (pkgdet_parse (ATV14_LINE, pkg, sizeof (pkg)));
	assert (!strcmp (pkg, "studio.scillarium.ottnavigator"));

	assert (pkgdet_parse (ATV9_LINE, pkg, sizeof (pkg)));
	assert (!strcmp (pkg, "com.google.android.tvlauncher"));

	assert (pkgdet_parse (ATV14_BOTH, pkg, sizeof (pkg)));
	assert (!strcmp (pkg, "studio.scillarium.ottnavigator"));
}

/* Обрезанная запись: пакет отделяется ' ' или '}', закрывающей скобки может не быть. */
static void test_truncated (void)
{
	char pkg [64];

	assert (pkgdet_parse ("  ResumedActivity: ActivityRecord{b4570ac u0 com.example.player/.Main t1",
	                      pkg, sizeof (pkg)));
	assert (!strcmp (pkg, "com.example.player"));
}

/* Мусор и «нетактивности» — false и пустая строка, не мусор в имени пакета. */
static void test_garbage (void)
{
	char pkg [64];

	assert (!pkgdet_parse ("    topResumedActivity=null", pkg, sizeof (pkg)));
	assert (!pkg [0]);

	assert (!pkgdet_parse ("  ResumedActivity: ActivityRecord{b4570ac u0 }", pkg, sizeof (pkg)));
	assert (!pkg [0]);

	assert (!pkgdet_parse ("No activities", pkg, sizeof (pkg)));
	assert (!pkg [0]);

	assert (!pkgdet_parse ("", pkg, sizeof (pkg)));
	assert (!pkg [0]);
}

/* Буфера не хватает — false, а не усечённое имя (иначе пакет не совпадёт
 * с элементом switch.skip.pkg и AFR включится плееру, которому он мешает). */
static void test_short_buffer (void)
{
	char pkg [8];
	assert (!pkgdet_parse (ATV9_LINE, pkg, sizeof (pkg)));
}

int main (void)
{
	test_format ();
	test_truncated ();
	test_garbage ();
	test_short_buffer ();
	printf ("test_pkgdet: OK\n");
	return 0;
}
```

- [ ] **Step 2: Прописать набор в раннере и убедиться, что он не собирается**

В `tests/run_tests.sh` добавить строку после `run test_cfgkeys ...`:

```sh
run test_pkgdet pkgdet.c
```

Запустить: `sh tests/run_tests.sh`
Ожидаемо: остальные наборы проходят, `test_pkgdet` падает — `pkgdet.h: No such file or directory`.

- [ ] **Step 3: Реализовать разбор**

Создать `pkgdet.h`:

```c
/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * Разбор «пакета приложения на переднем плане» из дампа активности.
 * Один разбор на оба пути работы демона (Android 6-9 и 10-14): форматы поля
 * различаются, внутренняя часть записи — нет.
 */

#ifndef __PKGDET_H__
#define __PKGDET_H__

#include <stdbool.h>
#include <stddef.h>

/**
 * Найти пакет приложения на переднем плане в выводе команды
 * `dumpsys activity activities | grep -m1 ResumedActivity`.
 *
 * Понимает оба формата поля:
 *   "topResumedActivity=ActivityRecord{<hash> u<uid> <пакет>/<активити> t<n>}"  (Android 10-14)
 *   "ResumedActivity: ActivityRecord{<hash> u<uid> <пакет>/<активити> t<n>}"    (Android 6-9)
 *
 * @arg dump   текст вывода (NUL-терминированный)
 * @arg out    буфер для имени пакета
 * @arg out_sz размер буфера
 * @return true — пакет найден и скопирован в out (out — всегда NUL-строка);
 *         false — признака нет, запись не разобрана или пакет не влез в буфер
 *                 (усечённое имя совпало бы с элементом списка неверно).
 */
bool pkgdet_parse (const char *dump, char *out, size_t out_sz);

#endif /* __PKGDET_H__ */
```

Создать `pkgdet.c` (тело разбора — обобщение проверенного `parse_top_package` из `afrd.c:1246`):

```c
#include <string.h>
#include "pkgdet.h"

bool pkgdet_parse (const char *dump, char *out, size_t out_sz)
{
	if (out && out_sz)
		out [0] = 0;

	if (!dump || !out || !out_sz)
		return false;

	const char *p = strstr (dump, "topResumedActivity=ActivityRecord{");
	if (p)
		p += strlen ("topResumedActivity=ActivityRecord{");
	else
	{
		p = strstr (dump, "ResumedActivity: ActivityRecord{");
		if (!p)
			return false;
		p += strlen ("ResumedActivity: ActivityRecord{");
	}

	while (*p)
	{
		const char *sp = strchr (p, ' ');
		if (!sp)
			break;

		/* после поля "u<uid> " сразу идёт компонент "<пакет>/<активити>" */
		if ((sp [1] == 'u') && (sp [2] >= '0') && (sp [2] <= '9'))
		{
			const char *c = sp + 2;
			while ((*c >= '0') && (*c <= '9'))
				c++;
			if (*c == ' ')
				c++;

			const char *slash = strchr (c, '/');
			const char *end = slash ? slash : strpbrk (c, " }");
			if (end && (end > c) && ((size_t)(end - c) < out_sz))
			{
				memcpy (out, c, (size_t)(end - c));
				out [end - c] = 0;
				return true;
			}

			return false;
		}

		p = sp + 1;
	}

	return false;
}
```

- [ ] **Step 4: Убедиться, что тест проходит**

Run: `sh tests/run_tests.sh`
Ожидаемо: `test_pkgdet: OK`, все 11 наборов зелёные.

- [ ] **Step 5: Одна команда дампа для обоих путей**

В `platform.c` добавить рядом с `#define CS_CLI`:

```c
/* Вывод `dumpsys activity activities` нужен только одной строкой: приложение на
 * переднем плане. Поле называется по-разному — "topResumedActivity=..." (Android
 * 10-14) и "ResumedActivity: ..." (Android 6-9), но подстрока "ResumedActivity"
 * есть в обоих, а первое совпадение в дампе — нужная строка (на ATV14 первым
 * идёт topResumedActivity; проверено на стенде .50 и .76). Фильтр на стороне
 * команды обязателен, как и для дампа SurfaceFlinger: дамп activity — 18-26 КБ,
 * а буфер приёма в afrd.c под него — 4 КБ. */
#define ACTIVITY_DUMP_CMD "dumpsys activity activities | grep -m1 ResumedActivity"
```

Удалить строку `p.activity_dump_cmd = "dumpsys activity activities | grep -m1 topResumedActivity";`
из ATV14-ветки и добавить в общую часть `platform_detect()`, сразу после
`p.vdec_status = VDVC_STATUS;`:

```c
	/* Нужна обоим путям: по этому пакету работает switch.skip.pkg */
	p.activity_dump_cmd = ACTIVITY_DUMP_CMD;
```

- [ ] **Step 6: Перевести разбор на `pkgdet` в `afrd.c`**

Добавить `#include "pkgdet.h"` к остальным include в начале `afrd.c`.

Удалить функцию `parse_top_package()` целиком (`afrd.c:1246-1290`).

Тело `atv14_top_package()` заменить на `pkg_probe()` (переименовать и убрать статический буфер
дампа в пользу общего):

```c
/* Приложение на переднем плане (общее для обоих путей работы демона).
 * Возврат: 0 — разобрано (*out заполнен), -1 — команда не удалась,
 * -2 — вывод не разобран. */
static int pkg_probe (const platform_t *plat, char *out, size_t out_sz)
{
	if (out && out_sz)
		out [0] = 0;

	if (!plat->activity_dump_cmd)
		return -1;

	static char dump [4096];
	if (exec_capture (plat->activity_dump_cmd, dump, sizeof (dump)) != 0)
		return -1;

	return pkgdet_parse (dump, out, out_sz) ? 0 : -2;
}
```

`atv14_probe_package()` переименовать в `pkg_probe_logged()` (тело оставить, заменив вызов):

```c
/* Пакет для fps.correct.list / mode.min.hz.list / switch.skip.pkg:
 * разобрать и, если не вышло, сказать об этом в журнал. */
static void pkg_probe_logged (const platform_t *plat)
{
	int rc = pkg_probe (plat, g_cur_pkg, sizeof (g_cur_pkg));
	if (rc == -1)
		trace (1, "не удалось определить приложение на переднем плане\n");
	else if (rc == -2)
		trace (1, "приложение на переднем плане не разобрано\n");
}
```

Обновить три вызова: `atv14_top_package (plat, video_pkg, sizeof (video_pkg))` →
`pkg_probe (plat, video_pkg, sizeof (video_pkg))` (строка ~2149),
`atv14_top_package (plat, top, sizeof (top))` → `pkg_probe (plat, top, sizeof (top))` (~2209),
`atv14_probe_package (plat)` → `pkg_probe_logged (plat)` (~2390).
Проверить, что упоминаний `atv14_top_package`/`atv14_probe_package`/`parse_top_package` не осталось:

Run: `grep -n "atv14_top_package\|atv14_probe_package\|parse_top_package" afrd.c`
Ожидаемо: пусто.

- [ ] **Step 7: Собрать и прогнать всё**

Run: `sh tests/run_tests.sh && ./mka 2>&1 | tail -5`
Ожидаемо: 11 наборов зелёные, сборка без предупреждений.

- [ ] **Step 8: Коммит**

```bash
git add pkgdet.h pkgdet.c tests/test_pkgdet.c tests/run_tests.sh platform.c afrd.c
git commit -m "feat(daemon): общий разбор пакета на переднем плане (Android 6-14)"
```

---

### Task 2: `switch.skip.pkg` и `mode.min.hz.list` на старом пути

**Files:**
- Modify: `afrd.c` (кэш пакета, гейт, порог в выборе режима)
- Modify: `cfgkeys.c`, `tests/test_cfgkeys.c`
- Modify: `config/afrd-android7-.ini`, `config/afrd-android8+.ini`, `config/afrd-minix7-.ini`
- Modify: `README.md`, `ChangeLog-ru.txt`, `ChangeLog-en.txt`

**Interfaces:**
- Consumes: `pkg_probe()` (Task 1), `mode_min_hz_for()` (`afrd.c:3090`), `strlist_contains()` (`strfun.c:107`).
- Produces (в `afrd.c`, статические): `static const char *pkg_name (void)`,
  `static bool pkg_skip_active (void)`, `static bool select_mode (int floor, display_mode_t *best)`.

- [ ] **Step 1: Тест на применимость ключей (падающий)**

В `tests/test_cfgkeys.c` рядом с существующими проверками `CFGKEY_ATV14` добавить:

```c
	/* Перенесены на оба пути (политика аддитивности с переносом): «зажим»
	 * темпа плеером и определение переднего пакета есть и на Android 6-9. */
	assert (cfgkeys_note ("switch.skip.pkg", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("switch.skip.pkg", CFGKEY_LEGACY) == NULL);
	assert (cfgkeys_note ("mode.min.hz.list", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("mode.min.hz.list", CFGKEY_LEGACY) == NULL);
```

Run: `sh tests/run_tests.sh`
Ожидаемо: `test_cfgkeys` падает (на `CFGKEY_LEGACY` ключи сейчас дают примечание «не действует»).

- [ ] **Step 2: Ключи — на оба пути**

В `cfgkeys.c` перенести две строки из блока «только новый путь» в блок «общие для обоих путей»
(с сохранением алфавитного вида блока):

```c
	{ "mode.min.hz.list",		CFGKEY_BOTH, NULL },
	{ "switch.skip.pkg",		CFGKEY_BOTH, NULL },
```

Run: `sh tests/run_tests.sh`
Ожидаемо: все 11 наборов зелёные.

- [ ] **Step 3: Кэш пакета и проверка списка в `afrd.c`**

**Сначала поднять объявление `g_plat`.** Сейчас `static platform_t g_plat;` объявлен в `afrd.c:2833`,
глубже точки, где нужен (`pkg_name()` — около строки 1300, `framerate_switch()` — около 660).
Удалить строку 2833 и добавить объявление рядом с остальными статиками, после `static strlist_t g_skip_pkgs;`
(строка ~92):

```c
/* Текущий путь работы (заполняется в afrd_run); нужен и старому пути — по нему
 * pkg_name() понимает, откуда брать пакет переднего плана. */
static platform_t g_plat;
```

Проверить, что дубликата не осталось: `grep -c "^static platform_t g_plat;" afrd.c` → `1`.

Рядом с существующим объявлением вперёд `mode_min_hz_for` (`afrd.c:1109-1110`) добавить:

```c
/* Пакет плеера текущего видео и решение по switch.skip.pkg (определения ниже:
 * нужны framerate_switch/delay_framerate_switch, которые идут раньше). */
static const char *pkg_name (void);
static bool pkg_skip_active (void);
```

После `pkg_probe_logged()` (Task 1, шаг 6) добавить:

```c
/* Кэш пакета для старого пути: `dumpsys` — это fork/exec, поэтому не чаще
 * раза в PKG_CACHE_MS. Пока ни одного из списков по пакетам нет, не делается
 * ни одного вызова (у существующих пользователей Android 6-9 поведение
 * остаётся прежним). */
#define PKG_CACHE_MS	2000
static const char *pkg_name (void)
{
	if (g_plat.kind == PLAT_ATV14)
		return g_cur_pkg;

	static char buf [64];
	static mstime_t stamp;

	if ((g_skip_pkgs.size == 0) && (g_mode_min_hz_count == 0))
		return "";

	if (!mstime_running (&stamp))
	{
		mstime_arm (&stamp, PKG_CACHE_MS);
		if (pkg_probe (&g_plat, buf, sizeof (buf)) != 0)
			buf [0] = 0;
	}

	return buf;
}

/* true — текущему видео AFR мешать не надо (плеер в switch.skip.pkg). */
static bool pkg_skip_active (void)
{
	if (g_skip_pkgs.size == 0)
		return false;

	const char *pkg = pkg_name ();
	return (pkg [0] != 0) && strlist_contains (&g_skip_pkgs, pkg);
}
```

`g_plat` объявлен выше по файлу (`platform_t g_plat;`) — проверить, что виден в этой точке;
если нет — вынести объявление `pkg_name()`/`pkg_skip_active()` ниже `g_plat`.

- [ ] **Step 4: Гейт в `delay_framerate_switch()`**

В начало `delay_framerate_switch()` (перед блоком `if (g_switch_ignore)`) добавить:

```c
	/* switch.skip.pkg: этому плееру AFR мешает — режим не переключаем.
	 * Если что-то уже удержано (плеер внесли в список на ходу), возвращаем
	 * исходный режим. */
	if (!restore && pkg_skip_active ())
	{
		trace (1, "плеер %s в switch.skip.pkg — режим не переключаю\n", pkg_name ());
		if (g_state.orig_mode.name [0])
			framerate_restore (false);
		update_stats ();
		return;
	}
```

- [ ] **Step 5: Гейт в `framerate_switch()`**

В `framerate_switch()` после проверки `g_enable` (перед блоком `if ((g_state.hz == 0) && ...)`)
добавить:

```c
	/* switch.skip.pkg — вторая точка решения: сюда приходят и по таймеру,
	 * и от API, минуя delay_framerate_switch. */
	if (pkg_skip_active ())
	{
		trace (1, "плеер %s в switch.skip.pkg — режим не переключаю\n", pkg_name ());
		if (g_state.orig_mode.name [0])
			framerate_restore (false);
		else
		{
			memset (&g_state, 0, sizeof (g_state));
			update_stats ();
		}
		return;
	}
```

- [ ] **Step 6: `mode.min.hz.list` на старом пути**

Вынести выбор режима из `framerate_switch()` в отдельную функцию (тело переносится дословно,
добавляется только проверка порога). Над `framerate_switch()` вставить:

```c
/* Выбор режима из g_modes под частоту g_state.hz (правила старого пути).
 * floor — минимальная допустимая частота режима в формате 24.8
 * (mode.min.hz.list) или 0.
 * Возврат: true — режим найден и записан в *best. */
static bool select_mode (int floor, display_mode_t *best)
{
	unsigned best_rating = 0;
	best->name [0] = 0;
	for (int i = 0; i < g_modes_n; i++) {
		display_mode_t *mode = &g_modes [i];
		if ((mode->width != g_current_mode.width) ||
		    (mode->height != g_current_mode.height) ||
		    (mode->interlaced != g_current_mode.interlaced))
			continue;

		/* mode.min.hz.list: для этого плеера режимы ниже порога не годятся
		 * (плеер «зажимает» темп отрисовки по частоте экрана в момент старта
		 * ролика — нужен режим не ниже частоты ролика). */
		if (floor && (display_mode_hz (mode) < floor))
			continue;

		unsigned rate_n = 1;
		unsigned rate = (mode->framerate << 16) / g_state.hz;
		while (rate > 0x180) {
			rate_n++;
			rate = (mode->framerate << 16) / (g_state.hz * rate_n);
		}

		unsigned delta = abs ((int)(rate - 0x100));
		if (delta > 11)
			continue; // freq error > 4.3%

		// rating is larger as delta is closer to 1.0 rate
		int rating = (11 - delta) * 16;

		unsigned n = (rate_n > 3) ? 3 : (rate_n - 1);
		rating += 4 * (g_mode_prefer_exact ? (3 - n) : n);

		if (rating > best_rating) {
			display_mode_t tmp = *mode;
			// decide if integer or fractional framerate is better
			display_mode_set_hz (&tmp, g_state.hz);

			// if framerate is blacklisted, try to invert fractional
			if (rate_is_blacklisted (display_mode_hz (&tmp))) {
				tmp.fractional = !tmp.fractional;
				if (rate_is_blacklisted (display_mode_hz (&tmp)))
					// no luck, both framerates are banned
					continue;
			}

			best_rating = rating;
			*best = tmp;
		}
	}

	return best->name [0] != 0;
}
```

В `framerate_switch()` заменить блок от комментария `/* Find the video mode that:` и до конца
цикла включительно на вызов (комментарий про правила выбора перенести в `select_mode()`):

```c
	/* Порог частоты для плеера текущего видео (mode.min.hz.list); если под
	 * порогом не осталось ни одного режима, берём обычное правило. */
	int floor = g_mode_min_hz_count ? mode_min_hz_for (pkg_name ()) : 0;
	display_mode_t best_mode;
	if (!select_mode (floor, &best_mode) && floor)
	{
		trace (1, "нет режима не ниже " HZ_FMT " Гц (mode.min.hz.list) — "
		       "выбираю обычным правилом\n", HZ_ARGS (floor));
		select_mode (0, &best_mode);
	}
```

Блок `if (!best_mode.name [0]) { trace (1, "Failed to find a suitable display mode\n"); ... }`
оставить как есть — он теперь идёт сразу за этим кодом.

Run: `sh tests/run_tests.sh && ./mka 2>&1 | tail -5`
Ожидаемо: зелёно, без предупреждений. Прежний код выбора режима не должен измениться ни в одном
символе кроме вынесения и добавленной проверки порога — проверить, что при пустом
`mode.min.hz.list` `floor == 0` и решение совпадает с прежним.

- [ ] **Step 7: Профили старого пути**

В `config/afrd-android7-.ini`, `config/afrd-android8+.ini`, `config/afrd-minix7-.ini` добавить
(в конец, рядом с уже существующими пояснениями) — **закомментированной**, чтобы умолчание
осталось пустым:

```
# Приложения, которым AFR мешает (имена пакетов через пробел): демон измеряет
# частоту и пишет журнал, но режим для них не переключает и не удерживает.
# Значение задаётся в приложении AFRd2 выбором из списка приложений.
#switch.skip.pkg=com.google.android.youtube.tv
# Минимальная частота режима для плееров, которые «зажимают» темп отрисовки
# по частоте экрана: «пакет=Гц». Ниже порога режимы не выбираются.
#mode.min.hz.list=com.example.player=50
```

- [ ] **Step 8: Документация**

`README.md`: в таблицу ключей (строка ~107-108) у `switch.skip.pkg` и `mode.min.hz.list` убрать
привязку к Android 10+ и дописать, что ключи действуют на обоих путях; в ограничении 8 (строка ~526)
добавить, что на Android 6–9 список работает так же, а определение плеера делается по
`dumpsys activity` (кэш 2 с, только когда список непуст).
`ChangeLog-ru.txt` и `ChangeLog-en.txt`: запись в начало файла (RU и EN, синхронно):

```
  * switch.skip.pkg и mode.min.hz.list действуют и на старом пути (Android 6-9):
    пакет приложения на переднем плане определяется общим разбором дампа
    активности, гейт стоит в обеих точках решения о переключении.
```

- [ ] **Step 9: Коммит**

```bash
cp FAQ-ru.txt android/app/src/main/res/raw-ru/faq.txt
cp FAQ-en.txt android/app/src/main/res/raw/faq.txt
git add afrd.c cfgkeys.c tests/test_cfgkeys.c README.md ChangeLog-ru.txt ChangeLog-en.txt config/*.ini
git commit -m "feat(legacy): switch.skip.pkg и mode.min.hz.list действуют на Android 6-9"
```

---

### Task 3: Экран выбора приложений (APK), строка `switch.skip.pkg`

**Files:**
- Create: `android/app/src/main/java/ru/cobra/zap/xprefs/XAppsPreference.java`
- Create: `android/app/src/main/java/ru/cobra/zap/afrd/Apps.java`
- Create: `android/app/src/main/java/ru/cobra/zap/afrd/gui/AppsPickerActivity.java`
- Create: `android/app/src/main/res/layout/activity_apps_picker.xml`, `item_app.xml`
- Modify: `android/app/src/main/java/ru/cobra/zap/afrd/gui/SettingsFragment.java`
- Modify: `android/app/src/main/java/ru/cobra/zap/afrd/gui/MainActivity.java`, `jfun.java`
- Modify: `android/app/src/main/java/ru/cobra/zap/afrd/Config.java`
- Modify: `android/app/src/main/res/xml/preferences.xml`, `AndroidManifest.xml`,
  `res/values/strings.xml`, `res/values-ru/strings.xml`
- Modify: `README.md`, `ChangeLog-ru.txt`, `ChangeLog-en.txt`

**Interfaces:**
- Produces (Java): `XAppsPreference` с `getApps()/setApps(String)/setPickListener(OnPickListener)`;
  `Apps.split/join/names/label/icon/videoPlayers/allApps/app/indexOf` и `Apps.App{pkg,label,installed}`;
  `AppsPickerActivity` с extras `key/title/mode/value/filter` и результатом `value`, константами
  `MODE_MULTI`/`MODE_SINGLE`, статическим `modeFor(String prefKey)`.
- Consumes: `MainActivity.applyPrefs()`, `Control.isAtv14()`, `R.layout.pref_value` (не используется —
  summary ставится текстом).

- [ ] **Step 1: Строка настройки `XAppsPreference`**

Создать `android/app/src/main/java/ru/cobra/zap/xprefs/XAppsPreference.java`:

```java
package ru.cobra.zap.xprefs;

import android.content.Context;
import android.preference.Preference;
import android.util.AttributeSet;

/**
 * Настройка, значение которой — имена пакетов приложений (switch.skip.pkg,
 * fps.correct.av1.pkg). Ввод текста с пульта для таких значений непригоден,
 * поэтому нажатие не открывает диалог, а передаётся хосту: он открывает
 * экран выбора приложений (см. gui.AppsPickerActivity).
 */
public class XAppsPreference extends Preference
{
    /** Хост открывает экран выбора для этой настройки. */
    public interface OnPickListener
    {
        void onPickApps (XAppsPreference pref);
    }

    private OnPickListener mListener;

    public XAppsPreference (Context context)
    {
        this (context, null);
    }

    public XAppsPreference (Context context, AttributeSet attrs)
    {
        super (context, attrs);
    }

    public void setPickListener (OnPickListener listener)
    {
        mListener = listener;
    }

    @Override
    protected void onClick ()
    {
        if (mListener != null)
            mListener.onPickApps (this);
    }

    /** Текущее значение в формате ini (пакеты через пробел). */
    public String getApps ()
    {
        return getPersistedString ("");
    }

    public void setApps (String value, boolean notify)
    {
        persistString (value == null ? "" : value);
        if (notify)
            notifyChanged ();
    }
}
```

- [ ] **Step 2: Работа с приложениями (`Apps.java`)**

Создать `android/app/src/main/java/ru/cobra/zap/afrd/Apps.java`:

```java
/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.drawable.Drawable;

import java.text.Collator;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

/**
 * Список приложений для настроек, значения которых — имена пакетов
 * (switch.skip.pkg, fps.correct.av1.pkg). Значение настройки — пакеты через
 * пробел, ровно в том виде, в каком его читает демон (strlist_load).
 */
public class Apps
{
    /** Строка списка выбора. */
    public static class App
    {
        public final String pkg;
        public final CharSequence label;
        public final boolean installed;

        App (String pkg, CharSequence label, boolean installed)
        {
            this.pkg = pkg;
            this.label = label;
            this.installed = installed;
        }
    }

    /* Типы, которые спрашиваем у системы. Одного "video/*" мало: интент с типом
     * video/* не совпадает с фильтром, объявившим video/mp4 (совпадение идёт по
     * точному типу или по wildcard в фильтре), поэтому перечисляем и частные. */
    private static final String [] VIDEO_MIME =
    {
        "video/*", "video/mp4", "video/x-matroska", "video/webm", "video/x-msvideo",
        "video/mpeg", "video/quicktime", "video/mp2t", "video/x-flv", "video/3gpp",
        "application/x-mpegURL", "application/vnd.apple.mpegurl",
    };

    /** Пакеты из значения настройки. */
    public static List<String> split (String value)
    {
        List<String> out = new ArrayList<> ();
        if (value == null)
            return out;

        for (String s : value.trim ().split ("\\s+"))
            if (!s.isEmpty () && !out.contains (s))
                out.add (s);

        return out;
    }

    /** Значение настройки из списка пакетов. */
    public static String join (List<String> pkgs)
    {
        StringBuilder sb = new StringBuilder ();
        for (String s : pkgs)
        {
            if (s == null || s.isEmpty ())
                continue;
            if (sb.length () > 0)
                sb.append (' ');
            sb.append (s);
        }
        return sb.toString ();
    }

    /** Имя приложения или сам пакет, если приложение не установлено. */
    public static CharSequence label (Context ctx, String pkg)
    {
        PackageManager pm = ctx.getPackageManager ();
        try
        {
            return pm.getApplicationInfo (pkg, 0).loadLabel (pm);
        }
        catch (PackageManager.NameNotFoundException e)
        {
            return pkg;
        }
    }

    /** Иконка приложения (null — приложение не установлено). */
    public static Drawable icon (Context ctx, String pkg)
    {
        PackageManager pm = ctx.getPackageManager ();
        try
        {
            return pm.getApplicationInfo (pkg, 0).loadIcon (pm);
        }
        catch (PackageManager.NameNotFoundException e)
        {
            return null;
        }
    }

    /** Имена приложений для строки настроек: "YouTube TV, VLC". */
    public static String names (Context ctx, String value)
    {
        List<String> pkgs = split (value);
        StringBuilder sb = new StringBuilder ();
        for (String pkg : pkgs)
        {
            if (sb.length () > 0)
                sb.append (", ");
            sb.append (label (ctx, pkg));
        }
        return sb.toString ();
    }

    /** Приложения, умеющие открывать видео. */
    public static List<App> videoPlayers (Context ctx)
    {
        PackageManager pm = ctx.getPackageManager ();
        List<String> pkgs = new ArrayList<> ();

        for (String mime : VIDEO_MIME)
        {
            Intent in = new Intent (Intent.ACTION_VIEW);
            in.setType (mime);
            List<ResolveInfo> found = pm.queryIntentActivities (in, 0);
            if (found == null)
                continue;

            for (ResolveInfo ri : found)
                if ((ri.activityInfo != null) &&
                    !pkgs.contains (ri.activityInfo.packageName))
                    pkgs.add (ri.activityInfo.packageName);
        }

        return build (ctx, pkgs);
    }

    /** Все установленные приложения, которые можно запустить. */
    public static List<App> allApps (Context ctx)
    {
        PackageManager pm = ctx.getPackageManager ();
        List<String> pkgs = new ArrayList<> ();

        for (ApplicationInfo ai : pm.getInstalledApplications (0))
            pkgs.add (ai.packageName);

        return build (ctx, pkgs);
    }

    /** Строка для одного пакета (в том числе удалённого). */
    public static App app (Context ctx, String pkg)
    {
        try
        {
            ApplicationInfo ai = ctx.getPackageManager ().getApplicationInfo (pkg, 0);
            return new App (pkg, ai.loadLabel (ctx.getPackageManager ()), true);
        }
        catch (PackageManager.NameNotFoundException e)
        {
            return new App (pkg, pkg, false);
        }
    }

    /** Индекс пакета в списке или -1. */
    public static int indexOf (List<App> list, String pkg)
    {
        for (int i = 0; i < list.size (); i++)
            if (list.get (i).pkg.equals (pkg))
                return i;

        return -1;
    }

    /* Отсеять не запускаемые приложения (на переднем плане такое оказаться не
     * может) и сам AFRd2, отсортировать по локализованному имени. Иконки здесь
     * не грузятся: их берёт адаптер лениво, только для видимых строк. */
    private static List<App> build (Context ctx, List<String> pkgs)
    {
        PackageManager pm = ctx.getPackageManager ();
        String self = ctx.getPackageName ();
        List<App> out = new ArrayList<> ();

        for (String pkg : pkgs)
        {
            if (pkg.equals (self) || (pm.getLaunchIntentForPackage (pkg) == null))
                continue;

            try
            {
                ApplicationInfo ai = pm.getApplicationInfo (pkg, 0);
                out.add (new App (pkg, ai.loadLabel (pm), true));
            }
            catch (PackageManager.NameNotFoundException ignored)
            {
            }
        }

        final Collator coll = Collator.getInstance ();
        Collections.sort (out, new Comparator<App> ()
        {
            @Override
            public int compare (App a, App b)
            {
                return coll.compare (a.label.toString (), b.label.toString ());
            }
        });

        return out;
    }
}
```

- [ ] **Step 3: Экран выбора**

Создать `android/app/src/main/res/layout/activity_apps_picker.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<LinearLayout xmlns:android="http://schemas.android.com/apk/res/android"
              android:layout_width="match_parent" android:layout_height="match_parent"
              android:orientation="vertical">

    <LinearLayout android:layout_width="match_parent" android:layout_height="40sp"
                  android:background="@color/colorMenuBackground" android:orientation="horizontal">
        <TextView android:id="@+id/pickerTitle" android:layout_width="0dp"
                  android:layout_height="match_parent" android:layout_weight="1"
                  android:gravity="start|center_vertical" android:paddingStart="16dp"
                  android:paddingEnd="16dp" android:textSize="18sp"/>
        <CheckBox android:id="@+id/pickerPlayers" android:layout_width="wrap_content"
                  android:layout_height="match_parent" android:layout_marginEnd="16dp"
                  android:text="@string/apps_players_only"/>
        <Button android:id="@+id/pickerDone" android:layout_width="wrap_content"
                android:layout_height="wrap_content" android:layout_marginEnd="16dp"
                android:text="@string/apps_done"/>
    </LinearLayout>

    <FrameLayout android:layout_width="match_parent" android:layout_height="match_parent">
        <ListView android:id="@+id/pickerList" android:layout_width="match_parent"
                  android:layout_height="match_parent"/>
        <TextView android:id="@+id/pickerEmpty" android:layout_width="match_parent"
                  android:layout_height="wrap_content" android:gravity="center"
                  android:paddingStart="16dp" android:paddingTop="24dp" android:paddingEnd="16dp"
                  android:text="@string/apps_empty" android:textSize="18sp"
                  android:visibility="gone"/>
    </FrameLayout>

</LinearLayout>
```

Создать `android/app/src/main/res/layout/item_app.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<LinearLayout xmlns:android="http://schemas.android.com/apk/res/android"
              android:layout_width="match_parent" android:layout_height="wrap_content"
              android:gravity="center_vertical" android:orientation="horizontal"
              android:paddingStart="16dp" android:paddingTop="8dp" android:paddingEnd="16dp"
              android:paddingBottom="8dp">

    <ImageView android:id="@+id/appIcon" android:layout_width="48dp" android:layout_height="48dp"
               android:contentDescription="@string/apps_icon"/>

    <LinearLayout android:layout_width="0dp" android:layout_height="wrap_content"
                  android:layout_marginStart="16dp" android:layout_weight="1"
                  android:orientation="vertical">
        <TextView android:id="@+id/appLabel" android:layout_width="match_parent"
                  android:layout_height="wrap_content" android:textSize="18sp"/>
        <TextView android:id="@+id/appPkg" android:layout_width="match_parent"
                  android:layout_height="wrap_content" android:textSize="12sp"
                  android:typeface="monospace"/>
    </LinearLayout>

    <CheckBox android:id="@+id/appCheck" android:layout_width="wrap_content"
              android:layout_height="wrap_content" android:clickable="false"
              android:focusable="false"/>
</LinearLayout>
```

Создать `android/app/src/main/java/ru/cobra/zap/afrd/gui/AppsPickerActivity.java`:

```java
/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd.gui;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.drawable.Drawable;
import android.os.AsyncTask;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.ImageView;
import android.widget.ListView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;

import ru.cobra.zap.afrd.Apps;
import ru.cobra.zap.afrd.jfun;

/**
 * Полноэкранный выбор приложений для настроек-списков пакетов.
 *
 * Экран ничего не пишет в конфиг: он возвращает новое значение хост-фрагменту
 * (SettingsFragment), а тот применяет его обычным путём (applyPrefs). Поэтому
 * экран переиспользуется любым числом настроек — сейчас это switch.skip.pkg
 * (режим multi) и fps.correct.av1.pkg (режим single).
 */
public class AppsPickerActivity extends Activity
{
    public static final String EXTRA_KEY = "key";
    public static final String EXTRA_TITLE = "title";
    public static final String EXTRA_MODE = "mode";
    public static final String EXTRA_VALUE = "value";
    public static final String EXTRA_FILTER = "filter";
    public static final String RESULT_VALUE = "value";

    public static final String MODE_MULTI = "multi";
    public static final String MODE_SINGLE = "single";

    private String mKey;
    private String mMode = MODE_MULTI;
    private boolean mPlayersOnly = true;
    private final LinkedHashSet<String> mSelected = new LinkedHashSet<> ();
    private final List<Apps.App> mApps = new ArrayList<> ();
    private AppsAdapter mAdapter;
    private AsyncTask<Void, Void, Void> mLoader;

    @Override
    protected void onCreate (Bundle savedInstanceState)
    {
        super.onCreate (savedInstanceState);

        jfun.limitDpi (this);
        setContentView (R.layout.activity_apps_picker);

        Intent in = getIntent ();
        mKey = in.getStringExtra (EXTRA_KEY);
        String mode = in.getStringExtra (EXTRA_MODE);
        if (MODE_SINGLE.equals (mode))
            mMode = MODE_SINGLE;
        mPlayersOnly = !"all".equals (in.getStringExtra (EXTRA_FILTER));
        mSelected.addAll (Apps.split (in.getStringExtra (EXTRA_VALUE)));

        String title = in.getStringExtra (EXTRA_TITLE);
        ((TextView) findViewById (R.id.pickerTitle)).setText (title == null ? "" : title);

        CheckBox players = findViewById (R.id.pickerPlayers);
        players.setChecked (mPlayersOnly);
        players.setOnCheckedChangeListener (new CompoundButton.OnCheckedChangeListener ()
        {
            @Override
            public void onCheckedChanged (CompoundButton button, boolean checked)
            {
                if (mPlayersOnly == checked)
                    return;

                mPlayersOnly = checked;
                load ();
            }
        });

        findViewById (R.id.pickerDone).setOnClickListener (new View.OnClickListener ()
        {
            @Override
            public void onClick (View v)
            {
                finishWithResult ();
            }
        });

        ListView list = findViewById (R.id.pickerList);
        mAdapter = new AppsAdapter (this, mApps, mSelected);
        list.setAdapter (mAdapter);
        list.setOnItemClickListener (new AdapterView.OnItemClickListener ()
        {
            @Override
            public void onItemClick (AdapterView<?> parent, View view, int position, long id)
            {
                String pkg = mApps.get (position).pkg;
                /* single: отметка ровно одна; повторное нажатие снимает выбор */
                if (MODE_SINGLE.equals (mMode))
                    mSelected.clear ();
                if (!mSelected.remove (pkg))
                    mSelected.add (pkg);

                mAdapter.notifyDataSetChanged ();
            }
        });

        load ();
    }

    /* Загрузка списка — в фоне: на ATV это десятки-сотни пакетов, а запрос
     * списка и загрузка имён идут через PackageManager. */
    private void load ()
    {
        if (mLoader != null)
            mLoader.cancel (true);

        mLoader = new AsyncTask<Void, Void, Void> ()
        {
            @Override
            protected Void doInBackground (Void... args)
            {
                final List<Apps.App> list = mPlayersOnly
                    ? Apps.videoPlayers (AppsPickerActivity.this)
                    : Apps.allApps (AppsPickerActivity.this);

                runOnUiThread (new Runnable ()
                {
                    @Override
                    public void run ()
                    {
                        showList (list);
                    }
                });
                return null;
            }
        }.execute ();
    }

    private void showList (List<Apps.App> list)
    {
        if (isFinishing ())
            return;

        /* Уже выбранные пакеты показываем всегда, даже если фильтр их отсеял:
         * приложение могло перестать быть плеером или быть удалено, но снять
         * отметку пользователь должен иметь возможность. */
        List<String> selected = new ArrayList<> (mSelected);
        for (String pkg : selected)
            if (Apps.indexOf (list, pkg) < 0)
                list.add (Apps.app (this, pkg));

        mApps.clear ();
        mApps.addAll (list);
        mAdapter.notifyDataSetChanged ();

        findViewById (R.id.pickerEmpty).setVisibility (mApps.isEmpty () ? View.VISIBLE : View.GONE);
    }

    /* На пульте Back ожидаемо означает «готово»: отдельного «отменить» у нас
     * нет, а потерять выбор хуже, чем сохранить его без подтверждения. */
    @Override
    public void onBackPressed ()
    {
        finishWithResult ();
    }

    private void finishWithResult ()
    {
        Intent out = new Intent ();
        out.putExtra (EXTRA_KEY, mKey);
        if (MODE_SINGLE.equals (mMode))
            out.putExtra (RESULT_VALUE, mSelected.isEmpty () ? "" : mSelected.iterator ().next ());
        else
            out.putExtra (RESULT_VALUE, Apps.join (new ArrayList<> (mSelected)));

        setResult (RESULT_OK, out);
        finish ();
    }

    /** Режим выбора для ключа настройки. */
    public static String modeFor (String prefKey)
    {
        return "fps_correct_av1_pkg".equals (prefKey) ? MODE_SINGLE : MODE_MULTI;
    }

    private static class AppsAdapter extends BaseAdapter
    {
        private final Activity mCtx;
        private final List<Apps.App> mApps;
        private final LinkedHashSet<String> mSelected;
        private final Map<String, Drawable> mIcons = new HashMap<> ();

        AppsAdapter (Activity ctx, List<Apps.App> apps, LinkedHashSet<String> selected)
        {
            mCtx = ctx;
            mApps = apps;
            mSelected = selected;
        }

        @Override
        public int getCount ()
        {
            return mApps.size ();
        }

        @Override
        public Object getItem (int position)
        {
            return mApps.get (position);
        }

        @Override
        public long getItemId (int position)
        {
            return position;
        }

        @Override
        public View getView (int position, View convertView, ViewGroup parent)
        {
            View v = convertView;
            if (v == null)
                v = mCtx.getLayoutInflater ().inflate (R.layout.item_app, parent, false);

            Apps.App app = mApps.get (position);

            ((TextView) v.findViewById (R.id.appLabel)).setText (app.label);
            ((TextView) v.findViewById (R.id.appPkg)).setText (app.installed ? app.pkg
                : mCtx.getString (R.string.apps_not_installed, app.pkg));
            ((CheckBox) v.findViewById (R.id.appCheck)).setChecked (mSelected.contains (app.pkg));

            ImageView iv = v.findViewById (R.id.appIcon);
            Drawable icon = mIcons.get (app.pkg);
            if (icon == null)
            {
                icon = Apps.icon (mCtx, app.pkg);
                if (icon == null)
                    icon = mCtx.getResources ().getDrawable (android.R.drawable.sym_def_app_icon);
                mIcons.put (app.pkg, icon);
            }
            iv.setImageDrawable (icon);

            return v;
        }
    }
}
```

- [ ] **Step 4: `limitDpi` в утилиту**

В `android/app/src/main/java/ru/cobra/zap/afrd/jfun.java` добавить статический метод (тело
переносится из `MainActivity.limitDpi()` дословно, `this` → `act`):

```java
    /**
     * Не дать пользовательскому DPI/масштабу шрифта сделать интерфейс
     * нечитаемым (телефонные значения на ТВ-боксе).
     */
    @SuppressWarnings( "deprecation" )
    public static void limitDpi (android.app.Activity act)
    {
        android.content.res.Configuration configuration = act.getResources ().getConfiguration ();

        if ((configuration.densityDpi <= 240) &&
            (configuration.fontScale <= 1.30F))
            return;

        if (configuration.densityDpi > 340)
            configuration.densityDpi = 340;
        if (configuration.fontScale > 1.30F)
            configuration.fontScale = 1.30F;

        android.util.DisplayMetrics metrics = act.getResources ().getDisplayMetrics ();
        android.view.WindowManager wm =
            (android.view.WindowManager) act.getSystemService (android.content.Context.WINDOW_SERVICE);
        if (wm == null)
            return;

        android.view.Display dpy = wm.getDefaultDisplay ();
        dpy.getMetrics (metrics);
        metrics.scaledDensity = configuration.fontScale * metrics.density;
        act.getBaseContext ().getResources ().updateConfiguration (configuration, metrics);
    }
```

В `MainActivity` удалить метод `limitDpi()` целиком и заменить вызов в `onCreate()`:

```java
        jfun.limitDpi (this);
```

- [ ] **Step 5: Регистрация экрана и строки**

В `AndroidManifest.xml` добавить в `<application>` после `ActionActivity`:

```xml
        <activity android:name="ru.cobra.zap.afrd.gui.AppsPickerActivity"
                  android:configChanges="orientation" android:exported="false"
                  android:screenOrientation="landscape"/>
```

В `res/values/strings.xml` (EN):

```xml
    <string name="prefs_switch_skip_pkg">Players that AFR gets in the way of</string>
    <string name="prefsum_switch_skip_pkg">For the selected applications AFRd2 measures the frame
        rate and writes everything to the log, but does not switch or hold the display mode. Tap
        the row and pick applications from the list of installed players.</string>
    <string name="apps_done">Done</string>
    <string name="apps_players_only">Video players only</string>
    <string name="apps_empty">No applications found — clear "Video players only" to see all</string>
    <string name="apps_not_installed">%s — not installed</string>
    <string name="apps_icon">Application icon</string>
```

В `res/values-ru/strings.xml` (RU):

```xml
    <string name="prefs_switch_skip_pkg">Плееры, которым AFR мешает</string>
    <string name="prefsum_switch_skip_pkg">Для выбранных приложений AFRd2 измеряет частоту кадров
        и пишет всё в журнал, но не переключает и не удерживает режим экрана. Коснитесь строки и
        выберите приложения из списка установленных плееров.</string>
    <string name="apps_done">Готово</string>
    <string name="apps_players_only">Только плееры</string>
    <string name="apps_empty">Приложения не найдены — снимите «Только плееры», чтобы увидеть все</string>
    <string name="apps_not_installed">%s — не установлено</string>
    <string name="apps_icon">Значок приложения</string>
```

В `res/xml/preferences.xml` в `PreferenceScreen android:key="atv14"` добавить **первой** строкой
(перед `fps_prefer_api`):

```xml
        <ru.cobra.zap.xprefs.XAppsPreference android:key="switch_skip_pkg"
                          android:summary="@string/prefsum_switch_skip_pkg"
                          android:title="@string/prefs_switch_skip_pkg"/>
```

- [ ] **Step 6: Ключ в `Config.itemType`**

В `android/app/src/main/java/ru/cobra/zap/afrd/Config.java` после
`itemType.put ("mode.blacklist.rates", TYPE_STR);` добавить:

```java
        itemType.put ("switch.skip.pkg", TYPE_STR);
```

- [ ] **Step 7: Подключение в `SettingsFragment`**

В `SettingsFragment.java` добавить импорты `android.content.Intent`,
`ru.cobra.zap.afrd.Apps`, `ru.cobra.zap.xprefs.XAppsPreference` и поля/методы:

```java
    /* Настройки-списки приложений: ключ настройки → режим выбора. Значение
     * таких ключей — имена пакетов, их задают выбором из списка установленных
     * приложений, а не вводом с пульта. Вторая строка (fps_correct_av1_pkg)
     * добавляется, когда эта настройка переводится на выборщик. */
    private static final String [][] kAppsPrefs =
    {
        { "switch_skip_pkg", AppsPickerActivity.MODE_MULTI },
    };

    private static final int REQ_APPS = 1001;

    private static String appsMode (String key)
    {
        for (String [] item : kAppsPrefs)
            if (item [0].equals (key))
                return item [1];

        return AppsPickerActivity.MODE_MULTI;
    }

    private void setupAppsPrefs ()
    {
        for (String [] item : kAppsPrefs)
        {
            Preference p = findPreference (item [0]);
            if (!(p instanceof XAppsPreference))
                continue;

            final XAppsPreference pref = (XAppsPreference) p;
            pref.setPickListener (new XAppsPreference.OnPickListener ()
            {
                @Override
                public void onPickApps (XAppsPreference p)
                {
                    Intent in = new Intent (getActivity (), AppsPickerActivity.class);
                    in.putExtra (AppsPickerActivity.EXTRA_KEY, p.getKey ());
                    in.putExtra (AppsPickerActivity.EXTRA_TITLE, p.getTitle ());
                    in.putExtra (AppsPickerActivity.EXTRA_MODE, appsMode (p.getKey ()));
                    in.putExtra (AppsPickerActivity.EXTRA_VALUE, p.getApps ());
                    startActivityForResult (in, REQ_APPS);
                }
            });

            setAppsSummary (pref);
        }
    }

    private void setAppsSummary (XAppsPreference pref)
    {
        pref.setSummary (Apps.names (getActivity (), pref.getApps ()));
    }

    @Override
    public void onActivityResult (int requestCode, int resultCode, Intent data)
    {
        super.onActivityResult (requestCode, resultCode, data);

        if ((requestCode != REQ_APPS) || (resultCode != Activity.RESULT_OK) || (data == null))
            return;

        Preference p = findPreference (data.getStringExtra (AppsPickerActivity.EXTRA_KEY));
        if (!(p instanceof XAppsPreference))
            return;

        XAppsPreference pref = (XAppsPreference) p;
        pref.setApps (data.getStringExtra (AppsPickerActivity.RESULT_VALUE), false);
        setAppsSummary (pref);
        pref.notifyChanged ();

        /* Настройки применяются сразу (как у остальных строк) — демон
         * перечитывает конфиг по изменению файла. */
        if (mMain != null)
            mMain.applyPrefs ();
    }
```

Импорт `android.app.Activity` — для `Activity.RESULT_OK` (проверить, что не конфликтует с
`android.app.Fragment`). В `onCreate()` после `markInapplicable (getPreferenceScreen ());`
(внутри блока `try` с `addPreferencesFromResource`) вызвать `setupAppsPrefs ();` — **после** того,
как экран добавлен, то есть сразу за `addPreferencesFromResource (R.xml.preferences);` и **до**
`break;`:

```java
                addPreferencesFromResource (R.xml.preferences);
                markInapplicable (getPreferenceScreen ());
                setupAppsPrefs ();
                break;
```

- [ ] **Step 8: Сев новых ключей из профиля (`MainActivity`)**

В `MainActivity.updateConfig()` в ветке, где настройки уже инициализированы, после
`conf.load (mOptions);` добавить:

```java
            /* Ключи, появившиеся в GUI позже установки приложения, в настройках
             * отсутствуют: без этого строка показала бы пустое значение, а
             * первая же правка настроек записала бы пустое значение поверх
             * профильного умолчания (switch.skip.pkg потерял бы исключение
             * для YouTube TV). */
            seedOptions (conf);
```

и сам метод (рядом с `updateConfig`):

```java
    /* Настройки, которых ещё нет в ini_options, берём из конфига профиля. */
    private void seedOptions (Config conf)
    {
        final String [] seeded = { "switch.skip.pkg", "fps.correct.av1.pkg" };
        SharedPreferences.Editor edit = null;

        for (String key : seeded)
        {
            String pref = key.replace ('.', '_');
            if (mOptions.contains (pref))
                continue;

            if (edit == null)
                edit = mOptions.edit ();
            edit.putString (pref, conf.getString (key, ""));
        }

        if (edit != null)
            edit.apply ();
    }
```

- [ ] **Step 9: Собрать и проверить, что приложение запускается**

Run:
```bash
cd android && JAVA_HOME=/usr/lib/jvm/java-11-openjdk-amd64 ./gradlew --offline assembleDebug
```
Ожидаемо: `BUILD SUCCESSFUL`, предупреждений компилятора нет.

- [ ] **Step 10: Коммит**

```bash
git add android/app/src/main/java android/app/src/main/res android/app/src/main/AndroidManifest.xml
git commit -m "feat(app): экран выбора приложений; switch.skip.pkg задаётся из списка"
```

---

### Task 4: Переиспользование выборщика для `fps.correct.av1.pkg` и пометка ATV14-ключей на legacy

**Files:**
- Modify: `android/app/src/main/res/xml/preferences.xml` (строка `fps_correct_av1_pkg`)
- Modify: `android/app/src/main/java/ru/cobra/zap/afrd/gui/SettingsFragment.java` (`kInactiveOnLegacy`)
- Modify: `android/app/src/main/res/values/strings.xml`, `res/values-ru/strings.xml`
- Modify: `README.md`, `ChangeLog-ru.txt`, `ChangeLog-en.txt`, `FAQ-ru.txt`, `FAQ-en.txt`

- [ ] **Step 1: Строка AV1-пакета — выборщик в режиме single**

В `preferences.xml` заменить описание `fps_correct_av1_pkg` на:

```xml
        <ru.cobra.zap.xprefs.XAppsPreference android:key="fps_correct_av1_pkg"
                          android:summary="@string/prefsum_fps_correct_av1_pkg"
                          android:title="@string/prefs_fps_correct_av1_pkg"/>
```

В `SettingsFragment.java` добавить вторую строку в `kAppsPrefs`:

```java
    private static final String [][] kAppsPrefs =
    {
        { "switch_skip_pkg", AppsPickerActivity.MODE_MULTI },
        { "fps_correct_av1_pkg", AppsPickerActivity.MODE_SINGLE },
    };
```

- [ ] **Step 2: Пометка недействующих на legacy ключей**

В `SettingsFragment.java` рядом с `kInactiveOnAtv14` добавить:

```java
    /* Настройки, которые демон объявляет ключами нового пути (CFGKEY_ATV14) и
     * потому на Android 6-9 не читает. switch.skip.pkg здесь нет: он действует
     * на обоих путях (перенос 2026-10-06). */
    private static final String [] kInactiveOnLegacy =
    {
        "fps_correct_enable",
        "fps_correct_list",
        "fps_correct_av1_offset",
        "fps_correct_av1_pkg",
    };

    private static boolean isInactiveLegacy (String key)
    {
        if (Control.isAtv14 ())
            return false;

        for (String k : kInactiveOnLegacy)
            if (k.equals (key))
                return true;

        return false;
    }
```

В `markInapplicable()` (или там, где вызывается `isInactive`) добавить вторую ветку: если
`isInactiveLegacy (p.getKey ())` — так же отключить и дописать
`getString (R.string.not_applicable_legacy)`; строку в оба файла строк:

```xml
    <string name="not_applicable_legacy">(не действует на Android 6-9)</string>
```
```xml
    <string name="not_applicable_legacy">(does not work on Android 6-9)</string>
```

- [ ] **Step 3: Документация и ЧАВО**

`README.md`: в разделе про экран выбора (или в таблице ключей) описать, что значения-списки
пакетов задаются в приложении выбором из списка приложений (фильтр «Только плееры», переключатель
«Все приложения»); `fps.correct.av1.pkg` выбирается тем же экраном (режим одиночного выбора).
`ChangeLog-ru.txt`/`ChangeLog-en.txt` — запись про экран выбора и про пометки legacy.
`FAQ-ru.txt`/`FAQ-en.txt`: в новом пункте про АПЧ плеера (раздел о конфликте) заменить фразу
«добавьте его в чёрный список AFRd» на «добавьте его в список «Плееры, которым AFR мешает»
(Настройки → Настройки для Android 10 и выше)»; в EN — соответственно. После правки обновить
копии в `res/raw`:

```bash
cp FAQ-ru.txt android/app/src/main/res/raw-ru/faq.txt
cp FAQ-en.txt android/app/src/main/res/raw/faq.txt
```

- [ ] **Step 4: Сборка и коммит**

Run: `cd android && JAVA_HOME=/usr/lib/jvm/java-11-openjdk-amd64 ./gradlew --offline assembleDebug`
Ожидаемо: `BUILD SUCCESSFUL` без предупреждений.

```bash
git add -A android/app/src/main FAQ-ru.txt FAQ-en.txt README.md ChangeLog-ru.txt ChangeLog-en.txt
git commit -m "feat(app): выборщик приложений для поправки AV1; пометка legacy-недействующих ключей"
```

---

### Task 5: Живая приёмка на двух стендах (выполняет контроллер, не субагент)

**Files:** нет (проверка; артефакты — лог бокса и записи в журнале).

Приёмка требует согласия владельца: перезагрузка боксов включает ТВ по CEC (§13.15), на `.76`
работает его upstream-AFRd (§27.1). Все шаги — по одному вызову adb; эксклюзивный доступ к `.76`
получить и вернуть по §27.1.

- [ ] **Step 1: Стенд `.50` (ATV14) — регресс демона**

```bash
adb -s 192.168.88.50:5555 push libs/armeabi-v7a/afrd /data/local/tmp/afrd_pk
adb -s 192.168.88.50:5555 shell 'chmod 755 /data/local/tmp/afrd_pk; /data/local/tmp/afrd_pk -k -l /data/local/tmp/afrd_pk.log -p /data/local/tmp/afrd_pk.pid -D /data/local/tmp/afrd.ini'
```
Ожидаемо: в логе `пакеты: no mode switching` (строка загрузки списка) и `+ com.google.android.youtube.tv`;
`atv14`-путь без изменений: на 24-фпс клипе в VLC режим переключается, для YouTube — нет.
Проверить, что определение пакета на ATV14 не сломалось унификацией команды:

```bash
adb -s 192.168.88.50:5555 shell 'dumpsys activity activities | grep -m1 ResumedActivity'
adb -s 192.168.88.50:5555 shell 'grep -n "видео ведёт приложение\|app на переднем плане" /data/local/tmp/afrd_pk.log | tail -3'
```
Ожидаемо: имя пакета в логе совпадает с выводом команды.

- [ ] **Step 2: Стенд `.76` (ATV9) — пустое умолчание не меняет поведение**

```bash
adb -s 192.168.88.76:5555 shell 'am stopservice -n ru.cobra.zap.afrd/.gui.AFRService'
adb -s 192.168.88.76:5555 push libs/armeabi-v7a/afrd /data/local/tmp/afrd9b
# профиль: config/afrd-android8+.ini с log.file=/data/local/tmp/afrd9b.log и своим pid-файлом
adb -s 192.168.88.76:5555 shell '/data/local/tmp/afrd9b -k -D /data/local/tmp/afrd9b.ini'
```
Ожидаемо: клип 25 fps в VLC переключает режим как раньше; в логе **нет** строк
`приложение на переднем плане` и `switch.skip.pkg`; `dumpsys` не запускался (проверить по времени
CPU процесса). Откат: `am start-foreground-service -n ru.cobra.zap.afrd/.gui.AFRService`.

- [ ] **Step 3: Стенд `.76` — список действует**

Добавить пакет плеера в `/data/local/tmp/afrd9b.ini` (`switch.skip.pkg=<пакет VLC>`), перезапустить
демон, запустить клип.
Ожидаемо: в логе `плеер <пакет> в switch.skip.pkg — режим не переключаю`, режим не меняется;
после удаления ключа из конфига и перезапуска — переключается как раньше. Отдельно: пакет
**другого** приложения в списке ничего не меняет.

- [ ] **Step 4: Стенд `.76` — `mode.min.hz.list` действует**

`mode.min.hz.list=<пакет VLC>=50` + клип 25 fps.
Ожидаемо: выбран режим 50 Гц (не 25); в логе при отсутствии подходящего режима — строка
`нет режима не ниже ... — выбираю обычным правилом`.

- [ ] **Step 5: APK на обоих стендах**

```bash
adb -s 192.168.88.50:5555 install -r android/app/build/outputs/apk/debug/app-debug.apk
adb -s 192.168.88.76:5555 install -r android/app/build/outputs/apk/debug/app-debug.apk
```
Ожидаемо на `.50`: строка «Плееры, которым AFR мешает» в «Настройки для Android 10 и выше»,
summary = «YouTube TV» (после первого запуска — из профиля); «Готово» возвращает в настройки,
значение уходит в ini (проверить `grep skip.pkg /data/user/0/ru.reyter.afrd2/cache/afrd.ini`);
видео в VLC при пустом списке переключает режим, при добавленном VLC — нет. Плюс пункт 4 Review
Focus: у ранее установленного приложения значение из профиля не теряется.
Ожидаемо на `.76`: строка видна, выбор работает, `fps.correct.*` помечены «не действует на
Android 6–9».

- [ ] **Step 6: Запись результатов**

Внести результаты (что проверено, что нет, расхождения) в журнал `afrd2-journal.md` новым
разделом и в `README.md` (статус приёмки).

---

## Self-Review

**1. Spec coverage**

| Требование спеки | Задача |
|---|---|
| Полноэкранный выбор приложений (вариант A) | Task 3, шаги 1–3, 5 |
| Фильтр «только плееры» по умолчанию + переключатель «Все приложения» | Task 3, шаг 3 (`VIDEO_MIME`, `pickerPlayers`) |
| Общий (переиспользуемый) выборщик; второй потребитель `fps.correct.av1.pkg` (single) | Task 3 шаг 3 (`modeFor`), Task 4 шаг 1 |
| `switch.skip.pkg` как `CFGKEY_BOTH`, перенос на legacy | Task 1, 2 |
| `mode.min.hz.list` тоже переносится | Task 2 шаг 6 |
| Определение переднего пакета на legacy | Task 1 |
| Пометка «не действует» на legacy для ATV14-ключей (кроме `switch.skip.pkg`) | Task 4 шаг 2 |
| Сев новых ключей в `ini_options` из профиля | Task 3 шаг 8 |
| Значение — пакеты через пробел | Task 3 шаг 2 (`Apps.split/join`) |
| Файлы вне списка (`Context` → `AppsPickerActivity`, `exported=false`) | Task 3 шаг 5 |
| Документация: README/ChangeLog/FAQ, политика | Task 2 шаг 8, Task 4 шаг 3, Task 5 шаг 6 |
| Приёмка на двух стендах, включая §28.10 | Task 5 |

**2. Placeholder scan:** в task-файлах нет «TBD»/«implement later»; в каждом шаге правки указаны
файлы, текст правки и команда проверки. Единственные «прозаические» шаги — документация
(Task 2 шаг 8, Task 4 шаг 3, Task 5 шаг 6) — намеренно описательные, с указанием файлов и
формулировок.

**3. Type consistency:** `pkgdet_parse` (Task 1) вызывается только из `pkg_probe` (Task 1) и теста;
`pkg_name`/`pkg_skip_active`/`select_mode` (Task 2) — статические в `afrd.c`; `AppsPickerActivity.MODE_*`,
`EXTRA_*`, `RESULT_VALUE`, `modeFor` (Task 3) используются в `SettingsFragment` (Task 3 шаг 7) с теми
же именами; `XAppsPreference.getApps/setApps/setPickListener` — как в шаге 1.

**4. Review Focus:** каждый из пяти пунктов привязан к тесту (Task 1 шаг 1) или к шагу живой
приёмки (Task 5 п.3, 5, 4).
