#!/system/bin/sh
# AFRd2: «охота» за аномалией «FPS переключился, а система не в соответствие» —
# HWC не вошёл в видео-режим vsync (MixMode) при живом видео.
#
# Циклами гоняет ролики с разными FPS и с разными условиями старта, снимая по
# каждому прогону полный набор (см. stutter_probe.sh) и останавливаясь на первой
# аномалии. Вариации: обычный старт (VLC), старт «встык» (без паузы после стопа),
# kill -9 плеера, рестарт codec2 HAL перед стартом, штатный YouTube, AV1.
#
# Запуск (демон поднят, ТВ включён):
#   adb -s 192.168.88.50:5555 shell 'RUNS=30 SECS=45 sh /data/local/tmp/stutter_hunt.sh'
#
# Переменные: RUNS (максимум прогонов), SECS (секунд наблюдения в прогоне).

RUNS=${RUNS:-30}
SECS=${SECS:-60}
PROBE=${PROBE:-/data/local/tmp/stutter_probe.sh}
CDIR=${CDIR:-/sdcard/Download/afr}
OUT=/data/local/tmp/stutter_hunt.txt

# «клип:плеер:условие» — клип, плеер (vlc|just|yt), условие старта (plain|adjacent|kill9|c2)
V1="long_24000_1001:vlc:plain"
V2="long_25:vlc:adjacent"
V3="long_50:vlc:plain"
V4="sintel24:vlc:kill9"
V5="clip1_av1_4k_60s:just:c2"
V6="long_24000_1001:yt:plain"
V7="clip1_av1_4k_60s:just:adjacent"
V8="long_25:vlc:kill9"

: > "$OUT"
say () { echo "$@" | tee -a "$OUT"; }

say "охота: старт $(date +%H:%M:%S) режим=$(cat /sys/class/display/mode) демон=$(pidof afrd)"

r=1
anomaly=0
done_runs=0
while [ "$r" -le "$RUNS" ]; do
	idx=$(( (r - 1) % 8 + 1 ))
	eval "item=\$V$idx"
	c=${item%%:*}
	rest=${item#*:}; p=${rest%%:*}; cond=${rest##*:}
	f="$CDIR/$c.mp4"
	[ -f "$f" ] || { say "SKIP $c: нет файла"; r=$((r + 1)); continue; }

	case "$p" in
		vlc)  PKG=org.videolan.vlc;   START=org.videolan.vlc/.StartActivity ;;
		just) PKG=com.brouken.player; START=com.brouken.player/.PlayerActivity ;;
		yt)   PKG=com.google.android.youtube.tv; START="" ;;
		*)    PKG=""; START="" ;;
	esac

	if [ "$p" = yt ]; then
		URL="https://www.youtube.com/watch?v=zhApeaHMvfs"
	else
		URL="file://$f"
		# «встык»: не гасим плеер заранее — старт сразу, как стоп отработает
		[ "$cond" = adjacent ] || am force-stop "$PKG" >/dev/null 2>&1
	fi
	# «kill9»: жёстко убить плеер (pkill -f опасен — шаблон совпадает с
	# командной строкой самой оболочки, §14.4 журнала)
	if [ "$cond" = kill9 ]; then
		pp=$(pidof "$PKG")
		[ -n "$pp" ] && kill -9 $pp
	fi

	if [ "$cond" = c2 ]; then
		setprop ctl.restart android-hardware-media-c2-hal-1-2 2>/dev/null
		sleep 3
	fi

	L="h${r}_${c}_${p}_${cond}"
	say "### $L"
	PKG="$PKG" START="$START" URL="$URL" SECS="$SECS" sh "$PROBE" "$L" \
		> "/data/local/tmp/$L.out" 2>&1

	hwc=$(grep 'HWC на устойчивом замере' "/data/local/tmp/$L.out" | tail -1 | sed 's/.*: //')
	verdict=$(grep -E 'удержаний=' "/data/local/tmp/$L.out" | tail -1)
	modes=$(grep -E '^  t=' "/data/local/tmp/$L.out" | tr '\n' ' ')
	say "    HWC=$hwc"
	say "    режим: $modes"
	say "    $verdict"

	case "$hwc" in
		*HwMode*) anomaly=1 ;;
	esac
	if grep -q '!!ДЕРГАНИЕ-МАРКЕР' "/data/local/tmp/$L.out"; then anomaly=1; fi
	if grep -q '!!КАТАСТРОФА' "/data/local/tmp/$L.out"; then anomaly=1; fi
	if grep -q 'ЗАМЕР-ПУСТОЙ' "/data/local/tmp/$L.out"; then
		say "    (пустой прогон — видео не стартовало; не считаю аномалией)"
		r=$((r + 1)); sleep 3; continue
	fi

	if [ "$anomaly" = 1 ]; then
		say "!!! АНОМАЛИЯ на прогоне $L — останавливаюсь (данные: /data/local/tmp/$L.*)"
		break
	fi
	done_runs=$((done_runs + 1))
	sleep 2
	r=$((r + 1))
done

say "=== ИТОГ: зачтено прогонов=$done_runs аномалия=$anomaly $(date +%H:%M:%S) ==="
