#!/system/bin/sh
# AFRd2: серия «переключение режима в процессе» — ищем и считаем состояние
# «половинной подачи» (интервалы ~2 периода дисплея, видео «как 13 fps»).
#
# Сценарий владельца: плеер сам выставил частоту (свой AFR), затем демон
# переключает режим — и подача может «залипнуть» вдвое реже.
#
# Запуск (демон ОСТАНОВЛЕН, ТВ включён):
#   adb -s 192.168.88.50:5555 shell 'RUNS=12 sh /data/local/tmp/yt_switch_probe.sh'
#
# Переменные: URL (ролик), WAIT1 (до переключения), WAIT2 (после), MODE_ID.

URL=${URL:-https://www.youtube.com/watch?v=zhApeaHMvfs}
PKG=${PKG:-com.google.android.youtube.tv}
RUNS=${RUNS:-12}
WAIT1=${WAIT1:-10}
WAIT2=${WAIT2:-8}
MODE_ID=${MODE_ID:-141}
ORIG_ID=${ORIG_ID:-135}
OUT=/data/local/tmp/yt_switch.txt
LAT=/data/local/tmp/yt_switch.lat

: > "$OUT"
say () { echo "$@" | tee -a "$OUT"; }

avg_of () { # <файл> -> средний интервал, мс
	awk 'NF == 3 && $2 + 0 > 0 && $2 + 0 < 1e17 { n++; t[n] = $2 + 0 }
	     END { if (n < 3) { print "n/a"; exit }
	           m = 0; sum = 0
	           for (i = 2; i <= n; i++) { d = (t[i] - t[i-1]) / 1e6; if (d <= 0) continue; m++; sum += d }
	           printf "%.1f", sum / m }' "$1"
}

say "серия: старт $(date +%H:%M:%S) демон=$(pidof afrd)"

i=1
while [ "$i" -le "$RUNS" ]; do
	am force-stop "$PKG" >/dev/null 2>&1
	service call SurfaceFlinger 1035 i32 "$ORIG_ID" >/dev/null 2>&1
	service call SurfaceFlinger 1035 i32 9999 >/dev/null 2>&1
	sleep 3
	logcat -c 2>/dev/null
	am start -a android.intent.action.VIEW -d "$URL" >/dev/null 2>&1
	sleep "$WAIT1"

	pre=$(cat /sys/class/display/mode)
	app_rate=$(logcat -d -v time 2>/dev/null | grep -c "setFrameRate")
	L=$(dumpsys SurfaceFlinger --list 2>/dev/null | grep 'SurfaceView\[' | grep -i blast | head -1)
	[ -n "$L" ] && dumpsys SurfaceFlinger --latency "$L" > "$LAT.before$i" 2>&1

	service call SurfaceFlinger 1035 i32 "$MODE_ID" >/dev/null 2>&1
	sleep "$WAIT2"
	post=$(cat /sys/class/display/mode)
	L=$(dumpsys SurfaceFlinger --list 2>/dev/null | grep 'SurfaceView\[' | grep -i blast | head -1)
	[ -n "$L" ] && dumpsys SurfaceFlinger --latency "$L" > "$LAT.after$i" 2>&1
	hwc=$(logcat -d -v time 2>/dev/null | grep 'adjustVsyncMode' | tail -1 | sed 's/.*adjustVsyncMode/adjustVsyncMode/')

	ab=$(avg_of "$LAT.before$i")
	aa=$(avg_of "$LAT.after$i")
	verdict=$(awk -v a="$ab" -v b="$aa" 'BEGIN {
		if (a ~ /^[0-9.]+$/ && b ~ /^[0-9.]+$/ && b > 1.5 * a) print "ПОЛОВИННАЯ-ПОДАЧА"; else print "ок" }')
	say "  #$i до=${ab}мс ($pre) после=${aa}мс ($post) app-setFrameRate=$app_rate HWC=$hwc => $verdict"
	i=$((i + 1))
done

am force-stop "$PKG" >/dev/null 2>&1
sleep 2
service call SurfaceFlinger 1035 i32 "$ORIG_ID" >/dev/null 2>&1
service call SurfaceFlinger 1035 i32 9999 >/dev/null 2>&1
say "=== ИТОГ: $(grep -c 'ПОЛОВИННАЯ-ПОДАЧА' "$OUT")/$RUNS половинной подачи $(date +%H:%M:%S) ==="
