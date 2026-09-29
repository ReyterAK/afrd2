Auto Frame Rate Daemon (AFRD)
-----------------------------

AFRd2: Android 10–14 и Android TV 14 (ядро 5.x, Amlogic S905X5M)
----------------------------------------------------------------

Этот репозиторий — форк [AFRd](https://github.com/anpaza/afrd) (upstream 0.3.2).
Форк аддитивный: путь для Android 6–9 (ядра 3.14/4.9) сохранён без изменений, а
для Android 10–14 добавлен рабочий — там декодер больше не рассылает uevent'ы, а
запись в `/sys/class/display/mode` режим не переключает. Нужная ветка выбирается
на старте автоматически (`platform_detect()`): если есть `/sys/class/vdec/vdec_status`
и доступен backdoor SurfaceFlinger — работает новая ветка, иначе прежняя.

Что делает новая ветка:

* следит за декодером, опрашивая `/sys/class/vdec/vdec_status` (`device name` —
  признак видео, рост `frame count` — что видео не на паузе);
* определяет частоту по приросту `frame count` (поля `frame rate`/`frame dur` на
  ядрах 5.x врут), а если есть — по подсказке плеера через API (`fps.prefer.api`);
* переключает режим backdoor-транзакцией SurfaceFlinger
  (`service call SurfaceFlinger 1035 i32 <modeId>`), поэтому SurfaceFlinger и HDMI
  остаются синхронными;
* возвращает исходный режим на остановке видео и снимает флаг backdoor (также по
  `SIGTERM`/`SIGINT`, на старте демона и по таймауту «видео потеряно»);
* сохраняет API UDP 50505 (`frame_rate_hint`, `refresh_rate`, `status`, ...).

### Сборка

Нужен NDK (проверен r26d):

```sh
NDK=$HOME/Android/Sdk/android-ndk-r26d ./mka          # оба ABI
AFRD_ABIS=armeabi-v7a ./mka                           # только бинарник для бокса
```

Результат для бокса — `libs/armeabi-v7a/afrd` (ELF 32-bit ARM, API 21,
зависимости только libc/libm/libdl). `./mka` без аргументов дополнительно
раскладывает бинарники и профили в `android/app/src/main/res/raw/` (для сборки
APK) и правит `versionName` в `android/app/build.gradle`.

### Установка и запуск

Демону нужен root (чтение sysfs и backdoor SurfaceFlinger). Установка — вариант B,
как в оригинале: бинарник кладётся на устройство и запускается под root;
автозапуск появится вместе с приложением (отдельный этап).

```sh
D=192.168.88.50:5555
adb connect $D
adb -s $D root        # если прошивка разрешает adb root; иначе выполняйте команды через su
D=$D sh tools/stand/build_push.sh
```

`build_push.sh` делает всё сразу: снимает прошлый экземпляр демона, копирует
`libs/armeabi-v7a/afrd` → `/data/local/tmp/afrd` и `config/afrd2.ini` →
`/data/local/tmp/afrd.ini`, делает бинарник исполняемым, запускает
`/data/local/tmp/afrd -v -D /data/local/tmp/afrd.ini` и печатает pid с хвостом лога.

Те же шаги вручную:

```sh
adb -s $D push libs/armeabi-v7a/afrd /data/local/tmp/afrd
adb -s $D push config/afrd2.ini /data/local/tmp/afrd.ini
adb -s $D shell chmod 755 /data/local/tmp/afrd
adb -s $D shell '/data/local/tmp/afrd -D /data/local/tmp/afrd.ini'
```

Полезное:

```sh
adb -s $D shell 'pidof afrd'                                          # демон жив?
adb -s $D shell 'tail -20 /data/local/afrd.log'                       # лог (log.file; прошлый — afrd.log~)
adb -s $D shell '/data/local/tmp/afrd -k'                             # остановить (pid-файл /dev/run/afrd.pid)
adb -s $D shell '/data/local/tmp/afrd -k -D /data/local/tmp/afrd.ini' # перезапуск (нужен после правки конфига)
adb -s $D shell '/data/local/tmp/afrd -s'                             # статистика (shmem /dev/run/afrd.ipc)
adb -s $D shell 'echo status | nc -u -w1 127.0.0.1 50505'             # статус через API
```

### Профиль config/afrd2.ini

Ключи, которые использует новая ветка (полный список — в самом файле):

| ключ | смысл |
|---|---|
| `enable` | включить демон |
| `log.file`, `log.enable` | файл лога (по умолчанию `/data/local/afrd.log`) |
| `trigger.poll.ms` | период опроса `vdec_status` |
| `trigger.start.debounce.ms`, `trigger.stop.debounce.ms` | дебаунс старта/стопа видео |
| `fps.measure.window.ms`, `fps.measure.retries` | окно и число попыток измерения частоты |
| `fps.prefer.api` | предпочитать подсказку плеера (`frame_rate_hint`) собственному замеру |
| `mode.prefer.exact`, `mode.use.fract` | выбор режима (как в upstream) |
| `switch.delay.on`, `switch.delay.off` | задержки переключения и возврата |
| `switch.restore.on.stop` | возвращать исходный режим при остановке видео |
| `pause.keep` | 1 — на паузе режим удерживать (по умолчанию), 0 — возвращать исходный |

### Как убедиться, что AFR работает

```sh
sh tools/stand/check_mode.sh     # другой бокс: D=<ip:port> sh tools/stand/check_mode.sh
```

печатает одну строку: `kernel mode | HDMI timing | SF activeMode | backdoor flag`.

Если проиграть клип 23.976 fps в VLC (у него нет своего AFR), ожидается (эталон
плана; на стенде прогоном пока не подтверждён — см. «Статус приёмки»):

* до старта: `1080p60hz | ... | activeMode=59.94 Hz | debugDisplayModeSetByBackdoor=false`
* во время: `1080p24hz | ... | activeMode=23.98 Hz | debugDisplayModeSetByBackdoor=true`
* после остановки: исходный режим и `backdoor=false`

Матрица приёмки и дополнительные проверки:

```sh
sh tools/stand/acceptance_matrix.sh                        # 23.976/25/50: удержание и возврат
PAUSE_TEST=1 sh tools/stand/acceptance_matrix.sh           # + пауза (PAUSE_EXPECT=restore для pause.keep=0)
OWN_AFR_TEST=1 sh tools/stand/acceptance_matrix.sh         # + плеер со своим AFR
API_TEST=1 sh tools/stand/acceptance_matrix.sh             # + API, подсказка плеера, сброс счётчика (R34в)
LOG=acceptance.log sh tools/stand/acceptance_matrix.sh     # + протокол в файл
sh tools/stand/guard_test.sh                               # SIGTERM: режим возвращён, флаг снят
```

Клипы ожидаются в `/sdcard/Download/afr/` (23.976/24/25/50/60 fps). Скрипты сами
печатают `ls -l` этого каталога и пропускают отсутствующие клипы как `SKIP`, а не
выдают их за пройденные. Полезные переменные: `D` (адрес устройства),
`CLIPS`/`CLIP_DIR` (что играть), `ORIG_MODE` (куда обязан вернуться демон,
по умолчанию `1080p60hz`).

### Статус приёмки

Код порта завершён и отревьюирован, скрипты приёмки готовы. **Живая приёмка не
выполнена**: стенд `192.168.88.50:5555` недоступен (`No route to host`, ping без
ответа), поэтому ни матрица, ни `guard_test.sh` на устройстве не прогонялись. Всё,
что в этом разделе помечено как «ожидается», — эталон из плана, а не результат
измерения.

### Ограничения (осознанные, а не баги)

1. **Цветовые пространства и HDR не переключаются.** На ATV14-пути цвета (`attr`,
   `dc_cap`, ключ `cs.select`) не применяются — цветами управляет система/плеер.
   Тема отдельного этапа.
2. **HDCP не управляется.** Узлов `/sys/module/hdmitx20/...` на ядрах 5.x нет,
   HDCP-ветка upstream здесь не работает (тоже тема отдельного этапа).
3. **`reconf` и `color_space` через API не действуют.** Перечитки конфига на ходу
   в новой ветке нет: после правки `afrd.ini` демон нужно перезапустить
   (`/data/local/tmp/afrd -k -D /data/local/tmp/afrd.ini`).
4. **Нужен backdoor SurfaceFlinger.** Если прошивка его не поддерживает (другой
   вендор, будущие версии Android), демон пишет в лог «backdoor SurfaceFlinger
   недоступен — пассивный режим» и AFR не работает.
5. **Меняется только частота.** Разрешение не трогается (режим ищется для текущего
   разрешения), экран на время переключения не гасится — дешёвого способа на ATV14
   нет, экран моргает как при обычной смене режима.
6. **Только Amlogic.** На не-AmLogic устройствах и на Android < 10 работает
   upstream-ветка (как раньше).

Ниже — описание upstream (Android 6–9), оно сохранено как есть.

This is a Linux daemon for AMLogic SoC based boxes to switch screen
framerate to most suitable during video play. The daemon uses kernel
uevent-based notifications, available in AMLogic 3.14 kernels from
summer 2017. Earlier kernels won't emit notifications at the start
and end of playing video, so the daemon won't work.

Unfortunately, this uevent is gone from kernel 4.9 (used for
Android 7 and 8), so afrd can't use frame rate notifications
on newer kernels. Instead, afrd will use video decoder notifications
that are emitted at the start and end of playback.

The daemon can be linked either with BioniC (for Android) or with
glibc (for plain Linux OSes).

Versions up to 0.1.* use kernel uevent notifications emmited by
AMLogic kernels starting from summer 2017, used in some Android 6.0
and 7.0 firmwares. Unfortunately, somewhere around start of 2018
AMLogic broke its own code and since then these uevents are not
generated (altough part of the respective code is still in kernel).
Here are a example FRAME_RATE_HINT uevent generated when a 29.976
fps movie is started:
```
change@/devices/virtual/tv/tv
    ACTION=change
    DEVPATH=/devices/virtual/tv/tv
    SUBSYSTEM=tv
    FRAME_RATE_HINT=3203
    MAJOR=254
    MINOR=0
    DEVNAME=tv
    SEQNUM=2787
```
and when the movie stops:
```
change@/devices/virtual/tv/tv
    ACTION=change
    DEVPATH=/devices/virtual/tv/tv
    SUBSYSTEM=tv
    FRAME_RATE_END_HINT
    MAJOR=254
    MINOR=0
    DEVNAME=tv
    SEQNUM=2788
```
Since version 0.2.0 afrd supports detecting movie framerate by
catching uevent's generated when a hardware decoder is started
or stopped. These events are present on both older and newer
kernels, thus afrd can work with almost every AMLogic kernel
out there:
```
add@/devices/vdec.25/amvdec_h264.0
    ACTION=add
    DEVPATH=/devices/vdec.25/amvdec_h264.0
    SUBSYSTEM=platform
    MODALIAS=platform:amvdec_h264
    SEQNUM=2786

remove@/devices/vdec.25/amvdec_h264.0
    ACTION=remove
    DEVPATH=/devices/vdec.25/amvdec_h264.0
    SUBSYSTEM=platform
    MODALIAS=platform:amvdec_h264
    SEQNUM=2789
```
These events do not contain the movie frame rate, thus in addition
to these events afrd has to query the video decoder driver, its status
is available from /sys/class/vdec/vdec_status:
```
vdec channel 0 statistics:
  device name : amvdec_h264
  frame width : 1920
 frame height : 1080
   frame rate : 24 fps
     bit rate : 856 kbps
       status : 63
    frame dur : 4000
   frame data : 19 KB
  frame count : 230
   drop count : 0
fra err count : 0
 hw err count : 0
   total data : 1197 KB
```
The "frame dur" field is used if not 0 (the actual frame rate is
96000/frame_dur), and "frame rate" field is used otherwise
(with 23 fps being 23.976, 29 being 29.970 and 59 being 59.94 fps).

Configuration file
------------------

AFRD uses a simple configuration file that can be used to customize
its behavior. The file contains either comment lines, starting with
the '#' character, or key=value pairs.

If a key contains several values, list elements are always separated
by white spaces.

Every 5 seconds afrd will check the last-modified timestamp on the
loaded config file. If config file is changed, afrd reloads it.

The following parameters are recognized by AFRD:

* *hdmi.dev*
    This keyword defines the sysfs directory with HDMI driver attributes.
    Usually has the value /sys/class/amhdmitx/amhdmitx0.

* *hdmi.state*
    This defines the sysfs file used to check if HDMI is enabled or not.
    Usually has the value /sys/class/switch/hdmi/state.

* *switch.delay.on*
* *switch.delay.off*
    The time to delay a mode switch when video starts (on) or stops (off).
    The 'on' delay is usually small, while 'off' should be relatively big
    to avoid unneeded mode switches at position change or when you're
    watching a series of videos with same framerate.

    Setting a parameter to zero will disable the respective operation.
    For example, if you want to completely disable restoration of the
    original refresh rate, set switch.delay.off to 0.

    The time is given in milliseconds.

* *switch.delay.retry*
    Sometimes video decoder doesn't report movie frame rate correctly.
    In these cases afrd will wait some time and try the query again.
    This keyword defines the time interval between these tries.

    The time is given in milliseconds.

* *switch.timeout*
    If movie frame rate cannot be safely determined for this period
    of time, afrd will give up trying.

* *switch.blackout*
    When movie starts after this period of time the screen will be
    disabled to minimize flicker. Don't set it too low as in some
    circumstances screen may be blackened when it shouldn't be.

* *switch.ignore*
    If time interval between a STOP/PLAY operation is less than given
    amount of milliseconds, afrd will not re-evaluate movie fps. This
    is useful to avoid suddent refresh rate switches due to wrong fps
    evaluation during movie rewind operations.

* *switch.hdmi*
    Number of milliseconds to delay HDMI hot-plug event handling.
    When HDMI is plugged out, afrd will clear the list of supported
    video modes, and when it is plugged in, afrd will query the list
    of supported modes (which is retrieved from TV EDID data), but it
    has to wait a little so that things settle down (till kernel driver
    does its job).

    Some strange TVs will simulate a HDMI off/on event pair on every
    refresh rate switch, this can be seen in afrd log. In this case
    this parameter should be either 0 (which means to ignore HDMI
    hotplug events), or larger than the "HDMI off" period.

* *mode.path*
    Points to sysfs file used to switch current video mode.
    This is usually /sys/class/display/mode.

* *mode.prefer.exact*
    If set is 1, AFRD will prefer exact match of video mode refresh rate
    to movie frame rate. If 0 (default setting), AFRD will prefer the
    video mode with highest refresh rate which is a exact multiple of
    movie frame rate.

    Default value is 0.

* *mode.use.fract*
    Choose how AFRD handles fractional frame rates (23.976, 29.97, 59.94 fps):

    * 0 - Use both fractional and integer refresh rates as appropiate
    * 1 - Prefer fractional refresh rates, even if movie says it uses integer
    * 2 - Prefer integer refresh rates, even if movie says it uses fractional

    This can be used to limit the number of mode switches, e.g. when your OS
    is set up to use 59.94Hz refresh rate (the default on AndroidTV boxes with
    Android 8+) to avoid a mode switch when watching 30 or 60Hz videos
    you can set this to 1.

* *mode.blacklist.rates*
    This option allows you to blacklist some refresh rates. For example,
    AMLogic kernels have a bug in the HDMI driver that sets the 29.976 mode
    wrong; using this refresh mode causes severe jittering on 23.976 movies.
    Thus, this refresh rate is blacklisted by default.

    This is a list of numbers separated by spaces.

* *mode.extra*
    Normally afrd takes the list of supported video modes from EDID info blob
    fetched via HDMI from your TV. Sometimes this info doesn't contain all the
    supported modes. You can put a list of additional supported video modes
    that aren't listed

* *vdec.sysfs*
    This points to sysfs directory containing the status of the video decoder.
    afrd will try to use attributes vdec_status, dump_vdec_blocks, dump_vdec_chunks.

* *uevent.filter.frhint*
* *uevent.filter.vdec*
* *uevent.filter.hdmi*
    These sets the filters for kernel uevents we handle:
    * *frhint* for the FRAMERATE_HINT uevent generated on some older and patched
        kernels when video decoder detects a change of framerate in played video
    * *vdec* for uevents generated by the video decoder when a hardware decoder
        is activated or deactivated
    * *hdmi* for uevents generated when we plug in or out the HDMI connector.

    Event filters use the following format:

    uevent.filter.XXX=&lt;keyword&gt;=&lt;regex&gt;[ &lt;keyword&gt;=&lt;regex&gt;...]

    The regular expressions are "extended regular expressions" and are matched
    against the whole attribute value. E.g. regular expression "plat" will not
    match the string "platform", while "platform" or "plat.*" will.

    Regular expressions cannot contain space characters, which are used to
    delimit attribute filters from each other. If you need a space, use the
    [[:space:]] regular expression.

* *frhint.vdec.blacklist*
    A list of video decoders to ignore FRAME_RATE_HINT events from. For example,
    the amvdec_h265 codec will always report 26 fps on every video in a
    FRAME_RATE_HINT event. By ignoring this FRAME_RATE_HINT event you give
    a chance to afrd to use other sources to determine the correct frame rate.


AFRd API
--------

AFRd has an API which can be used to cooperate with video player software in order
to get a better user experience. In particular, software may tell afrd in advance
what is the frame rate of the video, so that afrd doesn't have to guess it.

The API is available by sending UDP datagrams to address 127.0.0.1, port 50505.
The protocol is purely textual, each datagram may contain multiple commands,
each command on a separate line.

The format of each line is:

	<command> [<arguments> ...]

The following commands are available:

* *help*
    display a list of supported commands and their short description

* *frame_rate_hint* <fr>
    tell afrd the video starting in less than one second will use
    <fr>/1000 frames per second (e.g. 23976 = 23.976 fps).

* *refresh_rate* <rr>
    tell afrd to set display refresh rate as close to <rr>/1000 Hz
    as possible immediately, no arg to restore original rate.

* *color_space* <cs>
    override colorspace, empty arg to restore default behavior.
    The <cs> specificator may contain, separated by comma:
    * The color space itself (one of rgb, 444, 422, 420)
    * The color depth (8bit, 10bit, 12bit, 16bit)
    * The color range (limit or full)
    Color space takes effect on next refresh rate switch.

* *status*
    get current afrd status

* *reconf*
    tell afrd to reload configuration file as soon as possible

To test the API you may use the 'nc' tool that is part of busybox, which can be
easily installed if you didn't already. Example dialog with afrd, lines starting
with '>' are outgoing, others are incoming:

```
1|u211:/ # nc -u 127.0.0.1 50505
>help
frame_rate_hint <fr>
	tell afrd the video starting in <1.0 seconds will use <fr>/1000 frames per second (e.g. 23976 = 23.976 fps)
refresh_rate <rr>
	tell afrd to set display refresh rate as close to <rr>/1000 Hz as possible, no arg to restore original rate
color_space <cs>
	override colorspace, empty arg tor restore default behavior
status
	get current afrd status
reconf
	tell afrd to reload configuration file as soon as possible

>status
enabled:1
active:0
blackened:0
version:0.2.5
build:2019-03-31 21:17:38
current hz:50000
original hz:50000

>refresh_rate 59940

>status
enabled:1
active:1
blackened:0
version:0.2.5
build:2019-03-31 21:17:38
current hz:59941
original hz:50000

>refresh_rate

>status
enabled:1
active:0
blackened:0
version:0.2.5
build:2019-03-31 21:17:38
current hz:50000
original hz:50000

>color_space rgb,8bit,full
>refresh_rate 50000
```

Note how afrd remembers original refresh rate when you switch it for the first time,
and how it restores refresh rate when the command 'refresh_rate' without parameters
is issued.
