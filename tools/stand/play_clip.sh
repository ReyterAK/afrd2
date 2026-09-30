#!/bin/sh
# Стенд ATV14: запуск/остановка тестового клипа в плеере без собственного AFR
# (по умолчанию VLC — он режим не трогает, поэтому видно работу демона).
#
# Запуск:
#   sh tools/stand/play_clip.sh start long_24000_1001
#   sh tools/stand/play_clip.sh stop
#
# Переменные:
#   ADB_DEV (или D) — адрес adb-устройства (192.168.88.50:5555);
#   CLIP_DIR        — каталог с клипами на устройстве (/sdcard/Download/afr);
#   PLAYER          — component активности плеера (org.videolan.vlc/.StartActivity);
#   MIME            — MIME-тип для am start (video/mp4).
#
# Код возврата: 0 — действие выполнено, 1 — клип не найден на устройстве,
#               2 — стенд недоступен (действие не выполнено).

D=${ADB_DEV:-${D:-192.168.88.50:5555}}
CLIP_DIR=${CLIP_DIR:-/sdcard/Download/afr}
PLAYER=${PLAYER:-org.videolan.vlc/.StartActivity}
MIME=${MIME:-video/mp4}

usage ()
{
	echo "usage: $0 start <clip> | stop" >&2
	exit 1
}

if ! adb -s "$D" shell true >/dev/null 2>&1; then
	echo "стенд $D недоступен — действие не выполнено" >&2
	exit 2
fi

stop_player ()
{
	adb -s "$D" shell "am force-stop ${PLAYER%%/*}" >/dev/null
}

case "$1" in
	start)
		[ $# -ge 2 ] || usage
		clip=$2
		# Расширение можно не указывать. Существование клипа проверяем на
		# устройстве: иначе плеер молча откроет пустоту, и прогон будет ложным.
		file="$CLIP_DIR/$clip"
		case "$file" in
			*.mp4|*.mkv|*.ts|*.avi) ;;
			*) file="$file.mp4" ;;
		esac
		if ! adb -s "$D" shell "test -f '$file'" >/dev/null 2>&1; then
			echo "клип $file не найден на устройстве ($D)" >&2
			exit 1
		fi
		# Сначала остановить плеер: свежий старт декодера — то, что видит демон.
		stop_player
		sleep 2
		adb -s "$D" shell "am start -n $PLAYER -a android.intent.action.VIEW -d file://$file -t $MIME" >/dev/null
		;;
	stop)
		stop_player
		;;
	*)
		usage
		;;
esac
