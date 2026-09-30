#!/system/bin/sh
# AFRd2: проверка R52/P2 — пауза ДО первого успешного замера не должна выключать
# AFR до конца файла. Старый код: окно на стоящем счётчике тратило попытку замера,
# три подряд (≈5 с паузы) выставляли measure_failed -> режим не переключался.
PKG=org.videolan.vlc
VLC=org.videolan.vlc/.StartActivity
DLOG=/data/local/afrd.log
F=${1:-/sdcard/Download/afr/long_24000_1001.mp4}

am force-stop $PKG >/dev/null 2>&1
sleep 3
L0=$(wc -l < "$DLOG")
echo "# до: mode=$(cat /sys/class/display/mode) $(date +%H:%M:%S)"

am start -n $VLC -a android.intent.action.VIEW -d "file://$F" -t video/mp4 >/dev/null 2>&1
sleep 1
input keyevent 85            # MEDIA_PAUSE — внутри первого окна замера
echo "# пауза $(date +%H:%M:%S) mode=$(cat /sys/class/display/mode)"
sleep 6                      # по старому коду этого хватало на 3 попытки и measure_failed
input keyevent 85            # продолжить
echo "# продолжение $(date +%H:%M:%S) mode=$(cat /sys/class/display/mode)"
sleep 8
echo "# после: mode=$(cat /sys/class/display/mode) sync=$(sed -n 's/^[[:space:]]*sync_duration_num[[:space:]]*:[[:space:]]*//p' /sys/class/display/vinfo | head -1) $(date +%H:%M:%S)"
echo "--- лог демона:"
tail -n +$((L0 + 1)) "$DLOG"
am force-stop $PKG >/dev/null 2>&1
