#!/bin/sh
# Стенд ATV14: завершение демона по SIGTERM не оставляет залипшего режима
# (Task 7, Step 3 — приёмка спеки §3.1: guard снимает флаг при SIGTERM/SIGINT).
#
# Требуется включённый бокс с доступным adb (см. Task 8 — сборка и заливка бинарника).
# Запуск:
#   sh tools/stand/guard_test.sh
# Переменные: ADB_DEV (192.168.88.50:5555), AFRD_BIN (/data/local/tmp/afrd),
#             AFRD_INI (/data/local/tmp/afrd.ini), CLIP, START_ACT, ORIG_MODE,
#             HOLD_WAIT (секунды ожидания удержания, по умолчанию 26).
#
# HOLD_WAIT должен быть больше fps.measure.envelope.skip.ms + fps.measure.envelope.ms
# (в профиле 2000 + 20000 = 22 с): решение по огибающей принимается не раньше
# этого срока, и до него режим не переключён. Прежние 3 с годились, пока частота
# решалась по одному окну — с наблюдением (20 с) скрипт всегда давал «удержание не
# поднято» (живая находка 2026-10-08).
#
# Код возврата: 0 — проверка пройдена, 1 — провал, 2 — стенд недоступен (пропуск).

D=${ADB_DEV:-192.168.88.50:5555}
AFRD=${AFRD_BIN:-/data/local/tmp/afrd}
INI=${AFRD_INI:-/data/local/tmp/afrd.ini}
CLIP=${CLIP:-/sdcard/Download/afr/long_24000_1001.mp4}
START_ACT=${START_ACT:-org.videolan.vlc/.StartActivity}
ORIG_MODE=${ORIG_MODE:-1080p60hz}
HOLD_WAIT=${HOLD_WAIT:-26}

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
echo "жду удержания ${HOLD_WAIT} с (окно наблюдения огибающей)"
sleep "$HOLD_WAIT"

mode_held=$(adb -s "$D" shell 'cat /sys/class/display/mode' | tr -d '\r')
bd_held=$(adb -s "$D" shell 'dumpsys SurfaceFlinger | grep debugDisplayModeSetByBackdoor' | tr -d '\r')
daemon_pid=$(adb -s "$D" shell 'pidof afrd' | tr -d '\r')
echo "режим в удержании: $mode_held"
echo "флаг backdoor в удержании: $bd_held"
echo "pid демона: $daemon_pid"

# Предусловие: проверять путь SIGTERM бессмысленно, если демон не поднялся или
# удержание не состоялось — иначе провал демона выглядел бы как «OK».
if [ -z "$daemon_pid" ]; then
	echo "FAIL: демон не запустился (pidof afrd пуст) — путь SIGTERM не проверен" >&2
	exit 1
fi
case "$bd_held" in
	*true*) ;;
	*)
		if [ "$mode_held" = "$mode_before" ]; then
			echo "FAIL: удержание не поднято (режим $mode_held = исходный, флаг backdoor: $bd_held) — путь SIGTERM не проверен" >&2
			exit 1
		fi
		;;
esac

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
