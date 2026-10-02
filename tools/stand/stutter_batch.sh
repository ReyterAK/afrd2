#!/system/bin/sh
# AFRd2: батч-прогон роликов разного fps через stutter_probe.sh.
# Ищем «маркер дёрганья» — HWC не вошёл в видео-режим vsync (нет MixMode) — и
# собираем по каждому прогону режим выхода, решение демона и вердикт подачи слоя.
#
# Запуск (демон должен быть уже поднят; ТВ включён):
#   adb -s 192.168.88.50:5555 shell 'RUNS=2 SECS=30 sh /data/local/tmp/stutter_batch.sh'
#
# Переменные:
#   RUNS  — прогонов на клип (по умолчанию 2)
#   SECS  — секунд наблюдения в прогоне (по умолчанию 30)
#   LIST  — список «клип:плеер», плеер: vlc | just (Just Player, AV1)

RUNS=${RUNS:-2}
SECS=${SECS:-30}
PROBE=${PROBE:-/data/local/tmp/stutter_probe.sh}
LIST=${LIST:-"long_24000_1001:vlc sintel24:vlc long_25:vlc long_50:vlc t60:vlc clip1_av1_4k_60s:just"}
CDIR=${CDIR:-/sdcard/Download/afr}
OUT=/data/local/tmp/stutter_batch.txt
DLOG=${DLOG:-/data/local/afrd.log}

: > "$OUT"
say () { echo "$@" | tee -a "$OUT"; }

say "батч: $(date +%H:%M:%S) mode=$(cat /sys/class/display/mode) демон=$(pidof afrd)"
for item in $LIST; do
	c=${item%%:*}
	p=${item##*:}
	f="$CDIR/$c.mp4"
	[ -f "$f" ] || { say "SKIP $c: нет файла"; continue; }

	case "$p" in
		vlc)  PKG=org.videolan.vlc;   START=org.videolan.vlc/.StartActivity ;;
		just) PKG=com.brouken.player; START=com.brouken.player/.PlayerActivity ;;
		*)    PKG=""; START="" ;;
	esac

	r=1
	while [ "$r" -le "$RUNS" ]; do
		L="b_${c}_r${r}"
		say "### $L ($p)"
		L0=$(wc -l < "$DLOG" 2>/dev/null)
		PKG="$PKG" START="$START" URL="file://$f" SECS="$SECS" sh "$PROBE" "$L" \
			> "/data/local/tmp/$L.out" 2>&1
		grep -E "ДЕРГАНИЕ-МАРКЕР|ОК \(HWC|^  t=|удержаний|no-data" "/data/local/tmp/$L.out" | sed 's/^/  /' | tee -a "$OUT"
		tail -n +$((L0 + 1)) "$DLOG" | grep -E "видео:|Переключаю|удержан|снял удержание|backdoor" | tail -4 | sed 's/^/  демон: /' | tee -a "$OUT"
		sleep 3
		r=$((r + 1))
	done
done

say "=== ИТОГ ==="
say "прогонов с маркером дёрганья: $(grep -c 'ДЕРГАНИЕ-МАРКЕР' "$OUT")"
say "прогонов ОК (MixMode): $(grep -c 'ОК (HWC' "$OUT")"
say "DONE $(date +%H:%M:%S)"
