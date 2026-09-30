# AFRd2 — Magisk-модуль (автостарт без приложения)

Сборка:

```sh
./mka                              # собрать демон (ABI armeabi-v7a и arm64-v8a)
sh install/magisk/build-module.sh  # собрать out/afrd2-magisk-armeabi-v7a.zip
```

Установка: **Magisk → Modules → Install from storage** → выбрать zip → перезагрузка.
Удаление: Magisk → Modules → Remove (скрипт `uninstall.sh` снимает демон по SIGTERM,
демон сам вернёт исходный режим и сбросит флаг backdoor).

Что делает модуль: `service.sh` ждёт `sys.boot_completed`, снимает прежний экземпляр
демона (SIGTERM) и запускает `$MODDIR/afrd -D $MODDIR/afrd.ini`. Бинарник и конфиг
лежат внутри модуля, системные разделы не трогаются.

Управление: конфиг `afrd.ini` (копия `config/afrd2.ini`) + API UDP 50505
(`status`, `frame_rate_hint`); GUI у модуля нет — это вариант доставки «автостарт без
приложения». Вариант с приложением (APK + `su`) даёт GUI и пультовое управление;
одновременно должен работать только один экземпляр демона.

**Установка модуля меняет состояние ОС бокса** (каталог `/data/adb/modules/afrd2`) —
делать только по решению владельца.
