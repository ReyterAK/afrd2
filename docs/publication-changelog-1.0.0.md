# Публикация 1.0.0: раздел «История версий» и чек-лист

> Заготовка к публикации. Текст ниже утверждён владельцем (журнал §31.9) и **применяется в момент
> публикации**: сейчас `ChangeLog-ru.txt`/`ChangeLog-en.txt` намеренно не тронуты (решение владельца —
> не переписывать историю до выхода). Факты и чек-лист сверены с репозиторием на 2026-10-07.

## Чек-лист (все пункты — одним коммитом, в день публикации)

| # | Файл | Действие |
|---|---|---|
| 1 | `main.c:37` | `g_version` = `"1.0.0"` (единственный источник правды) |
| 2 | `./mka` | обновит `versionName`/`versionCode` в `android/app/build.gradle`, `version`/`versionCode` в `install/magisk/module.prop`, строку версии в `README.md`, перекопирует ChangeLog/FAQ/профили в `res/raw*` |
| 3 | `ChangeLog-ru.txt`, `ChangeLog-en.txt` | удалить верхний раздел и 13 блоков «0.3.3», вставить раздел `1.0.0` (текст ниже) |
| 4 | `README.md` | при необходимости уточнить строку версии (её правит `./mka`) |
| 5 | — | `sh tests/run_tests.sh` (в нём страж `tests/test_versions.sh`: не пропустит выпуск без раздела 1.0.0 в обоих ChangeLog) |
| 6 | — | `cd android && ./gradlew assembleRelease` (подпись своим ключом, см. раздел про подпись в README) |
| 7 | — | `sh install/magisk/build-module.sh` (zip модуля) |

Перед публикацией: живая приёмка на стенде и приёмка Magisk-модуля (решение владельца — обязательны).

## Раздел для `ChangeLog-ru.txt`

```
AFRd2 — форк AFRd (основа: AFRd 0.3.2, Андрей Заболотный, anpaza @ 4pda).
Лицензия GPL-3. Подробности и ограничения — README.

1.0.0 <дата публикации>

    Первый публичный билд форка. Ниже — отличия от AFRd 0.3.2; дальнейшие
    версии получают обычные записи об изменениях.

    Android 12-14 (ядра 5.x) — новый путь работы:

    * Режим HDMI переключается штатным механизмом SurfaceFlinger, поэтому
      остаётся согласованным с системой: картинка и меню не рассинхронизируются.
      На прошивках, где этот механизм недоступен (в том числе на Android 10 и 11,
      где служебный вызов занят другой операцией), демон пишет об этом в журнал и
      систему не трогает.

    * Частота определяется по счётчику кадров декодера — по нижней огибающей
      подокон наблюдения, а не по одному окну: короткие всплески и повторные
      кадры не сбивают решение. Если плеер сообщает частоту сам (frame_rate_hint),
      она имеет приоритет.

    * Поведение с режимом настраивается: на паузе он по умолчанию удерживается,
      при остановке видео — возвращается исходный (оба поведения переключаются).

    * Демон проверяет, что кадры действительно доходят до экрана, и исправляет
      «половинную подачу»; в журнал пишется и случай, когда темп задаёт сам плеер
      (такому плееру AFR мешает — см. ниже).

    * Завершение работы плеера и залипший канал декодера распознаются: режим не
      остаётся чужим, если видео закончилось или приложение ушло с экрана.

    * Правила выбора цветового пространства (cs.select) применяются и здесь —
      через встроенный сервис производителя. Атрибуты HDR/SDR демон читает и
      показывает, но не меняет: ими управляют система и плеер.

    * Приложение следит за демоном и поднимает его, если тот остановился,
      снимая залипший флаг переключения.

    Android 6-9 — прежнее поведение AFRd; перенесены общие настройки:

    * «Плееры, которым AFR мешает» (switch.skip.pkg): для выбранных плееров демон
      измеряет частоту и пишет всё в журнал, но режим не переключает. Такие плееры
      фиксируют темп отрисовки на старте ролика, и удержанный от предыдущего ролика
      режим «зажимает» следующий — именно это ломало листание роликов. По умолчанию
      в списке штатный YouTube.

    * «Минимальная частота режима для плееров» (mode.min.hz.list): ниже указанной
      частоты режимы для этих плееров не выбираются.

    Приложение:

    * Название AFRd2, пакет ru.reyter.afrd2.

    * Плееры выбираются из списка установленных приложений — имя пакета с пульта
      больше не вводится; там же задаются поправки частоты по плееру.

    * Настройки, которые на текущей версии Android не действуют, помечены прямо
      в приложении, а при старте демон называет их в журнале.

    * Конфигурация перечитывается без остановки демона (reconf), настройки
      применяются без перезапуска.

    Ограничения (осознанные) — в README: демон меняет только частоту, разрешение
    и HDCP не трогает, HDR не управляет, экран на время переключения не гасится.

Оригинальный AFRd и его история версий — github.com/anpaza/afrd.
```

## Раздел для `ChangeLog-en.txt`

```
AFRd2 — a fork of AFRd (based on AFRd 0.3.2 by Andrey Zabolotnyi,
anpaza @ 4pda). License: GPL-3. Details and limitations: see README.

1.0.0 <publication date>

    First public build of the fork. Below are the differences from AFRd 0.3.2;
    later versions get ordinary change entries.

    Android 12-14 (kernel 5.x) — the new code path:

    * The HDMI mode is switched by the stock SurfaceFlinger mechanism, so it stays
      in sync with the system: video and UI never disagree. On firmware without
      that mechanism (including Android 10 and 11, where the service call is taken
      by a different operation) the daemon says so in the log and leaves the
      system alone.

    * The frame rate is measured from the decoder frame counter using the lower
      envelope of observation sub-windows rather than a single window, so short
      spikes and repeated frames do not spoil the decision. If the player reports
      the rate itself (frame_rate_hint), its value wins.

    * Mode behaviour is configurable: on pause the mode is held by default, on
      video stop the original mode is restored (both are switchable).

    * The daemon verifies that frames actually reach the screen and fixes
      "half-rate" output; it also logs the case where the player itself sets the
      pace (AFR gets in the way of such a player — see below).

    * Player exit and a stuck decoder channel are detected: the mode is not left
      behind when the video ends or the app leaves the screen.

    * Colour space rules (cs.select) apply here too, through the vendor service.
      HDR/SDR attributes are read and shown but never changed: the system and the
      player own them.

    * The app watches the daemon and restarts it if it stops, clearing a stuck
      mode-switch flag.

    Android 6-9 — the previous AFRd behaviour; shared settings were ported:

    * "Players AFR gets in the way of" (switch.skip.pkg): for the selected players
      the daemon measures the rate and logs everything but does not switch the
      mode. Such players fix their render pace at clip start, and a mode held from
      the previous clip throttles the next one — which broke clip browsing. The
      stock YouTube is in the list by default.

    * "Minimum mode rate for players" (mode.min.hz.list): modes below the given
      rate are not chosen for those players.

    App:

    * Name AFRd2, package ru.reyter.afrd2.

    * Players are picked from the list of installed apps — the package name is no
      longer typed with the remote; per-player rate corrections are set there too.

    * Settings that do not apply on the current Android version are marked in the
      app, and the daemon names them in the log at startup.

    * The configuration is re-read without stopping the daemon (reconf), so
      settings apply without a restart.

    Known limitations (by design) — see README: the daemon changes the frame rate
    only, never resolution or HDCP, does not manage HDR, and does not blank the
    screen while switching.

The original AFRd and its version history — github.com/anpaza/afrd.
```
