# AFRd2: тест-контракт списков недействующих ключей и GUI-строка `mode.min.hz.list` — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** расхождение между таблицей применимости ключей в демоне и пометками GUI становится
невозможным (его ловит host-тест), а `mode.min.hz.list` задаётся из приложения выбором плееров
с указанием частоты — и переживает обновление APK.

**Architecture:** Списки недействующих ключей остаются в `SettingsFragment`, но перестают быть
единственным источником правды: новый host-тест `tests/test_inactive.c` сверяет их с
`cfgkeys.c` (через публичный `cfgkeys_note()`) и с `preferences.xml`, падая с именем ключа.
Второй пункт: новый Android-независимый класс `PkgValues` (разбор/сборка «пакет=число»)
с host-тестом на JVM, поверх него — режим `pkg_value` в существующем `AppsPickerActivity`
(отметки + диалог числа) и строка `XAppsPreference` в настройках. Путь применения настроек
(`applyPrefs` → ini → `Control.reload()`) не меняется.

**Tech Stack:** C (host-тесты обычным `cc`), Java (framework `android.preference`, AGP 3.3.2,
Gradle 4.10.1, compileSdk 27, minSdk 23, targetSdk 28), host-тест Java на `javac`/`java` из JDK 11,
adb-стенды: `.50` (ATV14, S905X5M) и `.76` (ATV9, UGOOS AM6).

**Spec:** `docs/superpowers/specs/2026-10-06-afrd2-inactive-keys-drift-and-min-hz-gui-design.md`

## Global Constraints

* Рабочее дерево: `.worktrees/afrd2-cfgkeys-gui`, ветка `afrd2-cfgkeys-gui` от `master` = `e53fb5b`.
* Тесты: `sh tests/run_tests.sh` (сейчас 11 наборов C; после Task 1 — 12, после Task 2 — плюс
  Java-набор). Все наборы обязаны быть зелёными, вывод — без предупреждений компилятора.
* Сборка демона и копирование профилей/ЧАВО/ChangeLog в `res/raw*`: `./mka` (NDK r26d,
  обе ABI) — без предупреждений.
* Сборка APK: `cd android && JAVA_HOME=/usr/lib/jvm/java-11-openjdk-amd64 ./gradlew --offline assembleDebug`
  (SDK: `~/Android/Sdk`), javac с `-Xlint:deprecation -Xlint:unchecked` — предупреждений быть не должно.
* Демон, формат конфига и API (UDP 50505) не меняются: правки только в APK-части, host-тестах
  и документации (C-код демона не трогаем; `cfgkeys.c` не меняется).
* Значения профилей не меняем: строка `mode.min.hz.list` остаётся закомментированным примером.
* Разделитель значений — любой из `" \t\r\n"` (демон), имена пакетов через пробел, запятые не понимаются.
* Стенд `.76` занят рабочим upstream-AFRd владельца: эксклюзивный доступ и полный откат — по
  процедуре §27.1 журнала (`journal.md` / `afrd2-journal.md`). Перезагрузка боксов и выключение ТВ —
  только с согласия владельца.
* Java-стиль проекта: анонимные классы вместо лямбд, отступы — пробелы как в соседних файлах,
  комментарии по-русски с указанием причины решения.

## Review Focus

Пять классов входных данных и отказов, которые тесты ниже пиннят явно:

1. Значение, синтаксически похожее на число, но не принимаемое демоном (`50d`, `0x1p3`, `NaN`,
   `Infinity`) — не должно ни попадать в конфиг из GUI, ни считаться «заданным» при разборе
   (Task 2, `tests/PkgValuesTest.java`).
2. Запись без числа (`com.example.player` без `=`) в существующем конфиге — при сохранении из GUI
   не должна молча исчезнуть (Task 2, `tests/PkgValuesTest.java`).
3. Больше 8 записей (предел демона `MODE_MIN_HZ_MAX`) — GUI обязан отказать с сообщением, а не
   отдать демону значение, из которого тот молча выбросит лишнее (Task 2, шаг с диалогом; живая
   проверка — Task 2, шаг 8).
4. Ключ, показанный в GUI, но отсутствующий в таблице `cfgkeys.c` — тест обязан упасть (Task 1,
   `tests/test_inactive.c`).
5. Изменение класса ключа в `cfgkeys.c` без правки пометок GUI — тест обязан упасть, назвав ключ
   (Task 1, первый прогон на текущем коде падает именно так — на `cs_select`).

---

### Task 1: Тест-контракт пометок GUI и снятие ложной пометки `cs.select`

**Files:**

* Create: `tests/test_inactive.c`
* Modify: `tests/run_tests.sh` (добавить набор после `test_cfgkeys`)
* Modify: `android/app/src/main/java/ru/cobra/zap/afrd/gui/SettingsFragment.java:50-67` (убрать `cs_select`)
* Modify: `README.md:122` (строка таблицы применимости)

**Interfaces:**

* Consumes: `cfgkeys_note (const char *key, unsigned path)` из `cfgkeys.h`.
* Produces: набор `test_inactive` в `run_tests.sh`; массив `kInactiveOnAtv14` в `SettingsFragment`
  без `cs_select` (Task 2 на него опирается: новый ключ `mode.min.hz.list` класса `CFGKEY_BOTH`
  обязан остаться вне обоих списков, иначе тест упадёт).

- [ ] **Step 1: Написать падающий тест `tests/test_inactive.c`**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../cfgkeys.h"

/* Пометки «не действует на ...» в GUI (SettingsFragment.kInactiveOnAtv14 /
 * kInactiveOnLegacy) синхронизируются с таблицей ключей демона (cfgkeys.c)
 * вручную — и уже разъехались (cs.select был помечен недействующим на ATV14,
 * хотя действует там). Тест сверяет три источника:
 *   * cfgkeys.c        — через публичный cfgkeys_note();
 *   * preferences.xml  — какие настройки вообще показаны в GUI;
 *   * SettingsFragment — как они помечены.
 * Любое расхождение = пользователь видит выключенную рабочую настройку или
 * наоборот; тест падает, называя ключ. */

#define MAX_KEYS	128
#define KEY_LEN		128

static const char *k_prefs = "android/app/src/main/res/xml/preferences.xml";
static const char *k_fragment = "android/app/src/main/java/ru/cobra/zap/afrd/gui/SettingsFragment.java";

static char g_gui [MAX_KEYS][KEY_LEN];
static int g_gui_n;
static char g_atv14 [MAX_KEYS][KEY_LEN];
static int g_atv14_n;
static char g_legacy [MAX_KEYS][KEY_LEN];
static int g_legacy_n;

static void fail (const char *fmt, const char *arg)
{
	fprintf (stderr, "test_inactive: ");
	fprintf (stderr, fmt, arg);
	fprintf (stderr, "\n");
	exit (1);
}

static char *read_file (const char *path)
{
	FILE *f = fopen (path, "rb");
	if (!f)
	{
		fprintf (stderr, "test_inactive: %s не открывается\n", path);
		exit (2);
	}

	fseek (f, 0, SEEK_END);
	long n = ftell (f);
	fseek (f, 0, SEEK_SET);
	char *buf = malloc ((size_t) n + 1);
	if (!buf || (fread (buf, 1, (size_t) n, f) != (size_t) n))
	{
		fprintf (stderr, "test_inactive: %s не читается\n", path);
		exit (2);
	}
	buf [n] = 0;
	fclose (f);
	return buf;
}

static void add_key (char out [][KEY_LEN], int *count, const char *text, size_t len)
{
	if ((len == 0) || (len >= KEY_LEN) || (*count >= MAX_KEYS))
	{
		fprintf (stderr, "test_inactive: ключ неразумной длины или их слишком много\n");
		exit (2);
	}

	memcpy (out [*count], text, len);
	out [*count][len] = 0;
	(*count)++;
}

/* Ключи настроек из preferences.xml: все элементы, кроме групп
 * PreferenceScreen (у них android:key — идентификатор группы, а не настройки). */
static void parse_gui (const char *xml)
{
	const char *attr = "android:key=\"";
	const char *p = xml;

	while ((p = strstr (p, attr)) != NULL)
	{
		/* начало тега — ближайший '<' слева */
		const char *tag = p;
		while ((tag > xml) && (tag [-1] != '<'))
			tag--;

		size_t tag_len = strcspn (tag, " \t\r\n/>");
		const char *val = p + strlen (attr);
		const char *end = strchr (val, '"');
		if (!end)
		{
			fprintf (stderr, "test_inactive: незакрытый android:key в %s\n", k_prefs);
			exit (2);
		}

		if ((tag_len != strlen ("PreferenceScreen")) ||
		    (strncmp (tag, "PreferenceScreen", tag_len) != 0))
			add_key (g_gui, &g_gui_n, val, (size_t) (end - val));

		p = end;
	}
}

/* Строковый массив из SettingsFragment.java: от "String [] <имя>" до "};". */
static int parse_array (const char *src, const char *name, char out [][KEY_LEN])
{
	char decl [KEY_LEN];
	snprintf (decl, sizeof (decl), "String [] %s", name);

	const char *p = strstr (src, decl);
	if (!p)
	{
		fprintf (stderr, "test_inactive: в %s нет массива %s\n", k_fragment, name);
		exit (2);
	}

	p = strchr (p, '{');
	const char *end = p ? strstr (p, "};") : NULL;
	if (!end)
	{
		fprintf (stderr, "test_inactive: не найден конец массива %s\n", name);
		exit (2);
	}

	int n = 0;
	while (++p < end)
	{
		if (*p != '"')
			continue;

		const char *e = strchr (p + 1, '"');
		if (!e || (e > end))
			break;

		add_key (out, &n, p + 1, (size_t) (e - p - 1));
		p = e;
	}

	return n;
}

/* Ключ настройки в GUI → ключ в ini (то же правило, что в Config.load (SharedPreferences)). */
static void key_to_ini (const char *pref, char *ini)
{
	size_t i = 0;
	for (; pref [i] && (i + 1 < KEY_LEN); i++)
		ini [i] = (pref [i] == '_') ? '.' : pref [i];
	ini [i] = 0;
}

static int in_list (char list [][KEY_LEN], int n, const char *key)
{
	for (int i = 0; i < n; i++)
		if (strcmp (list [i], key) == 0)
			return 1;

	return 0;
}

static int is_unknown (const char *note)
{
	return note && strstr (note, "неизвестный");
}

int main (void)
{
	char *xml = read_file (k_prefs);
	char *java = read_file (k_fragment);

	parse_gui (xml);
	g_atv14_n = parse_array (java, "kInactiveOnAtv14", g_atv14);
	g_legacy_n = parse_array (java, "kInactiveOnLegacy", g_legacy);

	/* Санитарные проверки самого разбора: «ноль найденного» не должен
	 * выглядеть как согласованность. */
	if (g_gui_n < 25)
		fail ("найдено подозрительно мало строк GUI: %s", "");
	if ((g_atv14_n < 5) || (g_legacy_n < 10))
		fail ("найдено подозрительно мало пометок в списках: %s", "");

	for (int i = 0; i < g_gui_n; i++)
	{
		char ini [KEY_LEN];
		key_to_ini (g_gui [i], ini);

		const char *a14 = cfgkeys_note (ini, CFGKEY_ATV14);
		const char *leg = cfgkeys_note (ini, CFGKEY_LEGACY);
		int marked_a14 = in_list (g_atv14, g_atv14_n, g_gui [i]);
		int marked_leg = in_list (g_legacy, g_legacy_n, g_gui [i]);

		if (is_unknown (a14) || is_unknown (leg))
			fail ("строка GUI есть, а ключа нет в cfgkeys.c: %s", g_gui [i]);

		int only_legacy = (a14 != NULL) && (leg == NULL);	/* ключ старого пути */
		int only_a14 = (a14 == NULL) && (leg != NULL);		/* ключ нового пути */

		if (only_legacy && !marked_a14)
			fail ("не действует на Android 10-14, но в GUI не помечена: %s", g_gui [i]);
		if (only_a14 && !marked_leg)
			fail ("не действует на Android 6-9, но в GUI не помечена: %s", g_gui [i]);
		if (!only_legacy && marked_a14)
			fail ("помечена «не действует на Android 10-14», хотя ключ действует: %s", g_gui [i]);
		if (!only_a14 && marked_leg)
			fail ("помечена «не действует на Android 6-9», хотя ключ действует: %s", g_gui [i]);
	}

	/* Обратная проверка: в списках не должно быть ключей, которых нет в GUI
	 * (не-GUI ключ вроде mode.path пометки не получает никогда). */
	for (int i = 0; i < g_atv14_n; i++)
		if (!in_list (g_gui, g_gui_n, g_atv14 [i]))
			fail ("в kInactiveOnAtv14 есть ключ, которого нет в preferences.xml: %s", g_atv14 [i]);

	for (int i = 0; i < g_legacy_n; i++)
		if (!in_list (g_gui, g_gui_n, g_legacy [i]))
			fail ("в kInactiveOnLegacy есть ключ, которого нет в preferences.xml: %s", g_legacy [i]);

	/* Списки не пересекаются: строка не может быть помечена двумя путями сразу. */
	for (int i = 0; i < g_atv14_n; i++)
		if (in_list (g_legacy, g_legacy_n, g_atv14 [i]))
			fail ("ключ помечен недействующим на обоих путях: %s", g_atv14 [i]);

	free (xml);
	free (java);

	printf ("inactive: OK (%d строк GUI, %d на ATV14, %d на legacy)\n",
	        g_gui_n, g_atv14_n, g_legacy_n);
	return 0;
}
```

- [ ] **Step 2: Подключить набор к `tests/run_tests.sh`**

После строки `run test_cfgkeys cfgkeys.c cfg_parse/cfg_parse.c` добавить:

```sh
run test_inactive cfgkeys.c cfg_parse/cfg_parse.c
```

- [ ] **Step 3: Прогнать тест и увидеть падение по делу**

Run: `sh tests/run_tests.sh`

Expected:

```
test_inactive: помечена «не действует на Android 10-14», хотя ключ действует: cs_select
```

(остальные наборы зелёные; сам `test_inactive` в списке — последним перед падением оркестратора).

- [ ] **Step 4: Убрать ложную пометку**

В `android/app/src/main/java/ru/cobra/zap/afrd/gui/SettingsFragment.java` удалить строку
`"cs_select",` из массива `kInactiveOnAtv14` и дополнить комментарий перед массивом абзацем:

```java
    /* Настройки, которые на новом пути демона (Android 10-14) не действуют:
     * режим переключает фреймворк транзакцией SurfaceFlinger 1035, затемнения
     * нет, цвета/HDR ведёт вендорский стек, uevent-фильтры не нужны. Демон эти
     * ключи не читает (см. таблицу в cfgkeys.c) и сообщает о них в логе.
     * В GUI они остаются видимыми, но выключенными — чтобы не создавать
     * впечатление работающей настройки.
     * cs.select здесь нет: правила цветового пространства применяются и на
     * ATV14 (через вендорский сервис, см. afrd.c: atv14_apply_mode).
     * Согласованность списка с cfgkeys.c проверяет tests/test_inactive.c. */
```

- [ ] **Step 5: Прогнать тесты — зелёные**

Run: `sh tests/run_tests.sh`

Expected: все наборы `OK`, в конце `inactive: OK (38 строк GUI, 9 на ATV14, 19 на legacy)`,
код возврата 0. Числа могут отличаться, если строк в GUI стало больше (проверьте, что список
`kInactiveOnAtv14` содержит ровно 9 ключей).

- [ ] **Step 6: Поправить таблицу в README**

В `README.md:122` убрать `cs.select` из перечисления legacy-only и добавить его в строку
«действует на обоих путях» (строка `README.md:120`), сохранив порядок остальных ключей:

```
| `mode.extra`, `mode.path`, `vdec.sysfs`, `vdec.blacklist`, `frhint.vdec.blacklist`, `hdmi.sysfs`, `hdmi.state`, `switch.delay.on/off/retry`, `switch.blackout`, `switch.ignore`, `switch.hdmi`, `cs.list.path`, `cs.path`, `uevent.filter.*` | — (в логе: «не действует на новом пути») | действует |
```

и в строке 120 — `…, `cs.select`, `cs.list.path`, `cs.path`, `switch.timeout`, …` — то есть
`cs.select` перечислен среди действующих на обоих путях.

- [ ] **Step 7: Коммит**

```bash
git add tests/test_inactive.c tests/run_tests.sh \
	android/app/src/main/java/ru/cobra/zap/afrd/gui/SettingsFragment.java README.md
git commit -m "test(cfgkeys): пометки GUI сверяются с таблицей ключей; cs.select снова действует на ATV14

Тест tests/test_inactive.c сверяет kInactiveOnAtv14/kInactiveOnLegacy с
cfgkeys.c (через cfgkeys_note) и preferences.xml: строка GUI обязана быть в
таблице, обязана быть помечена, если ключ не действует на этом пути, и не
может быть помечена, если действует. На текущем коде падал на cs.select:
ключ класса CFGKEY_BOTH (цветовое пространство применяется и на ATV14 через
вендорский сервис), а GUI выключал его и писал «не действует на Android 10-14».
Пометка убрана, таблица README приведена в соответствие."
```

---

### Task 2: Строка `mode.min.hz.list` в GUI (режим `pkg_value` выборщика)

**Files:**

* Create: `android/app/src/main/java/ru/cobra/zap/afrd/PkgValues.java`
* Create: `tests/PkgValuesTest.java`
* Modify: `tests/run_tests.sh` (Java-набор)
* Modify: `android/app/src/main/java/ru/cobra/zap/afrd/gui/AppsPickerActivity.java`
* Modify: `android/app/src/main/java/ru/cobra/zap/afrd/gui/SettingsFragment.java`
* Modify: `android/app/src/main/java/ru/cobra/zap/afrd/Config.java:46`
* Modify: `android/app/src/main/java/ru/cobra/zap/afrd/gui/MainActivity.java:230`
* Modify: `android/app/src/main/res/layout/item_app.xml`
* Modify: `android/app/src/main/res/xml/preferences.xml`
* Modify: `android/app/src/main/res/values/strings.xml`, `android/app/src/main/res/values-ru/strings.xml`
* Modify: `README.md:108`, `ChangeLog-ru.txt`, `ChangeLog-en.txt`
* Regenerate: `android/app/src/main/res/raw*/changelog.txt` (через `./mka`)

**Interfaces:**

* Consumes: `cfgkeys.c` (ключ `mode.min.hz.list` — `CFGKEY_BOTH`, диапазон 1…1000 Гц, ≤ 8 записей);
  `Apps.split`/`Apps.join`/`Apps.label`/`Apps.app`/`Apps.indexOf` (существующие);
  `XAppsPreference.getApps()`/`setApps()`; `XAppsPreference.OnPickListener`;
  `SettingsFragment.kAppsPrefs` (из ветки `afrd2-app-picker`).
* Produces:
  * `PkgValues.parse (String value, double min, double max) → LinkedHashMap<String,String>`
  * `PkgValues.format (Map<String,String> values) → String`
  * `PkgValues.inRange (String text, double min, double max) → boolean`
  * `PkgValues.MIN_HZ = 1.0`, `PkgValues.MAX_HZ = 1000.0`
  * `AppsPickerActivity.MODE_PKG_VALUE = "pkg_value"` (значение extras/result — как у `multi`/`single`)

- [ ] **Step 1: Написать падающий host-тест `tests/PkgValuesTest.java`**

```java
package ru.cobra.zap.afrd;

import java.util.LinkedHashMap;

/**
 * Host-тест разбора и сборки значения «пакет=число» (см. tests/run_tests.sh).
 * Проверяет, что GUI видит значение ровно так же, как демон (afrd.c,
 * mode_min_hz_load): иначе сохранение из настроек молча потеряет или испортит
 * то, что демон на самом деле использует.
 */
public class PkgValuesTest
{
    private static final double MIN = PkgValues.MIN_HZ;
    private static final double MAX = PkgValues.MAX_HZ;

    private static void eq (String what, String got, String want)
    {
        if (!got.equals (want))
        {
            System.err.println ("PkgValues: " + what + ": ожидалось [" + want
                + "], получено [" + got + "]");
            System.exit (1);
        }
    }

    private static String round (String value)
    {
        return PkgValues.format (PkgValues.parse (value, MIN, MAX));
    }

    public static void main (String [] args)
    {
        /* обычный список: порядок записей сохраняется */
        eq ("порядок", round ("a.b=50 c.d=59.94"), "a.b=50 c.d=59.94");

        /* повтор пакета: первая запись (как mode_min_hz_for в демоне) */
        eq ("повтор", round ("a=50 a=60"), "a=50");

        /* запись без '=' сохраняется как есть: GUI не удаляет текст молча */
        eq ("без =", round ("a.b"), "a.b");
        eq ("пустое =", round ("a.b="), "a.b");

        /* мусор вместо числа и «числа», которые Double.parseDouble съел бы,
         * а sscanf("%f") в демоне — нет */
        eq ("мусор", round ("a=abc b=NaN c=50d d=0x1p3 e=Infinity f=12"), "f=12");

        /* диапазон 1…1000: границы включаются, выход за них отбрасывается */
        eq ("диапазон", round ("a=0 b=1 c=1000 d=1001"), "b=1 c=1000");

        /* разделители — любые пробельные (strspn(spaces) в демоне) */
        eq ("разделители", round ("  a=50\t b=60\n c=70  "), "a=50 b=60 c=70");

        /* дробные и экспоненциальная запись принимаются демоном */
        eq ("дробное", round ("a=23.976 b=6e1"), "a=23.976 b=6e1");

        /* пустое значение настройки — пустой список, а не «одна пустая запись» */
        eq ("пусто", round (""), "");
        eq ("null", round (null), "");
        eq ("сборка пустого", PkgValues.format (new LinkedHashMap<String, String> ()), "");

        /* без числа запись считается незаданной, но остаётся в значении */
        LinkedHashMap<String, String> v = PkgValues.parse ("a.b", MIN, MAX);
        if (!"".equals (v.get ("a.b")) || (v.size () != 1))
        {
            System.err.println ("PkgValues: запись без числа должна сохраняться с пустым значением");
            System.exit (1);
        }

        System.out.println ("PkgValues: OK");
    }
}
```

- [ ] **Step 2: Прогнать тест — не компилируется**

Run:

```sh
mkdir -p out/host-tests/java && javac -d out/host-tests/java tests/PkgValuesTest.java
```

Expected: ошибки компиляции `package ru.cobra.zap.afrd does not exist` / `cannot find symbol:
class PkgValues` — реализации ещё нет.

- [ ] **Step 3: Реализовать `PkgValues`**

`android/app/src/main/java/ru/cobra/zap/afrd/PkgValues.java`:

```java
/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.regex.Pattern;

/**
 * Значение настройки вида «пакет=число» (mode.min.hz.list): разбор и сборка
 * ровно в том виде, в каком значение читает демон (afrd.c, mode_min_hz_load).
 * Разница в поведении здесь стоила бы пользователю silently потерянной
 * настройки, поэтому правила повторяют демон: записи через пробел, число
 * в диапазоне 1…1000 (для ЧАСТОТЫ), не более 8 записей.
 *
 * Класс намеренно не зависит от Android: его проверяет host-тест
 * tests/PkgValuesTest.java тем же javac, которым собирается APK.
 */
public class PkgValues
{
    /** Границы частоты для mode.min.hz.list (afrd.c: hz >= 1 && hz <= 1000). */
    public static final double MIN_HZ = 1.0;
    public static final double MAX_HZ = 1000.0;

    /** Предел числа записей у демона (afrd.c: MODE_MIN_HZ_MAX). */
    public static final int MAX_ENTRIES = 8;

    /* Синтаксис числа, который принимает sscanf("%f") в демоне: обычная или
     * экспоненциальная запись. Отсекает то, что принял бы Double.parseDouble,
     * но демон — нет: шестнадцатеричное (0x1p3), хвостовое d/f (50d),
     * NaN, Infinity. */
    private static final Pattern NUMBER =
        Pattern.compile ("[+-]?(\\d+(\\.\\d*)?|\\.\\d+)([eE][+-]?\\d+)?");

    /**
     * Разбор значения. Порядок записей сохраняется; повтор пакета оставляет
     * первую запись (как mode_min_hz_for в демоне). Записи, которые демон
     * игнорирует (число не разбирается, вне диапазона, пустое имя пакета),
     * отбрасываются; запись без числа сохраняется с пустым значением — демон
     * её тоже игнорирует, но GUI не должен молча удалять текст пользователя.
     */
    public static LinkedHashMap<String, String> parse (String value, double min, double max)
    {
        LinkedHashMap<String, String> out = new LinkedHashMap<> ();
        if (value == null)
            return out;

        for (String token : value.trim ().split ("\\s+"))
        {
            if (token.isEmpty ())
                continue;

            int eq = token.indexOf ('=');
            String pkg = (eq < 0) ? token : token.substring (0, eq);
            String num = (eq < 0) ? "" : token.substring (eq + 1);
            if (pkg.isEmpty () || out.containsKey (pkg))
                continue;
            if (!num.isEmpty () && !inRange (num, min, max))
                continue;

            out.put (pkg, num);
        }

        return out;
    }

    /** Сборка значения: записи через пробел, порядок сохраняется. */
    public static String format (Map<String, String> values)
    {
        StringBuilder sb = new StringBuilder ();
        for (Map.Entry<String, String> e : values.entrySet ())
        {
            if (sb.length () > 0)
                sb.append (' ');
            sb.append (e.getKey ());
            if ((e.getValue () != null) && !e.getValue ().isEmpty ())
                sb.append ('=').append (e.getValue ());
        }

        return sb.toString ();
    }

    /** Число в диапазоне min…max, записанное так, как его поймёт демон. */
    public static boolean inRange (String text, double min, double max)
    {
        if (text == null)
            return false;

        String s = text.trim ();
        if (!NUMBER.matcher (s).matches ())
            return false;

        try
        {
            double v = Double.parseDouble (s);
            return !Double.isNaN (v) && !Double.isInfinite (v) && (v >= min) && (v <= max);
        }
        catch (NumberFormatException exc)
        {
            return false;
        }
    }
}
```

- [ ] **Step 4: Подключить Java-набор к `tests/run_tests.sh`**

В конце файла, перед `exit $status`, добавить:

```sh
# Java-логика без Android-зависимостей (значения «пакет=число»): тот же
# исходник, что идёт в APK, компилируется javac и запускается на JVM.
JAVAC=${JAVAC:-javac}
JAVA=${JAVA:-java}
mkdir -p out/host-tests/java
"$JAVAC" -d out/host-tests/java \
	android/app/src/main/java/ru/cobra/zap/afrd/PkgValues.java \
	tests/PkgValuesTest.java && "$JAVA" -cp out/host-tests/java ru.cobra.zap.afrd.PkgValuesTest || status=1
```

- [ ] **Step 5: Прогнать тесты — Java-набор зелёный**

Run: `sh tests/run_tests.sh`

Expected: 12 наборов C зелёные + `PkgValues: OK`, код возврата 0.

- [ ] **Step 6: Реализовать режим `pkg_value` в выборщике**

6.1. `res/layout/item_app.xml` — перед `CheckBox` добавить поле значения (в остальных режимах скрыто):

```xml
    <TextView android:id="@+id/appValue" android:layout_width="wrap_content"
              android:layout_height="wrap_content" android:layout_marginEnd="16dp"
              android:textSize="16sp" android:visibility="gone"/>
```

6.2. `AppsPickerActivity.java`:

* константы:

```java
    public static final String MODE_PKG_VALUE = "pkg_value";
```

* поля: `private final LinkedHashMap<String, String> mValues = new LinkedHashMap<> ();`

* в `onCreate()`, вместо `mSelected.addAll (Apps.split (in.getStringExtra (EXTRA_VALUE)));`:

```java
        String value = in.getStringExtra (EXTRA_VALUE);
        if (MODE_PKG_VALUE.equals (mMode))
        {
            /* Разбор — теми же правилами, что у демона (PkgValues): то, что он
             * игнорирует, в значении не сохраняем — иначе GUI показывал бы
             * настройку, которой демон не видит. */
            mValues.putAll (PkgValues.parse (value, PkgValues.MIN_HZ, PkgValues.MAX_HZ));
            mSelected.addAll (mValues.keySet ());
        }
        else
            mSelected.addAll (Apps.split (value));
```

* в `list.setOnItemClickListener`, в начале обработчика:

```java
                if (MODE_PKG_VALUE.equals (mMode))
                {
                    editValue (mApps.get (position).pkg);
                    return;
                }
```

* новый метод (диалог числа):

```java
    /* Диалог значения для строки списка (режим pkg_value): число вводится с
     * пульта, имя пакета — отметкой в списке. Кнопка сохранения активна
     * только при допустимом числе: проверка идёт на каждом нажатии клавиши,
     * поэтому диалог не приходится переспрашивать. */
    private void editValue (final String pkg)
    {
        final boolean selected = mSelected.contains (pkg);
        final EditText input = new EditText (this);
        input.setInputType (InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL);
        input.setText (mValues.containsKey (pkg) ? mValues.get (pkg) : "");
        input.setHint (R.string.apps_value_hint);

        float d = getResources ().getDisplayMetrics ().density;
        AlertDialog.Builder b = new AlertDialog.Builder (this)
            .setTitle (getString (R.string.apps_value_title, Apps.label (this, pkg)))
            .setView (input, (int) (16 * d), (int) (8 * d), (int) (16 * d), (int) (8 * d))
            .setNegativeButton (android.R.string.cancel, null)
            .setPositiveButton (R.string.apps_value_save, new DialogInterface.OnClickListener ()
            {
                @Override
                public void onClick (DialogInterface dialog, int which)
                {
                    applyValue (pkg, selected, input.getText ().toString ().trim ());
                }
            });

        if (selected)
            b.setNeutralButton (R.string.apps_value_clear, new DialogInterface.OnClickListener ()
            {
                @Override
                public void onClick (DialogInterface dialog, int which)
                {
                    mSelected.remove (pkg);
                    mValues.remove (pkg);
                    mAdapter.notifyDataSetChanged ();
                }
            });

        final AlertDialog dialog = b.create ();
        dialog.setOnShowListener (new DialogInterface.OnShowListener ()
        {
            @Override
            public void onShow (DialogInterface d)
            {
                final Button ok = ((AlertDialog) d).getButton (AlertDialog.BUTTON_POSITIVE);
                ok.setEnabled (PkgValues.inRange (input.getText ().toString (),
                    PkgValues.MIN_HZ, PkgValues.MAX_HZ));
                input.addTextChangedListener (new TextWatcher ()
                {
                    @Override public void beforeTextChanged (CharSequence s, int a, int b, int c) { }
                    @Override public void onTextChanged (CharSequence s, int a, int b, int c) { }
                    @Override public void afterTextChanged (Editable s)
                    {
                        ok.setEnabled (PkgValues.inRange (s.toString (),
                            PkgValues.MIN_HZ, PkgValues.MAX_HZ));
                    }
                });
            }
        });
        dialog.show ();
    }

    /* Запись значения в набор: новую строку отмечаем, если демон ещё примет
     * запись (иначе он молча выбросит лишние — afrd.c: MODE_MIN_HZ_MAX). */
    private void applyValue (String pkg, boolean selected, String value)
    {
        if (!selected && (mValues.size () >= PkgValues.MAX_ENTRIES))
        {
            Toast.makeText (this, getString (R.string.apps_value_limit, PkgValues.MAX_ENTRIES),
                Toast.LENGTH_LONG).show ();
            return;
        }

        mValues.put (pkg, value);
        mSelected.add (pkg);
        mAdapter.notifyDataSetChanged ();
    }
```

* `finishWithResult()`: ветка режима

```java
        else if (MODE_PKG_VALUE.equals (mMode))
            out.putExtra (RESULT_VALUE, PkgValues.format (mValues));
```

* `AppsAdapter`: конструктор получает `showValues`, `values`; в `getView()`:

```java
            TextView val = v.findViewById (R.id.appValue);
            if (mShowValues)
            {
                String n = mValues.get (app.pkg);
                val.setVisibility (n == null ? View.GONE : View.VISIBLE);
                val.setText (n == null ? "" : mCtx.getString (R.string.apps_value_hz, n));
            }
            else
                val.setVisibility (View.GONE);
```

* импорты: `android.app.AlertDialog`, `android.content.DialogInterface`, `android.text.Editable`,
  `android.text.InputType`, `android.text.TextWatcher`, `android.widget.Button`, `android.widget.EditText`,
  `android.widget.Toast`, `java.util.LinkedHashMap`, `ru.cobra.zap.afrd.PkgValues`.

6.3. `SettingsFragment.java`:

* в `kAppsPrefs` добавить строку (порядок не важен):

```java
        { "mode_min_hz_list", AppsPickerActivity.MODE_PKG_VALUE },
```

* `setAppsSummary()` становится mode-aware:

```java
    private void setAppsSummary (XAppsPreference pref)
    {
        String value = pref.getApps ();
        CharSequence hint = mAppsHints.get (pref.getKey ());
        String names = AppsPickerActivity.MODE_PKG_VALUE.equals (appsMode (pref.getKey ()))
            ? valueSummary (value) : Apps.names (getActivity (), value);

        /* Пустой список — состояние по умолчанию: возвращаем пояснение из XML,
         * иначе Preference скрывает summary вместе с ним (пустая строка). */
        pref.setSummary (names.isEmpty () ? hint : names);
    }

    /* Значение «пакет=число» в виде «Плеер = 50 Гц, Другой = 59.94 Гц». */
    private String valueSummary (String value)
    {
        StringBuilder sb = new StringBuilder ();
        for (Map.Entry<String, String> e : PkgValues.parse (value,
                PkgValues.MIN_HZ, PkgValues.MAX_HZ).entrySet ())
        {
            if (sb.length () > 0)
                sb.append (", ");
            sb.append (Apps.label (getActivity (), e.getKey ()));
            if (!e.getValue ().isEmpty ())
                sb.append (" = ").append (getString (R.string.apps_value_hz, e.getValue ()));
        }

        return sb.toString ();
    }
```

* импорты: `java.util.Map`, `ru.cobra.zap.afrd.PkgValues`.

6.4. `res/xml/preferences.xml` — сразу после элемента `switch_skip_pkg`:

```xml
    <ru.cobra.zap.xprefs.XAppsPreference android:key="mode_min_hz_list"
                      android:summary="@string/prefsum_mode_min_hz_list"
                      android:title="@string/prefs_mode_min_hz_list"/>
```

6.5. `Config.java` — после `itemType.put ("switch.skip.pkg", TYPE_STR);`:

```java
        itemType.put ("mode.min.hz.list", TYPE_STR);
```

6.6. `MainActivity.java` — в `seedOptions()`:

```java
        final String [] seeded = { "switch.skip.pkg", "fps.correct.av1.pkg", "mode.min.hz.list" };
```

6.7. Строки — в `res/values-ru/strings.xml` (и с теми же именами в `res/values/strings.xml`,
по-английски):

```xml
    <string name="prefs_mode_min_hz_list">Минимальная частота режима для плееров</string>
    <string name="prefsum_mode_min_hz_list">Для выбранных приложений демон не выбирает режимы
        ниже указанной частоты (Гц): плееры, которые «зажимают» темп отрисовки, стартуя под
        низким режимом, начинают работать нормально. Коснитесь строки, отметьте приложения и
        задайте частоту в той же строке.</string>
    <string name="apps_value_title">%s: минимальная частота, Гц</string>
    <string name="apps_value_hint">1…1000</string>
    <string name="apps_value_save">Сохранить</string>
    <string name="apps_value_clear">Снять отметку</string>
    <string name="apps_value_hz">%s Гц</string>
    <string name="apps_value_limit">Не больше %1$d приложений: столько записей поддерживает демон</string>
```

- [ ] **Step 7: Собрать APK и демон — без предупреждений**

Run:

```bash
./mka
cd android && JAVA_HOME=/usr/lib/jvm/java-11-openjdk-amd64 ./gradlew --offline assembleDebug
```

Expected: `mka` — 0 предупреждений, обе ABI, профили и ChangeLog/FAQ скопированы в `res/raw*`;
`gradlew` — `BUILD SUCCESSFUL`, ни одного `warning:` от javac. Проверить, что
`android/app/src/main/res/raw-ru/changelog.txt` побайтово равен `ChangeLog-ru.txt`
(`cmp ChangeLog-ru.txt android/app/src/main/res/raw-ru/changelog.txt`), то же для `ChangeLog-en.txt`
и `res/raw/changelog.txt`.

- [ ] **Step 8: Живая проверка на `.50` (ATV14): строка, значение, поведение, переживание обновления**

```bash
A=192.168.88.50:5555
B=/home/reyter/AI/afrd2/.worktrees/afrd2-cfgkeys-gui
adb -s $A install -r $B/android/app/build/outputs/apk/debug/ru.reyter.afrd2-0.3.3-debug.apk
adb -s $A shell 'am force-stop ru.reyter.afrd2; am start -n ru.reyter.afrd2/ru.cobra.zap.afrd.gui.MainActivity'
# далее: Настройки → строка «Минимальная частота режима для плееров» → выборщик:
#   отметить VLC (org.videolan.vlc) → диалог: 50 → Сохранить → ГОТОВО
adb -s $A shell 'su -c "grep -m1 \"^mode.min.hz.list=\" /data/user/0/ru.reyter.afrd2/cache/afrd.ini"'
```

Ожидается `mode.min.hz.list=org.videolan.vlc=50`; строка настроек показывает «VLC = 50 Гц».

Проверка поведения (до/после — A/B): базовый прогон клипа 23.976 fps без значения даёт
`1080p24hz`, с порогом 50 — самый быстрый режим (`1080p60hz`), потому что ровного кратного
выше порога у 23.976 нет:

```bash
sh tools/stand/play_clip.sh start long_24000_1001 && sleep 12 && sh tools/stand/check_mode.sh
sh tools/stand/play_clip.sh stop
```

Проверка переживания обновления APK (главный сценарий пункта):

```bash
adb -s $A install -r $B/android/app/build/outputs/apk/debug/ru.reyter.afrd2-0.3.3-debug.apk
adb -s $A shell 'su -c "grep -m1 \"^mode.min.hz.list=\" /data/user/0/ru.reyter.afrd2/cache/afrd.ini"'
```

Ожидается то же значение: профиль из ресурсов перезаписан, но `applyPrefs()` вернул значение
из `ini_options` поверх него.

Также проверить, что имя пакета, которого нет в системе, не ломает экран (ввести вручную в
`ini_options` через «правку значения» невозможно — пропустить, если сценарий недоступен).

- [ ] **Step 9: Живая проверка ложной пометки и строки на обоих путях**

На `.50` (ATV14) строка `cs.select` («Цветовое пространство») обязана быть **активной** и без
пометки «(не действует на Android 10-14)»: дамп UI (`uiautomator dump`) в группе
«Настройки для опытных пользователей» — `enabled="true"`, текст пояснения без пометки.

На `.76` (ATV9, процедура §27.1: на время проверки AFRd владельца отключается, после — включается):

```bash
A=192.168.88.76:5555
adb -s $A shell 'su -c "pm disable ru.cobra.zap.afrd; am force-stop ru.cobra.zap.afrd"'
adb -s $A install -r $B/android/app/build/outputs/apk/debug/ru.reyter.afrd2-0.3.3-debug.apk
adb -s $A shell 'am start -n ru.reyter.afrd2/ru.cobra.zap.afrd.gui.MainActivity'
# дамп UI: строка «Минимальная частота режима для плееров» активна, без пометки
adb -s $A uninstall ru.reyter.afrd2
adb -s $A shell 'su -c "pm enable ru.cobra.zap.afrd; am startservice ru.cobra.zap.afrd/.service"'
```

Стенды после проверок: `.50` — режим как в профиле (1080p60hz), значение `mode.min.hz.list`
очищено (строка снята отметкой), демон приложения работает; `.76` — AFRd владельца включён и
работает (проверить `pidof afrd`), наше APK снято, режим 1080p50hz.

- [ ] **Step 10: Документация**

* `README.md:108` — в описание `mode.min.hz.list` добавить: значение задаётся в GUI (выбор
  плееров и частоты), в профиле остаётся закомментированный пример для ручной правки.
* `ChangeLog-ru.txt` и `ChangeLog-en.txt` — в верхний (незакоммиченный) раздел добавить пункт:

```
  * mode.min.hz.list задаётся в приложении: строка «Минимальная частота режима для плееров»
    открывает список приложений, для каждого выбранного плеера указывается частота —
    ввод имени пакета с пульта больше не нужен. Значение хранится в настройках приложения
    и потому переживает обновление APK.
  * Строка «Цветовое пространство» (cs.select) снова действует на Android 10-14: она была
    помечена в GUI как недействующая, хотя правила cs.select применяются и там (через
    вендорский сервис). Согласованность пометок GUI с таблицей ключей демона теперь
    проверяет host-тест.
```

* Затем `./mka` (перекопирует ChangeLog/FAQ в `res/raw*`) и проверка `cmp`.

- [ ] **Step 11: Коммит**

```bash
git add android tests README.md ChangeLog-ru.txt ChangeLog-en.txt
git commit -m "feat(app): строка mode.min.hz.list в GUI — выбор плееров и порога частоты

Новый режим pkg_value экрана выбора приложений: отметки вместо ввода имён
пакетов, число (Гц) вводится в диалоге строки. Разбор и сборка значения —
в Android-независимом PkgValues с host-тестом на JVM: правила повторяют
демон (диапазон 1…1000, ≤ 8 записей, первая запись при повторе пакета) и
сохраняют записи, которые демон игнорирует, чтобы GUI не терял текст молча.
Значение хранится в ini_options, поэтому переживает обновление APK, которое
перезаписывает профиль из ресурсов."
```

---

## Проверка плана на полноту (self-review)

* Пункт 1 спеки (GUI-строка) — Task 2, шаги 1–11; ограничения ключа (диапазон, ёмкость, повтор,
  формат) — шаг 1 (тест) и шаг 3 (реализация).
* Пункт 2 спеки (производные списки) — Task 1, шаги 1–7; расхождение `cs_select` — шаг 3 (падение),
  шаг 4 (починка), шаг 6 (README), шаг 9 (живая проверка обоих путей).
* Не-цели: `fps.correct.list` не трогаем; демон и API не меняем; профили не меняем.
* Открытые вопросы владельца вынесены в спеку §7 и в леджер исполнения.
