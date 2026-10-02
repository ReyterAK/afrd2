#!/bin/sh
# AFRd2: быстрое A/B режима для визуального сравнения (хост-скрипт).
#
# Для оценки «24->25 или 25->24 менее неприятно»: пока ролик играет, одной
# командой переключаем режим и смотрим. Демон должен быть остановлен, иначе он
# вернёт режим своим.
#
#   sh tools/stand/ab_rate.sh 23.98   # дробный представитель класса 24
#   sh tools/stand/ab_rate.sh 50      # класс 25 (кратный x2)
#   sh tools/stand/ab_rate.sh 25      # ровно 25 Гц
#   sh tools/stand/ab_rate.sh 59.94   # как было
#   sh tools/stand/ab_rate.sh off     # вернуть 59.94 и снять флаг backdoor
#
# id режима разрешается по частоте каждый раз: после рестарта SurfaceFlinger
# нумерация меняется (в одной сессии 23.98 был 141, в другой 156).

D=${D:-192.168.88.50:5555}

id_for () { # <частота> -> id режима 1920x1080 с этой частотой
	adb -s "$D" shell 'dumpsys SurfaceFlinger 2>/dev/null | grep -E "^ *\{id="' | tr -d '\r' | \
	awk -v r="$1" '
		{
			if (match ($0, /id=[0-9]+/)) id = substr ($0, RSTART + 3, RLENGTH - 3)
			if (match ($0, /resolution=[0-9]+x[0-9]+/)) res = substr ($0, RSTART + 11, RLENGTH - 11)
			if (match ($0, /refreshRate=[0-9.]+/)) rr = substr ($0, RSTART + 12, RLENGTH - 12)
			if ((id != "") && (res == "1920x1080") && (rr + 0 == r + 0)) { print id; exit }
		}'
}

set_mode () { # <частота>
	id=$(id_for "$1")
	if [ -z "$id" ]; then echo "режим $1 Гц не найден в таблице SurfaceFlinger"; return 1; fi
	adb -s "$D" shell "service call SurfaceFlinger 1035 i32 $id >/dev/null"
	echo "  $1 Гц -> id $id"
}

restore () {
	set_mode 59.94 >/dev/null
	adb -s "$D" shell 'service call SurfaceFlinger 1035 i32 9999 >/dev/null'
}

case "$1" in
	"")      echo "usage: sh tools/stand/ab_rate.sh <23.98|24|25|29.97|50|59.94|60|off>"; exit 2 ;;
	off)     restore ;;
	24)      set_mode 23.98 ;;
	60)      set_mode 59.94 ;;
	*)       set_mode "$1" ;;
esac

sleep 1
echo "mode: $(adb -s "$D" shell 'cat /sys/class/display/mode') | $(adb -s "$D" shell 'dumpsys SurfaceFlinger 2>/dev/null | grep -m1 -o "debugDisplayModeSetByBackdoor=[a-z]*"')"
