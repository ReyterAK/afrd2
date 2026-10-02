#!/system/bin/sh
# AFRd2: таргетированный поиск аномалии «FPS переключился, а система не в
# соответствие». Гипотеза: событие фреймворка «видео-слой включён»
# (frameRateDisplay 1 -> HWC MixMode) теряется, если переключение режима
# попадает в момент появления слоя. Тест: старт плеера и смена режима через
# 1035 со сдвигом OFF (перебор), затем смотрим последнее событие HWC.
#
# Запуск (демон ОСТАНОВЛЕН — режим меняем вручную; ТВ включён):
#   adb -s 192.168.88.50:5555 shell 'sh /data/local/tmp/anomaly_race.sh'
#
# Переменные: CLIP, MODE_ID (целевой режим, по умолчанию 141 = 23.98 Гц),
#             ORIG_ID (возврат, по умолчанию 135 = 59.94 Гц).

CLIP=${CLIP:-/sdcard/Download/afr/long_24000_1001.mp4}
PKG=${PKG:-org.videolan.vlc}
START=${START:-org.videolan.vlc/.StartActivity}
MODE_ID=${MODE_ID:-141}
ORIG_ID=${ORIG_ID:-135}
OUT=/data/local/tmp/anomaly_race.txt

: > "$OUT"
say () { echo "$@" | tee -a "$OUT"; }

say "гонка: старт $(date +%H:%M:%S) режим=$(cat /sys/class/display/mode) демон=$(pidof afrd)"

for off in 0 0.3 0.6 1 1.5 2 3 5; do
	am force-stop "$PKG" >/dev/null 2>&1
	service call SurfaceFlinger 1035 i32 "$ORIG_ID" >/dev/null 2>&1
	service call SurfaceFlinger 1035 i32 9999 >/dev/null 2>&1
	logcat -c 2>/dev/null
	am start -n "$START" -a android.intent.action.VIEW -d "file://$CLIP" -t video/mp4 >/dev/null 2>&1
	sleep "$off"
	service call SurfaceFlinger 1035 i32 "$MODE_ID" >/dev/null 2>&1
	sleep 6
	last=$(logcat -d -v time 2>/dev/null | grep 'adjustVsyncMode' | tail -1 | sed 's/.*adjustVsyncMode/adjustVsyncMode/')
	ev=$(logcat -d -v time 2>/dev/null | grep -cE "frameRateDisplay 1,")
	verdict="ОК"
	case "$last" in *HwMode*) verdict="АНОМАЛИЯ" ;; esac
	say "off=${off}s video-on=$ev mode=$(cat /sys/class/display/mode) HWC=$last => $verdict"
	am force-stop "$PKG" >/dev/null 2>&1
	sleep 2
	if [ "$verdict" = "АНОМАЛИЯ" ]; then
		logcat -d -v time > "/data/local/tmp/anomaly_race_off${off}.log" 2>/dev/null
		say "  лог сохранён: /data/local/tmp/anomaly_race_off${off}.log"
	fi
done

am force-stop "$PKG" >/dev/null 2>&1
sleep 2
service call SurfaceFlinger 1035 i32 "$ORIG_ID" >/dev/null 2>&1
service call SurfaceFlinger 1035 i32 9999 >/dev/null 2>&1
say "финал: mode=$(cat /sys/class/display/mode) $(dumpsys SurfaceFlinger 2>/dev/null | grep -m1 -o 'debugDisplayModeSetByBackdoor=[a-z]*')"
say "DONE"
