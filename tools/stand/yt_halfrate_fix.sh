#!/system/bin/sh
# AFRd2: воспроизводим «половинную подачу» (после переключения режима при живом
# видео подача становится ~2 периода дисплея, видео ~13-14 fps) и проверяем
# варианты лечения на месте.
#
# Запуск (демон ОСТАНОВЛЕН, ТВ включён):
#   adb -s 192.168.88.50:5555 shell 'sh /data/local/tmp/yt_halfrate_fix.sh'
#
# Переменные: URL, MODE_ID (цель, 141), ORIG_ID (возврат, 135), TRIES.

URL=${URL:-https://www.youtube.com/watch?v=zhApeaHMvfs}
PKG=${PKG:-com.google.android.youtube.tv}
MODE_ID=${MODE_ID:-141}
ORIG_ID=${ORIG_ID:-135}
TRIES=${TRIES:-20}
OUT=/data/local/tmp/yt_halfrate_fix.txt
LAT=/data/local/tmp/ytf.lat

: > "$OUT"
say () { echo "$@" | tee -a "$OUT"; }

measure () { # <файл> -> средний интервал, мс (n/a если нет данных)
	L=$(dumpsys SurfaceFlinger --list 2>/dev/null | grep 'SurfaceView\[' | grep -i blast | head -1)
	[ -z "$L" ] && { echo "n/a"; return; }
	dumpsys SurfaceFlinger --latency "$L" > "$1" 2>&1
	awk 'NF == 3 && $2 + 0 > 0 && $2 + 0 < 1e17 { n++; t[n] = $2 + 0 }
	     END { if (n < 3) { print "n/a"; exit }
	           m = 0; sum = 0
	           for (i = 2; i <= n; i++) { d = (t[i] - t[i-1]) / 1e6; if (d <= 0) continue; m++; sum += d }
	           printf "%.1f", sum / m }' "$1"
}
is_bad () { awk -v v="$1" 'BEGIN { exit !(v ~ /^[0-9.]+$/ && v > 55) }'; }

say "воспроизведение «половинной подачи»: старт $(date +%H:%M:%S)"

i=1
bad=0
while [ "$i" -le "$TRIES" ]; do
	am force-stop "$PKG" >/dev/null 2>&1
	service call SurfaceFlinger 1035 i32 "$ORIG_ID" >/dev/null 2>&1
	service call SurfaceFlinger 1035 i32 9999 >/dev/null 2>&1
	sleep 3
	logcat -c 2>/dev/null
	am start -a android.intent.action.VIEW -d "$URL" >/dev/null 2>&1
	sleep 10
	service call SurfaceFlinger 1035 i32 "$MODE_ID" >/dev/null 2>&1
	sleep 8
	a=$(measure "$LAT.repro$i")
	say "  #$i режим=$(cat /sys/class/display/mode) подача=${a}мс"
	if is_bad "$a"; then
		bad=1
		say "  ВОСПРОИЗВЕДЕНО на #$i (${a}мс). Проверяю лечение:"

		# В1: переиздать тот же режим
		service call SurfaceFlinger 1035 i32 "$MODE_ID" >/dev/null 2>&1; sleep 6
		b=$(measure "$LAT.fix1_$i"); say "    В1 переиздать тот же режим: ${b}мс"

		# В2: «туда-обратно» 60 -> 24
		service call SurfaceFlinger 1035 i32 "$ORIG_ID" >/dev/null 2>&1; sleep 3
		service call SurfaceFlinger 1035 i32 "$MODE_ID" >/dev/null 2>&1; sleep 6
		c=$(measure "$LAT.fix2_$i"); say "    В2 туда-обратно (60 -> 24): ${c}мс"

		# В3: сброс флага backdoor + повторный режим
		service call SurfaceFlinger 1035 i32 9999 >/dev/null 2>&1; sleep 3
		service call SurfaceFlinger 1035 i32 "$MODE_ID" >/dev/null 2>&1; sleep 6
		d=$(measure "$LAT.fix3_$i"); say "    В3 сброс флага + режим: ${d}мс"

		break
	fi
	i=$((i + 1))
done

am force-stop "$PKG" >/dev/null 2>&1
sleep 2
service call SurfaceFlinger 1035 i32 "$ORIG_ID" >/dev/null 2>&1
service call SurfaceFlinger 1035 i32 9999 >/dev/null 2>&1
say "=== ИТОГ: воспроизведено=$bad попыток=$i режим=$(cat /sys/class/display/mode) $(date +%H:%M:%S) ==="
