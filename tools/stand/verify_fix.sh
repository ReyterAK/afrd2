#!/system/bin/sh
# AFRd2: живая проверка демона «одним вызовом adb» — все клипы в одном проходе.
#
# Зачем одним вызовом: при выключенном ТВ бокс спит и каждый «холодный» adb-обмен
# стоит десятки секунд-минуты (замерено: первый вызов после простоя 226.7 с).
# Скрипт делает всю последовательность на устройстве и печатает результат один раз.
#
# Запуск (ТВ должен быть включён):
#   adb -s 192.168.88.50:5555 shell 'sh /data/local/tmp/verify_fix.sh' | tee live.txt
#
# Переменные:
#   CLIPS  — список клипов без расширения (по умолчанию long_24000_1001 long_25 long_50)
#   CDIR   — каталог клипов (по умолчанию /sdcard/Download/afr)
#   DLOG   — файл лога демона (по умолчанию /data/local/afrd.log)
CLIPS=${CLIPS:-"long_24000_1001 long_25 long_50"}
CDIR=${CDIR:-/sdcard/Download/afr}
DLOG=${DLOG:-/data/local/afrd.log}
VLC=org.videolan.vlc/.StartActivity
PKG=org.videolan.vlc
OUT=/data/local/tmp/verify_fix.txt

: > "$OUT"

say () { echo "$@" | tee -a "$OUT"; }

mode () { cat /sys/class/display/mode; }
hz_now () { grep -m1 'sync_duration_num' /sys/class/display/vinfo | tr -d '\r'; }
backdoor () { dumpsys SurfaceFlinger 2>/dev/null | grep -m1 -o 'debugDisplayModeSetByBackdoor=[a-z]*'; }
sf_rate () { dumpsys SurfaceFlinger 2>/dev/null | grep -m1 -o 'activeMode=[0-9.]* *Hz\?'; }

say "== AFRd2 live check: $(date)"
say "== daemon: pid $(pidof afrd), mode=$(mode), $(backdoor)"

for c in $CLIPS; do
	f="$CDIR/$c.mp4"
	say ""
	say "=== $c ==="
	if [ ! -f "$f" ]; then
		say "SKIP: нет файла $f"
		continue
	fi

	am force-stop $PKG >/dev/null 2>&1
	sleep 2
	say "до:    mode=$(mode) $(hz_now) $(backdoor)"
	lines0=$(wc -l < "$DLOG" 2>/dev/null || echo 0)

	am start -n $VLC -a android.intent.action.VIEW -d "file://$f" -t video/mp4 >/dev/null 2>&1

	# ждём смены режима (до 12 с)
	i=0
	while [ "$i" -lt 24 ]; do
		sleep 0.5
		i=$((i + 1))
		m=$(mode)
		case "$m" in
			dummy*|1080p60hz) ;;
			*) break ;;
		esac
	done
	sleep 4
	say "после: mode=$(mode) $(hz_now) $(sf_rate) $(backdoor)"
	say "лог:"
	tail -n +$((lines0 + 1)) "$DLOG" 2>/dev/null | sed -n 's/^/  /p'

	am force-stop $PKG >/dev/null 2>&1
	sleep 5
	say "стоп:  mode=$(mode) $(hz_now) $(backdoor)"
done

say ""
say "== итог: $(date)"
say "прогон завершён; разбор — в /data/local/tmp/verify_fix.txt"
