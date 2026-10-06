# AFRd2: доводка строки `mode.min.hz.list` — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** строки настроек, связанные с `mode.min.hz.list`, перестают молча расходиться с поведением
демона: инертные ключи начинают доезжать до конфига, правило посева и набор ключей закрыты
host-тестами, лимит восьми записей обрезается с предупреждением, а имя пакета длиннее 63 байт
больше не обрезается демоном.

**Architecture:** три независимых слоя. (1) Приложение: `Config.itemType` + `seedOptions` +
Android-независимый `Seed` для правила посева. (2) Тесты: `tests/test_inactive.c` расширяется
проверкой набора ключей; новые host-тесты `SeedTest`, кейсы в `PkgValuesTest` и `test_pkgdet`.
(3) Демон: один общий размер буфера имени пакета (`PKG_NAME_LEN` в `afrd.h`) вместо четырёх
разных `[64]`.

**Tech Stack:** C (bionic/NDK + host-тесты обычным `cc`), Java (framework `android.preference`,
AGP 3.3.2, compileSdk 27), host-тесты Java на `javac`/`java` (JDK 11), adb-стенды `.50` (ATV14)
и `.76` (ATV9).

**Spec:** `docs/superpowers/specs/2026-10-06-afrd2-min-hz-gui-fixups-design.md`

## Global Constraints

* Worktree: `.worktrees/afrd2-gui-fixups`, ветка `afrd2-gui-fixups` от `master` = `62eb412`.
* `sh tests/run_tests.sh` — 12 наборов C + Java-набор `PkgValues`; после этой работы добавляется
  `SeedTest`. Все зелёные, предупреждений компилятора нет.
* `./mka` (NDK r26d, обе ABI) и `cd android && JAVA_HOME=/usr/lib/jvm/java-11-openjdk-amd64 ./gradlew --offline assembleDebug` — без предупреждений; `mka` перекопирует профили/FAQ/ChangeLog в `res/raw*`.
* Значения профиля `config/afrd2.ini` не меняем; меняются только умолчания GUI, и только чтобы
  совпасть с профилем.
* Проведение демона не меняется: правка сводится к размерам буферов и не влияет на решения.
* Стенд `.76` занят рабочим AFRd владельца: эксклюзивный доступ по процедуре §27.1 журнала,
  после проверок — вернуть как было.
* Стиль: анонимные классы вместо лямбд, комментарии по-русски с причиной решения.

## Review Focus

1. Строка GUI, отсутствующая в `Config.itemType` — тест обязан упасть с именем ключа (Task 1).
2. Пустое значение профиля при первом запуске — не должно превращать закомментированную строку
   профиля в активную пустую (Task 2).
3. Значение из более чем восьми записей — обрезается с сообщением, порядок сохраняется (Task 3).
4. Имя пакета длиннее 63 байт — после правки демона применяется целиком (Task 4).
5. Ключ настройки приложения (`toast.hz`) — исключение из проверки набора ключей, не должен
   валить тест и не должен требовать `itemType` (Task 1).

---

### Task 1: Инертные ключи доезжают до конфига + контракт набора ключей

**Files:**
* Modify: `android/app/src/main/java/ru/cobra/zap/afrd/Config.java` (после `itemType.put ("fps.recheck.delay.ms", …)`)
* Modify: `android/app/src/main/res/xml/preferences.xml:72` (defaultValue `12000` → `14000`)
* Modify: `tests/test_inactive.c` (новая проверка)
* Test: `tests/test_inactive.c`

**Interfaces:**
* Consumes: `Config.itemType` (статические `put`-вызовы в конструкторе), `preferences.xml`.
* Produces: ключи `fps.measure.envelope.early.ms` и `fps.cadence.fix.ms` класса `TYPE_INT` в `Config`;
  проверка `itemType` в `test_inactive` (Task 2 опирается на неё: новый ключ настройки обязан быть в `itemType`).

- [ ] **Step 1: Написать падающую проверку**

В `tests/test_inactive.c` добавить разбор `Config.java` и проверку; вызов — в `main()` после проверок списков:

```c
/* Ключи, правка которых должна доезжать до ini: Config.load (SharedPreferences)
 * переносит только то, что объявлено в Config.itemType. Строка GUI, которой там
 * нет, показывает умолчание и молча не влияет на демон (так жили
 * fps.measure.envelope.early.ms и fps.cadence.fix.ms). */
static const char *k_config = "android/app/src/main/java/ru/cobra/zap/afrd/Config.java";

static void check_item_type (const char *java_config)
{
	for (int i = 0; i < g_gui_n; i++)
	{
		char ini [KEY_LEN];
		key_to_ini (g_gui [i], ini);

		/* toast.hz — настройка приложения: демон её не читает, в ini не нужна */
		if (strcmp (g_gui [i], "toast_hz") == 0)
			continue;

		char needle [KEY_LEN + 32];
		snprintf (needle, sizeof (needle), "itemType.put (\"%s\"", ini);
		if (!strstr (java_config, needle))
			fail ("строки GUI нет в Config.itemType — правка не доедет до ini: %s", g_gui [i]);
	}
}
```

В `main()`: прочитать `Config.java` (`read_file (k_config)`), вызвать `check_item_type (...)`, освободить буфер.

- [ ] **Step 2: Прогнать и увидеть падение**

Run: `sh tests/run_tests.sh`

Expected: `test_inactive: строки GUI нет в Config.itemType — правка не доедет до ini: fps_measure_envelope_early_ms`

- [ ] **Step 3: Добавить ключи в `Config.itemType`**

```java
        itemType.put ("fps.measure.envelope.early.ms", TYPE_INT);
        itemType.put ("fps.cadence.fix.ms", TYPE_INT);
```

- [ ] **Step 4: Привести умолчание в XML к профилю**

В `preferences.xml` у `fps_measure_envelope_early_ms` заменить `android:defaultValue="12000"` на `"14000"`
(значение профиля `config/afrd2.ini:27`; иначе первый сеанс настроек перепишет профиль на 12000).

- [ ] **Step 5: Прогнать — зелёные**

Run: `sh tests/run_tests.sh`

Expected: все наборы OK, `inactive: OK (39 строк GUI, 9 на ATV14, 19 на legacy)`, код возврата 0.

- [ ] **Step 6: Коммит**

```bash
git add tests/test_inactive.c android/app/src/main/java/ru/cobra/zap/afrd/Config.java \
	android/app/src/main/res/xml/preferences.xml
git commit -m "fix(app): fps.measure.envelope.early.ms и fps.cadence.fix.ms доезжают до конфига

Ключи были в GUI и в cfgkeys.c, но отсутствовали в Config.itemType, поэтому
Config.load (SharedPreferences) их не переносил: строка показывала умолчание
(12000 против профильных 14000), а демон читал профиль. Умолчание в XML
приведено к профильному. Регресс закрыт проверкой в tests/test_inactive.c:
каждая строка GUI, кроме toast.hz, обязана быть в Config.itemType."
```

---

### Task 2: Правило посева — в Android-независимый класс `Seed`

**Files:**
* Create: `android/app/src/main/java/ru/cobra/zap/afrd/Seed.java`
* Create: `tests/SeedTest.java`
* Modify: `tests/run_tests.sh` (Java-набор)
* Modify: `android/app/src/main/java/ru/cobra/zap/afrd/gui/MainActivity.java` (`seedOptions`)

**Interfaces:**
* Produces: `Seed.value (String profileValue) → String` (null, если сеять нечего) и
  `Seed.shouldSeed (String profileValue, boolean alreadyInPrefs) → boolean`.
* Consumes: ничего.

- [ ] **Step 1: Написать падающий тест `tests/SeedTest.java`**

```java
package ru.cobra.zap.afrd;

/** Правило посева настроек из профиля (см. MainActivity.seedOptions). */
public class SeedTest
{
    private static void expect (String what, boolean got, boolean want)
    {
        if (got != want)
        {
            System.err.println ("Seed: " + what + ": ожидалось " + want + ", получено " + got);
            System.exit (1);
        }
    }

    public static void main (String [] args)
    {
        /* пустое значение профиля не сеем: иначе закомментированная строка
         * профиля становится активной пустой и затирает ручную правку */
        expect ("пусто", Seed.shouldSeed ("", false), false);
        expect ("null", Seed.shouldSeed (null, false), false);
        expect ("пробелы", Seed.shouldSeed ("  ", false), false);

        /* непустое сеем, чтобы первая же правка настроек не потеряла профиль */
        expect ("значение", Seed.shouldSeed ("com.foo=50", false), true);

        /* уже имеющаяся настройка не перетирается */
        expect ("уже есть", Seed.shouldSeed ("com.foo=50", true), false);
        expect ("уже есть, пусто", Seed.shouldSeed ("", true), false);

        if (!"com.foo".equals (Seed.value (" com.foo ")))
        {
            System.err.println ("Seed: value должно обрезать пробелы");
            System.exit (1);
        }

        System.out.println ("Seed: OK");
    }
}
```

- [ ] **Step 2: Убедиться, что не компилируется**

Run: `javac -d out/host-tests/java tests/SeedTest.java`
Expected: `cannot find symbol: class Seed`.

- [ ] **Step 3: Реализовать `Seed`**

```java
/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

/**
 * Правило переноса настроек из профиля в настройки приложения (ini_options).
 *
 * Сеем только непустое значение: пустое — это не умолчание, а отсутствие
 * настройки. Посев пустоты превратил бы закомментированную строку профиля в
 * активную пустую и затёр ручную правку ключа при следующем сохранении
 * настроек. Класс без Android-зависимостей — под host-тест tests/SeedTest.java.
 */
public class Seed
{
    /** Значение для посева или null, если сеять нечего. */
    public static String value (String profileValue)
    {
        if (profileValue == null)
            return null;

        String v = profileValue.trim ();
        return v.isEmpty () ? null : v;
    }

    /** Сеять ли настройку: значения нет в профиле либо она уже задана пользователем. */
    public static boolean shouldSeed (String profileValue, boolean alreadyInPrefs)
    {
        return !alreadyInPrefs && (value (profileValue) != null);
    }
}
```

- [ ] **Step 4: Прогнать тест — зелёный**

Run: `javac -d out/host-tests/java android/app/src/main/java/ru/cobra/zap/afrd/Seed.java tests/SeedTest.java && java -cp out/host-tests/java ru.cobra.zap.afrd.SeedTest`
Expected: `Seed: OK`.

- [ ] **Step 5: Подключить к `run_tests.sh` и перевести `seedOptions`**

В блок Java-набора добавить компиляцию `Seed.java`/`tests/SeedTest.java` и запуск `ru.cobra.zap.afrd.SeedTest`
рядом с `PkgValues` (тем же `&&`-правилом, `|| status=1`).

`MainActivity.seedOptions()`:

```java
        for (String key : seeded)
        {
            String pref = key.replace ('.', '_');

            /* Правило «пустое не сеем, непустое сеем, уже заданное не трогаем» —
             * в Seed (проверяется tests/SeedTest.java). */
            if (!Seed.shouldSeed (conf.getString (key, ""), mOptions.contains (pref)))
                continue;

            if (edit == null)
                edit = mOptions.edit ();
            edit.putString (pref, Seed.value (conf.getString (key, "")));
        }
```

Импорт `ru.cobra.zap.afrd.Seed` в `MainActivity`.

- [ ] **Step 6: Прогнать всё**

Run: `sh tests/run_tests.sh`
Expected: все наборы OK, включая `Seed: OK` и `PkgValues: OK`.

- [ ] **Step 7: Коммит**

```bash
git add android/app/src/main/java/ru/cobra/zap/afrd/Seed.java tests/SeedTest.java \
	tests/run_tests.sh android/app/src/main/java/ru/cobra/zap/afrd/gui/MainActivity.java
git commit -m "test(app): правило посева настроек вынесено в Seed и покрыто host-тестом

Правило «пустое значение профиля не сеем, непустое сеем, уже заданное не
перетираем» жило только в комментарии к seedOptions() и проверялось живьём.
Теперь оно в Android-независимом Seed (tests/SeedTest.java через
tests/run_tests.sh), а MainActivity.seedOptions() его вызывает."
```

---

### Task 3: Избыток записей обрезается с сообщением

**Files:**
* Modify: `android/app/src/main/java/ru/cobra/zap/afrd/PkgValues.java` (метод `trim`)
* Modify: `android/app/src/main/java/ru/cobra/zap/afrd/gui/AppsPickerActivity.java` (`finishWithResult`)
* Modify: `android/app/src/main/res/values/strings.xml`, `values-ru/strings.xml`
* Test: `tests/PkgValuesTest.java`

**Interfaces:**
* Produces: `PkgValues.trim (Map<String,String> values, int max) → LinkedHashMap<String,String>`
  (первые `max` заполненных записей в исходном порядке, записи без числа сохраняются),
  строковый ресурс `apps_value_truncated` с числом отброшенных.
* Consumes: `PkgValues.filledCount` (Task из прошлой ветки).

- [ ] **Step 1: Написать падающие проверки в `tests/PkgValuesTest.java`**

```java
        /* Обрезка: порядок сохраняется, лишние отбрасываются, записи без числа
         * (их демон не хранит) на лимит не влияют. */
        eq ("обрезка", PkgValues.format (PkgValues.trim (
            PkgValues.parse ("a=1 b=2 c=3 d=4 e=5 f=6 g=7 h=8 i=9", MIN, MAX),
            PkgValues.MAX_ENTRIES)), "a=1 b=2 c=3 d=4 e=5 f=6 g=7 h=8");
        eq ("обрезка с пустой", PkgValues.format (PkgValues.trim (
            PkgValues.parse ("z a=1 b=2", MIN, MAX), 2)), "z a=1");
        eq ("обрезка не нужна", PkgValues.format (PkgValues.trim (
            PkgValues.parse ("a=1 b=2", MIN, MAX), PkgValues.MAX_ENTRIES)), "a=1 b=2");
```

- [ ] **Step 2: Прогнать — не компилируется**

Run: `sh tests/run_tests.sh`
Expected: ошибка компиляции `cannot find symbol: method trim`.

- [ ] **Step 3: Реализовать `trim`**

```java
    /**
     * Обрезка до предела записей, которые демон хранит: лишние (сверх max
     * заполненных) отбрасываются, порядок сохраняется, записи без числа
     * остаются — они слотов не занимают.
     */
    public static LinkedHashMap<String, String> trim (Map<String, String> values, int max)
    {
        LinkedHashMap<String, String> out = new LinkedHashMap<> ();
        int filled = 0;

        for (Map.Entry<String, String> e : values.entrySet ())
        {
            boolean hasValue = (e.getValue () != null) && !e.getValue ().isEmpty ();
            if (hasValue)
            {
                if (filled >= max)
                    continue;
                filled++;
            }

            out.put (e.getKey (), e.getValue ());
        }

        return out;
    }
```

- [ ] **Step 4: Применить при сохранении и сообщить о потере**

`AppsPickerActivity.finishWithResult()` для режима `pkg_value`:

```java
        else if (MODE_PKG_VALUE.equals (mMode))
        {
            LinkedHashMap<String, String> kept = PkgValues.trim (mValues, PkgValues.MAX_ENTRIES);
            int dropped = PkgValues.filledCount (mValues) - PkgValues.filledCount (kept);
            if (dropped > 0)
                Toast.makeText (this, getString (R.string.apps_value_truncated, dropped,
                    PkgValues.MAX_ENTRIES), Toast.LENGTH_LONG).show ();

            out.putExtra (RESULT_VALUE, PkgValues.format (kept));
        }
```

Строки (`values-ru` — как ниже, `values` — по-английски):

```xml
    <string name="apps_value_truncated">Демон хранит только %2$d записей: %1$d не поместилось.
        Эти записи убраны из настройки.</string>
```

- [ ] **Step 5: Прогнать тесты и собрать**

Run: `sh tests/run_tests.sh && cd android && JAVA_HOME=/usr/lib/jvm/java-11-openjdk-amd64 ./gradlew --offline assembleDebug`
Expected: тесты зелёные, `BUILD SUCCESSFUL`, предупреждений нет.

- [ ] **Step 6: Коммит**

```bash
git add android/app/src/main/java/ru/cobra/zap/afrd/PkgValues.java \
	android/app/src/main/java/ru/cobra/zap/afrd/gui/AppsPickerActivity.java \
	tests/PkgValuesTest.java android/app/src/main/res/values/strings.xml \
	android/app/src/main/res/values-ru/strings.xml
git commit -m "feat(app): избыток записей mode.min.hz.list обрезается с сообщением

Демон хранит не больше восьми записей (MODE_MIN_HZ_MAX), а GUI отдавал
значение целиком: лишние тихо пропадали. Теперь значение обрезается до
предела демона (PkgValues.trim, порядок сохраняется, записи без числа слотов
не занимают) и пользователь получает сообщение, сколько записей не поместилось."
```

---

### Task 4: Имя пакета длиннее 63 байт не обрезается

**Files:**
* Modify: `afrd.h` (общий `PKG_NAME_LEN`)
* Modify: `afrd.c` (`MODE_MIN_HZ_PKG_LEN`, `g_cur_pkg` ×2, `video_pkg`)
* Modify: `fps_class.c` (`pkg [64]` ×2)
* Modify: `android/app/src/main/java/ru/cobra/zap/afrd/PkgValues.java` (`MAX_PKG_LEN`)
* Modify: `android/app/src/main/java/ru/cobra/zap/afrd/gui/AppsPickerActivity.java` (`applyValue`)
* Modify: `android/app/src/main/res/values-ru/strings.xml`, `values/strings.xml`
* Test: `tests/test_pkgdet.c`, `tests/PkgValuesTest.java`

**Interfaces:**
* Produces: `PKG_NAME_LEN` (C, 256) — размер буфера имени пакета во всём демоне;
  `PkgValues.MAX_PKG_LEN` (Java, 255) — предел для имени в настройке.
* Consumes: `pkgdet_parse (dump, out, out_sz)` (существующий).

- [ ] **Step 1: Написать падающие проверки**

`tests/test_pkgdet.c` — кейс с длинным именем (в стиле существующих):

```c
	/* длинное имя пакета (система принимает такие: проверено установкой APK
	 * с именем 171 символа) должно разбираться целиком, а не обрезаться */
	{
		static char dump [512], out [64];
		snprintf (dump, sizeof (dump),
		    "    topResumedActivity=ActivityRecord{abc123 u0 %s/.Main t42}", LONG_PKG);
		if (!pkgdet_parse (dump, out, sizeof (out)) || (strcmp (out, LONG_PKG) != 0))
		{
			fprintf (stderr, "test_pkgdet: длинное имя пакета разобрано неверно: [%s]\n", out);
			return 1;
		}
	}
```

где `LONG_PKG` — строка 171 символа (`com.reyter.` + сегменты по 30 символов, как в зонде).

`tests/PkgValuesTest.java` — сверка Java-константы с C-константой и проверка непригодного имени:

```java
        /* Предел длины имени пакета: Java-константа обязана совпадать с
         * PKG_NAME_LEN из afrd.h — иначе GUI пропустит имя, которое демон
         * обрежет, и порог для такого плеера молча не сработает. */
        if (!Files.readString (Path.of ("afrd.h")).contains ("PKG_NAME_LEN 256"))
        {
            System.err.println ("PkgValues: PKG_NAME_LEN в afrd.h не 256 — сверь MAX_PKG_LEN");
            System.exit (1);
        }
        if (PkgValues.MAX_PKG_LEN != 255)
        {
            System.err.println ("PkgValues: MAX_PKG_LEN ожидалось 255");
            System.exit (1);
        }
```

- [ ] **Step 2: Прогнать — падают**

Run: `sh tests/run_tests.sh`
Expected: `test_pkgdet` — длинное имя разобрано неверно (обрезано); `PkgValues` — `PKG_NAME_LEN в afrd.h не 256`.

- [ ] **Step 3: Ввести общий размер и заменить буферы**

`afrd.h` (рядом с прочими общими константами):

```c
/* Размер буфера имени пакета приложения. 63 байт не хватало: система принимает
 * имена заметно длиннее (проверено установкой APK с именем 171 символа), а
 * обрезанное имя не совпадает с пакетом переднего плана — настройки для такого
 * плеера (mode.min.hz.list, fps.correct.*, switch.skip.pkg) молча не работают. */
#define PKG_NAME_LEN	256
```

`afrd.c`: `#define MODE_MIN_HZ_PKG_LEN PKG_NAME_LEN`, `static char g_cur_pkg [PKG_NAME_LEN];` (оба места),
`char video_pkg [PKG_NAME_LEN] = { 0 };`.
`fps_class.c`: оба `char pkg [64];` → `char pkg [PKG_NAME_LEN];` (плюс `#include "afrd.h"`, если ещё нет).

- [ ] **Step 4: Прогнать тесты — `test_pkgdet` зелёный**

Run: `sh tests/run_tests.sh`
Expected: `test_pkgdet: OK`; `PkgValues` всё ещё падает (Java-константы ещё нет).

- [ ] **Step 5: Добавить предел в GUI**

`PkgValues`:

```java
    /** Предел длины имени пакета — совпадает с PKG_NAME_LEN в afrd.h. */
    public static final int MAX_PKG_LEN = 255;
```

`AppsPickerActivity.applyValue()` — перед записью:

```java
        if (pkg.length () > PkgValues.MAX_PKG_LEN)
        {
            Toast.makeText (this, getString (R.string.apps_value_pkg_too_long,
                PkgValues.MAX_PKG_LEN), Toast.LENGTH_LONG).show ();
            return;
        }
```

Строка `apps_value_pkg_too_long`: «Имя пакета длиннее %1$d символов — демон такое имя не применит».

- [ ] **Step 6: Прогнать всё и собрать**

Run: `sh tests/run_tests.sh && ./mka && cd android && JAVA_HOME=/usr/lib/jvm/java-11-openjdk-amd64 ./gradlew --offline assembleDebug`
Expected: тесты зелёные, сборки без предупреждений.

- [ ] **Step 7: Живая проверка на `.50`**

```bash
A=192.168.88.50:5555
PKG=com.reyter.verylongsegmentverylongsegmentverylongsegmentverylongsegmentverylongsegmentverylongsegment.probe
adb -s $A install -r /tmp/pkgprobe/probe.apk          # зонд из этой сессии (107 символов)
# в ini: mode.min.hz.list=<PKG>=50 ; log.enable=1
adb -s $A shell 'su -c "grep -F -A2 \"minimal mode rates\" /data/local/afrd.log | tail -3"'
```

Expected: в журнале полное имя `+ <PKG>: 50.00 Гц или выше` (без обрезки), тогда как до правки было
`…verylon`. Затем убрать зонд из настройки и удалить APK зонда.

- [ ] **Step 8: Коммит**

```bash
git add afrd.h afrd.c fps_class.c tests/test_pkgdet.c tests/PkgValuesTest.java \
	android/app/src/main/java/ru/cobra/zap/afrd/PkgValues.java \
	android/app/src/main/java/ru/cobra/zap/afrd/gui/AppsPickerActivity.java \
	android/app/src/main/res/values/strings.xml android/app/src/main/res/values-ru/strings.xml
git commit -m "fix(daemon,app): имя пакета длиннее 63 байт больше не обрезается

Буферы имени пакета были 64 байта (MODE_MIN_HZ_PKG_LEN, g_cur_pkg, video_pkg,
fps_class), поэтому длинное имя обрезалось и не совпадало с пакетом переднего
плана: настройки для такого плеера (mode.min.hz.list, fps.correct.*, гейт
switch.skip.pkg) молча не работали. Проверено, что система принимает имена
длиннее (APK с именем 171 символа ставится и резолвится). Введён общий
PKG_NAME_LEN 256 в afrd.h; в GUI — предупреждение, если имя всё же длиннее."
```

---

### Task 5: Документация и исследование на ATV9

**Files:**
* Modify: `README.md` (строки `fps.measure.envelope.early.ms`, `fps.cadence.fix.ms`, `mode.min.hz.list`)
* Modify: `ChangeLog-ru.txt`, `ChangeLog-en.txt`
* Regenerate: `android/app/src/main/res/raw*/changelog.txt` (через `./mka`)

- [ ] **Step 1: README** — к строке `mode.min.hz.list` добавить предел записей (восемь) и что избыток
  обрезается с сообщением; отметить, что имя пакета применяется целиком (до 255 символов).
- [ ] **Step 2: ChangeLog** — строки про инертные ключи, `Seed`, обрезку и длину имени пакета.
- [ ] **Step 3: `./mka`, проверка копий** — `cmp ChangeLog-ru.txt android/app/src/main/res/raw-ru/changelog.txt` и то же для en.
- [ ] **Step 4: Исследование на `.76`**: зажимает ли YouTube частоту (методика — спека §5). Отчёт в
  леджер и владельцу, правок кода по итогам не делаем.
- [ ] **Step 5: Коммит** документации и отчёта об исследовании.
