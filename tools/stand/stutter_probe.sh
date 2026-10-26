#!/system/bin/sh
# AFRd2: прогон ролика с полной инструментацией «гладко/дёргано» (журнал, §19).
#
# Пишет:
#   /data/local/tmp/<label>.csv — раз в секунду: uptime, счётчики каналов, mode, sync
#   /data/local/tmp/<label>.log — метки (старт/стоп, каналы, вендорские vsync-события, лог демона)
#   /data/local/tmp/<label>.lat.<i> — дампы `dumpsys SurfaceFlinger --latency` видео-слоя
#     (шаг i=12 — до переключения, i=SECS-8 — после; вердикт считается по каждому дампу)
# и печатает вердикт по ровности фактической подачи кадров слоя (интервалы actualPresentTime).
#
# Запуск (ТВ включён; демон уже поднят на стенде):
#   adb -s 192.168.88.50:5555 shell 'URL="https://www.youtube.com/watch?v=zhApeaHMvfs" \
#       sh /data/local/tmp/stutter_probe.sh run1'
#
# Переменные: URL (ролик), SECS (секунд наблюдения, по умолчанию 45),
#             PKG/START (плеер; по умолчанию штатный YouTube TV).
#             PIN — id режима SurfaceFlinger: закрепить его на прогон (флаг
#             backdoor поднят, приложения со своим AFR не могут перебить).
#             Нужен для сравнения дёрганья по режимам при остановленном демоне;
#             ORIG_ID — mode id для восстановления (по умолчанию 135 = 59.94).

L=${1:-run}
URL=${URL:-https://www.youtube.com/watch?v=zhApeaHMvfs}
SECS=${SECS:-45}
PKG=${PKG:-com.google.android.youtube.tv}
START=${START:-}
DIR=/data/local/tmp
CSV=$DIR/$L.csv
LOG=$DIR/$L.log
LATP=$DIR/$L.lat

: > "$CSV"; : > "$LOG"
rm -f "$LATP".* 2>/dev/null

chans () { grep -c 'device name' /sys/class/vdec/vdec_status 2>/dev/null; }
fcount () { sed -n 's/.*frame count : //p' /sys/class/vdec/vdec_status 2>/dev/null; }
mode () { cat /sys/class/display/mode 2>/dev/null; }
syncnum () { grep -m1 sync_duration_num /sys/class/display/vinfo 2>/dev/null | tr -dc 0-9; }
backdoor () { dumpsys SurfaceFlinger 2>/dev/null | grep -m1 -o 'debugDisplayModeSetByBackdoor=[a-z]*'; }

echo "start label=$L url=$URL mode=$(mode) uptime=$(cut -d' ' -f1 /proc/uptime)" >> "$LOG"
logcat -c 2>/dev/null
am force-stop "$PKG" >/dev/null 2>&1
sleep 3

# Закрепить режим (для сравнения по режимам; демон должен быть остановлен)
if [ -n "$PIN" ]; then
	service call SurfaceFlinger 1035 i32 "$PIN" >> "$LOG" 2>&1
	sleep 1
	echo "pin: id=$PIN -> mode=$(mode) $(syncnum)" >> "$LOG"
fi

if [ -n "$START" ]; then
	am start -n "$START" -a android.intent.action.VIEW -d "$URL" >/dev/null 2>&1
else
	am start -a android.intent.action.VIEW -d "$URL" >/dev/null 2>&1
fi

i=0
while [ "$i" -lt 40 ]; do
	[ -n "$(fcount | head -1)" ] && break
	sleep 1; i=$((i + 1))
done
echo "decoder_up_after=${i}s channels=$(chans)" >> "$LOG"

i=0
# Отсчёт по настенным часам: одна итерация длится больше секунды (лог/дампы),
# поэтому «i секунд» не равно времени — устойчивый замер легко попадал уже
# ПОСЛЕ остановки плеера. Держим запас STEADY_GAP от конца прогона.
STEADY_GAP=${STEADY_GAP:-12}
T0=$(cut -d' ' -f1 /proc/uptime)
lat_pre=0; lat_steady=0
while :; do
	el=$(( ${T0%.*} ))
	now=$(( $(cut -d' ' -f1 /proc/uptime | cut -d. -f1) - el ))
	[ "$now" -ge "$SECS" ] && break

	t=$(cut -d' ' -f1 /proc/uptime)
	cf=$(fcount | head -1)
	cl=$(fcount | tail -1)
	ch=$(chans)
	echo "$t,$ch,${cf:-0},${cl:-0},$(mode),$(syncnum)" >> "$CSV"

	# латентность видео-слоя: после переключения (12 с) и в устойчивом режиме
	# (за STEADY_GAP секунд до стопа — гарантированно при живом видео)
	snap=""
	[ "$lat_pre" = 0 ] && [ "$now" -ge 12 ] && snap=pre
	[ "$lat_steady" = 0 ] && [ "$now" -ge $((SECS - STEADY_GAP)) ] && snap=steady
	if [ -n "$snap" ]; then
		[ "$snap" = pre ] && lat_pre=1 || lat_steady=1
		lay=$(dumpsys SurfaceFlinger --list 2>/dev/null | grep 'SurfaceView\[' | grep -i blast | head -1)
		echo "LAT $snap t=$now layer=$lay" >> "$LOG"
		[ -n "$lay" ] && dumpsys SurfaceFlinger --latency "$lay" > "$LATP.$snap" 2>&1
		# состояние vsync-режима HWC на момент замера: последнее событие FRA/HWC
		# (переключение режима 1035 его НЕ пересчитывает — проверено)
		echo "HWC@$snap t=$now: $(logcat -d -v time 2>/dev/null | grep 'adjustVsyncMode' | tail -1)" >> "$LOG"
		# снимок состояния SF для разбора: тип композиции видео-слоя, override частоты
		dumpsys SurfaceFlinger > "$LATP.$snap.sf" 2>/dev/null
	fi
	sleep 1; i=$((i + 1))
done

am force-stop "$PKG" >/dev/null 2>&1
sleep 4
echo "meta mode=$(mode) backdoor=$(backdoor) uptime=$(cut -d' ' -f1 /proc/uptime)" >> "$LOG"

# Снять закрепление: вернуть исходный режим и сбросить флаг backdoor
if [ -n "$PIN" ]; then
	service call SurfaceFlinger 1035 i32 "${ORIG_ID:-135}" >> "$LOG" 2>&1
	service call SurfaceFlinger 1035 i32 9999 >> "$LOG" 2>&1
	echo "unpin: mode=$(mode) $(syncnum) $(backdoor)" >> "$LOG"
fi

# Гигиена стенда (§58): если запускали по deep-link, задача плеера с этим
# намерением остаётся в recents и после force-stop, и после перезагрузки —
# тогда запуск с иконки лаунчера сразу играет тот же ролик. Снимаем её.
if [ -f "${0%/*}/yt_task_clean.sh" ] && [ -n "$PKG" ]; then
	echo "clean: $(sh "${0%/*}/yt_task_clean.sh" "$PKG" 2>&1 | tr '\n' ' ')" >> "$LOG"
fi

# вендорский путь: события FRA/HWC (журнал, §19.4)
logcat -d -v time 2>/dev/null | grep -E \
	"frameRateDisplay|setVideoLayerOn|adjustVsyncMode|configure width|Created component|waitMixVsync|wait for hw vsync|ModePolicy" \
	>> "$LOG"
echo "--- daemon ---" >> "$LOG"
tail -n 40 /data/local/afrd.log >> "$LOG"

echo "=== $L: режим по секундам (mode sync) ==="
awk -F, 'NR==1{p="";s=""} {k=$5" "$6; if(k!=p){printf "  t=%s %s\n",$1,k; p=k}}' "$CSV"

echo "=== $L: маркер ошибки HWC (video-on / MixMode) ==="
fra_on=$(grep -c 'frameRateDisplay 1,' "$LOG")
mix=$(grep -c 'adjustVsyncMode to MixMode' "$LOG")
hwm=$(grep -c 'adjustVsyncMode to HwMode' "$LOG")
hwc_steady=$(grep 'HWC@steady' "$LOG" | tail -1 | sed 's/.*adjustVsyncMode/adjustVsyncMode/')
echo "  HWC на устойчивом замере: ${hwc_steady:-нет строки}"
if [ -n "$PIN" ]; then
	echo "  PIN-прогон (id=$PIN): MixMode-маркер не показателен; смотрим удержания подачи"
elif ! printf '%s' "$hwc_steady" | grep -q 'adjustVsyncMode'; then
	echo "  ЗАМЕР-ПУСТОЙ: видео не стартовало или леер не найден (FRA video-on=$fra_on)"
elif [ "$hwc_steady" = "adjustVsyncMode to MixMode" ]; then
	echo "  ОК: при живом видео HWC в MixMode (FRA video-on=$fra_on, всего MixMode=$mix HwMode=$hwm)"
else
	echo "  !!ДЕРГАНИЕ-МАРКЕР: при живом видео HWC не в видео-режиме ($hwc_steady)"
fi

echo "=== $L: частоты (декодер vs подача слоя) ==="
dfps=$(awk -F, 'NR==1 { t0 = $1 + 0; f0 = $4 + 0 }
                END { if (($1 + 0) > t0) printf "%.3f", ($4 - f0) / ($1 - t0); else print "0" }' "$CSV")
awk -F, 'NR==1 { t0 = $1 + 0; f0 = $4 + 0 }
         END { if (($1 + 0) > t0) printf "  декодер: %.2f fps (ΔF=%d за %.1f с)\n",
                        ($4 - f0) / ($1 - t0), $4 - f0, $1 - t0 }' "$CSV"

echo "=== $L: вердикт ровности подачи слоя видео ==="
echo "  норма: интервалы чередуются 2/3 периода 60-Гц сетки (≈33/50 мс при 24 Гц),"
echo "  дёрганость = интервал > 1.35×среднего (удержание кадра сверх обычного);"
echo "  !!КАТАСТРОФА-ПО-ПОДАЧЕ = средний интервал > 1.8 ожидаемого (1000/частота декодера)"
for f in "$LATP".*; do
	[ -f "$f" ] || continue
	case "$f" in *.sf) continue;; esac
	awk -v dfps="$dfps" '
	NR == 1 { period = $1 + 0; next }
	NF == 3 && $2 + 0 > 0 && $2 + 0 < 1e17 { n++; t[n] = $2 + 0 }
	END {
		if (n < 3) { printf "    no-data n=%d\n", n; exit }
		m = 0; sum = 0; max = 0; min = 0
		for (i = 2; i <= n; i++) {
			d = (t[i] - t[i-1]) / 1e6; if (d <= 0) continue
			m++; sum += d
			if (d > max) max = d
			if (min == 0 || d < min) min = d
		}
		if (m < 2) { printf "    %s no-data n=%d\n", FILENAME, n; exit }
		avg = sum / m
		hang = 0
		for (i = 2; i <= n; i++) { d = (t[i] - t[i-1]) / 1e6; if (d > 1.35 * avg) hang++ }
		expms = (dfps > 0) ? 1000.0 / dfps : 0
		cat = (expms > 0 && avg > 1.8 * expms) ? " !!КАТАСТРОФА-ПО-ПОДАЧЕ" : ""
		printf "    %s период=%.2fms n=%d avg=%.2fms min=%.2fms max=%.2fms удержаний=%d %s%s\n",
		       FILENAME, period / 1e6, n, avg, min, max, hang, (hang == 0 ? "SMOOTH" : "JANKY"), cat
	}' "$f"
done

echo "DONE $L"
