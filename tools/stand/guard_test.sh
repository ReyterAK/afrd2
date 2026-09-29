#!/bin/sh
# Стенд ATV14: завершение демона по SIGTERM не оставляет залипшего режима
# (Task 7, Step 3 — приёмка спеки §3.1: guard снимает флаг при SIGTERM/SIGINT).
#
# Требуется включённый бокс с доступным adb (см. Task 8 — сборка и заливка бинарника).
# Запуск:
#   sh tools/stand/guard_test.sh
# Переменные: ADB_DEV (192.168.88.50:5555), AFRD_BIN (/data/local/tmp/afrd),
#             AFRD_INI (/data/local/tmp/afrd.ini), CLIP, START_ACT, ORIG_MODE.
#
# Код возврата: 0 — проверка пройдена, 1 — провал, 2 — стенд недоступен (пропуск).

D=${ADB_DEV:-192.168.88.50:5555}
AFRD=${AFRD_BIN:-/data/local/tmp/afrd}
INI=${AFRD_INI:-/data/local/tmp/afrd.ini}
CLIP=${CLIP:-/sdcard/Download/afr/long_24000_1001.mp4}
START_ACT=${START_ACT:-org.videolan.vlc/.StartActivity}
ORIG_MODE=${ORIG_MODE:-1080p60hz}

adb connect "$D" >/dev/null 2>&1 || true
if ! adb -s "$D" shell true >/dev/null 2>&1; then
	echo "стенд $D недоступен — проверка пропущена (живая приёмка: Task 9)" >&2
	exit 2
fi

echo "== запуск видео и демона =="
adb -s "$D" shell "am start -n $START_ACT -a android.intent.action.VIEW -d file://$CLIP -t video/mp4" >/dev/null
sleep 8

mode_before=$(adb -s "$D" shell 'cat /sys/class/display/mode' | tr -d '\r')
echo "режим до запуска демона: $mode_before"

adb -s "$D" shell "$AFRD -k -D $INI &" >/dev/null 2>&1
sleep 3

mode_held=$(adb -s "$D" shell 'cat /sys/class/display/mode' | tr -d '\r')
bd_held=$(adb -s "$D" shell 'dumpsys SurfaceFlinger | grep debugDisplayModeSetByBackdoor' | tr -d '\r')
echo "режим в удержании: $mode_held"
echo "флаг backdoor в удержании: $bd_held"

echo "== SIGTERM: демон обязан вернуть режим и снять флаг =="
adb -s "$D" shell 'kill -TERM $(pidof afrd)'
sleep 2

mode_after=$(adb -s "$D" shell 'cat /sys/class/display/mode' | tr -d '\r')
bd_after=$(adb -s "$D" shell 'dumpsys SurfaceFlinger | grep debugDisplayModeSetByBackdoor' | tr -d '\r')
echo "режим после SIGTERM: $mode_after"
echo "флаг backdoor после SIGTERM: $bd_after"

status=0
[ "$mode_after" = "$ORIG_MODE" ] || { echo "FAIL: режим не вернулся к $ORIG_MODE"; status=1; }
echo "$bd_after" | grep -q 'false' || { echo "FAIL: debugDisplayModeSetByBackdoor не снят"; status=1; }

if [ "$status" = 0 ]; then
	echo "OK: режим возвращён, флаг backdoor снят"
else
	exit 1
fi
