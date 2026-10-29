# AFRd2 1.0.0 — текст выпуска GitHub

Текст для страницы Releases (тег `1.0.0`). Исходники тега — этот же репозиторий.

---

## AFRd2 1.0.0

Автоподбор частоты обновления HDMI под частоту кадров видео для Amlogic.
Форк [AFRd](https://github.com/anpaza/afrd) 0.3.2 (Андрей Заболотный, anpaza), GPL-3.

Закрывает случай, где штатный AFR не работает: на Android 11–14 (ядра 5.x) декодер
больше не рассылает uevent'ы, а запись в `/sys/class/display/mode` режим не меняет.
Демон опрашивает `/sys/class/vdec/vdec_status`, определяет частоту кадров и
переключает режим системным механизмом — на Android 12–14 служебной транзакцией
SurfaceFlinger, на Android 11 вендорным SystemControl. Путь для Android 6–9
(ядра 3.14/4.9) сохранён без изменений и выбирается автоматически.

**Проверено вживую:** Android 14 (Amlogic S905X5M, ядро 5.15), Android 11
(Amlogic, ядро 5.4) и Android 9 (legacy-путь). Заявка — Android 11–14; на Android 10
ожидается legacy-путь (ядро 4.9), живого прогона нет.

### Требования

* root (Magisk или встроенный `su` в userdebug-прошивке);
* Amlogic, ядро 5.x для нового пути; ядра 3.14/4.9 — прежний путь AFRd;
* для приложения — Android 6.0+ (minSdk 23).

### Установка

Два варианта, одновременно должен работать только один экземпляр демона.

* **APK** (интерфейс, настройки, ЧАВО): `ru.reyter.afrd2-1.0.0-release.apk`,
  пакет `ru.reyter.afrd2`. После установки открыть приложение, дать root и включить
  «Включить автоподбор частоты».
* **Magisk-модуль** (автостарт без приложения): `afrd2-magisk-armeabi-v7a.zip` или
  `afrd2-magisk-arm64-v8a.zip` — Magisk → Модули → «Установить из памяти» → перезагрузка.
  Модуль ничего не пишет в системные разделы.

Подробности, ограничения, сборка из исходников и приёмка — в
[README](https://github.com/ReyterAK/afrd2/blob/master/README.md) и
[ЧАВО](https://github.com/ReyterAK/afrd2/blob/master/FAQ-ru.txt).

### Файлы

| Файл | Размер | MD5 | SHA-256 |
|---|---|---|---|
| `ru.reyter.afrd2-1.0.0-release.apk` | 407 687 Б | `3a53a259c4c4076942ce0f8a5a4b0721` | `90da0db1f2303dea676b209eb4b13a6a5d7311dc498a8739d2fd1829738894a7` |
| `afrd2-magisk-armeabi-v7a.zip` | 55 781 Б | `9ae28a99c6640a6930b39f6de6328f84` | `9b1541c221edff731416ee440e8d4f54329f7953683892bc566e7a5d8a169a16` |
| `afrd2-magisk-arm64-v8a.zip` | 65 007 Б | `3c04a8e8ed6e442ab28b904d56a39aff` | `b7e6bf206c42bdcc81c8606e728f4c0255cdfe9a78686cdfd2709c36795993ab` |

APK подписан релизным ключом; обновления ставятся поверх только той же подписью.
Демон внутри APK и обоих модулей собран из исходников тега `1.0.0`.

### Лицензия

GPL-3 (`COPYING.txt`, перевод — `COPYING-ru.txt`). Оригинал AFRd —
<https://github.com/anpaza/afrd>; тема на 4PDA —
<https://4pda.to/forum/index.php?showtopic=948250>.

Тема поддержки AFRd2 на 4PDA: <https://4pda.to/forum/index.php?showtopic=1127735>.
