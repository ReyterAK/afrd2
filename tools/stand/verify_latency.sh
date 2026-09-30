#!/system/bin/sh
# AFRd2: приёмка с опросом режима каждые 0.5 с — видно, когда режим реально
# появился в sysfs относительно строк лога демона (у демона свои метки времени).
CLIPS=${CLIPS:-"long_24000_1001 long_25 long_50"}
CDIR=${CDIR:-/sdcard/Download/afr}
DLOG=${DLOG:-/data/local/afrd.log}
PKG=org.videolan.vlc
VLC=org.videolan.vlc/.StartActivity

for c in $CLIPS; do
	f="$CDIR/$c.mp4"
	[ -f "$f" ] || { echo "SKIP $c: нет файла"; continue; }

	am force-stop $PKG >/dev/null 2>&1
	sleep 3
	L0=$(wc -l < "$DLOG")
	echo "=== $c: старт $(date +%H:%M:%S) mode=$(cat /sys/class/display/mode) ==="
	am start -n $VLC -a android.intent.action.VIEW -d "file://$f" -t video/mp4 >/dev/null 2>&1

	i=0
	while [ "$i" -lt 30 ]; do
		sd=$(sed -n 's/^[[:space:]]*sync_duration_num[[:space:]]*:[[:space:]]*//p' /sys/class/display/vinfo | head -1)
		echo "  $(date +%H:%M:%S) $(cat /sys/class/display/mode) sync=${sd:-?}"
		sleep 0.5
		i=$((i + 1))
	done

	echo "--- лог демона за прогон:"
	tail -n +$((L0 + 1)) "$DLOG"

	am force-stop $PKG >/dev/null 2>&1
	sleep 4
	echo "--- после стопа: mode=$(cat /sys/class/display/mode) $(date +%H:%M:%S)"
done
