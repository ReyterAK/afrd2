#!/system/bin/sh
# AFRd2: живая проверка сторожа приложения (R57).
# Демон держит режим флагом backdoor SurfaceFlinger. Если его убить (kill -9),
# флаг остаётся поднятым, и приложения со своим AFR перестают менять режим.
# Ожидание: AFRService (вариант B) замечает смерть процесса и перезапускает
# демон (≤ ~10 с), а демон на старте снимает залипший флаг.
#
# Запуск (ТВ включён, APK с правкой установлен):
#   adb -s 192.168.88.50:5555 shell 'sh /data/local/tmp/watchdog_test.sh'
#
# Переменные: CLIP (файл для удержания режима).

PKG=ru.cobra.zap.afrd
ACT=ru.cobra.zap.afrd.gui.MainActivity
CLIP=${CLIP:-/sdcard/Download/afr/long_24000_1001.mp4}
PKG_PLAYER=org.videolan.vlc
DL=${DL:-/data/local/afrd.log}
OUT=/data/local/tmp/watchdog_test.txt

: > "$OUT"
say () { echo "$@" | tee -a "$OUT"; }
pd () { pidof afrd; }
fl () { dumpsys SurfaceFlinger 2>/dev/null | grep -m1 -o 'debugDisplayModeSetByBackdoor=[a-z]*'; }
md () { cat /sys/class/display/mode; }

say "== R57: проверка сторожа приложения == $(date +%H:%M:%S)"
say "1) запуск приложения (MainActivity -> AFRService -> демон)"
am start -n "$PKG/$ACT" >/dev/null 2>&1
sleep 15
p=$(pd)
say "   демон=${p:-нет}$([ -n "$p" ] && echo " cmdline=$(cat /proc/$p/cmdline 2>/dev/null | tr '\0' ' ')")"
if [ -z "$p" ]; then say "FAIL: демон не поднялся из приложения"; exit 1; fi

say "2) видео (режим должен удержаться флагом)"
am force-stop "$PKG_PLAYER" >/dev/null 2>&1
sleep 2
am start -n "$PKG_PLAYER/.StartActivity" -a android.intent.action.VIEW \
	-d "file://$CLIP" -t video/mp4 >/dev/null 2>&1
sleep 28
say "   mode=$(md) $(fl) daemon=$(pd)"
p=$(pd)
if [ -z "$p" ]; then say "FAIL: демон исчез до убийства"; exit 1; fi

say "3) kill -9 демона (pid=$p)"
kill -9 "$p" 2>/dev/null
say "   сразу: daemon=$(pd) $(fl) mode=$(md)"

i=0
ok=0
while [ "$i" -lt 20 ]; do
	sleep 2
	i=$((i + 1))
	np=$(pd)
	say "   t=+$((i * 2))s daemon=${np:-нет} $(fl) mode=$(md)"
	if [ -n "$np" ] && [ "$np" != "$p" ]; then ok=1; break; fi
done

am force-stop "$PKG_PLAYER" >/dev/null 2>&1
sleep 5
if [ "$ok" = 1 ]; then
	say "РЕЗУЛЬТАТ: OK — сторож поднял демон заново за ≤$((i * 2)) с; после остановки $(fl) mode=$(md)"
else
	say "РЕЗУЛЬТАТ: FAIL — демон не поднялся за 40 с; $(fl) mode=$(md)"
fi
say "DONE"
