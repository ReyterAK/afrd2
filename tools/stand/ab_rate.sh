#!/bin/sh
# AFRd2: быстрое A/B режима для визуального сравнения (хост-скрипт).
#
# Для оценки «24->25 или 25->24 менее неприятно»: пока ролик играет, одной
# командой переключаем режим и смотрим. Демон при этом должен быть остановлен,
# иначе он перебьёт режим своим.
#
#   sh tools/stand/ab_rate.sh 141     # 23.98 Гц (младший класс)
#   sh tools/stand/ab_rate.sh 137     # 50 Гц (класс 25)
#   sh tools/stand/ab_rate.sh 140     # 25 Гц ровно
#   sh tools/stand/ab_rate.sh 135     # 59.94 Гц (как было)
#   sh tools/stand/ab_rate.sh off     # вернуть 59.94 и снять флаг backdoor
#
# id режимов берутся из таблицы SurfaceFlinger (см. dumpsys ниже); после
# рестарта SurfaceFlinger нумерация меняется.

D=${D:-192.168.88.50:5555}

mode () { adb -s "$D" shell 'cat /sys/class/display/mode'; }
flag () { adb -s "$D" shell 'dumpsys SurfaceFlinger 2>/dev/null | grep -m1 -o "debugDisplayModeSetByBackdoor=[a-z]*"'; }

case "$1" in
	"")  echo "usage: sh tools/stand/ab_rate.sh <mode-id|off>"; exit 2 ;;
	off) adb -s "$D" shell 'service call SurfaceFlinger 1035 i32 135 >/dev/null; service call SurfaceFlinger 1035 i32 9999 >/dev/null' ;;
	*)   adb -s "$D" shell "service call SurfaceFlinger 1035 i32 $1 >/dev/null" ;;
esac

sleep 1
echo "mode: $(mode) | $(flag)"
