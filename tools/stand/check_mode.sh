#!/bin/sh
# Стенд: снимок тракта «AFR» одной строкой, для обоих путей демона.
#
# Печатает 4 поля через «|»:
#
#   ATV14 (новый путь):   kernel mode | HDMI timing (sname, h_freq, v_freq) | SF activeMode | backdoor flag
#   legacy (Android 6-9): kernel mode | n/a, n/a, n/a | rate=<Гц из vinfo> | n/a
#
# Почему поля разные: на новом пути режим задаёт backdoor SurfaceFlinger, и
# фактическая частота видна в `dumpsys SurfaceFlinger` (activeMode) вместе с
# флагом «режим задан снаружи»; на старом пути режим пишется прямо в
# /sys/class/display/mode, флага нет, а фактическую частоту даёт
# /sys/class/display/vinfo (sync_duration_num/den). Путь определяется по тем же
# узлам, что platform_detect() в демоне: legacy — если есть старый узел HDMI.
#
# Запуск:
#   sh tools/stand/check_mode.sh
# Переменные:
#   ADB_DEV (или D) — адрес adb-устройства, по умолчанию 192.168.88.50:5555.
#   PLAT            — atv14 | legacy, переопределяет автоопределение.
#   Скрипт рассчитан на root-adb (новый путь) или обычный adb shell со чтением
#   sysfs (старый путь: mode и vinfo читаются без root, проверено на .76).
#
# Код возврата: 0 — снимок получен, 2 — стенд недоступен (снимок не снят).
#
# Фактический вид строки ATV14 для клипа 23.976 fps (живой прогон 2026-09-29, см.
# README → «Статус приёмки»): 1080p24hz | sname: 1080p24hz, 26973, 23976 |
# activeMode=23.98 Hz | debugDisplayModeSetByBackdoor=true (для 1080p59.94 —
# sname: 1080p60hz, 67433, 59940). После остановки видео — исходный режим и
# backdoor=false.
#
# Фактический вид строки legacy (живой прогон 2026-10-08 на ATV9 .76): при
# удержании — 1080p24hz | n/a, n/a, n/a | rate=23.976 Hz | n/a; после стопа —
# 1080p50hz | n/a, n/a, n/a | rate=50.000 Hz | n/a.
# Любое нечитаемое поле печатается как n/a (в т.ч. само поле режима).

D=${ADB_DEV:-${D:-192.168.88.50:5555}}
PLAT=${PLAT:-auto}

if ! adb -s "$D" shell true >/dev/null 2>&1; then
	echo "стенд $D недоступен — снимок не снят" >&2
	exit 2
fi

if [ "$PLAT" = auto ]; then
	if adb -s "$D" shell '[ -e /sys/class/switch/hdmi/cable.0/state ] || [ -e /sys/class/switch/hdmi/state ]' >/dev/null 2>&1; then
		PLAT=legacy
	else
		PLAT=atv14
	fi
fi

# Старый путь: частота — из vinfo (sync_duration_num/den, например 2997/125 для
# 23.976 Гц). awk считает с тремя знаками, чтобы проверки по подстроке видели
# «23.976». Чтение mode и vinfo прав root не требует, поэтому обычный adb shell.
if [ "$PLAT" = legacy ]; then
	adb -s "$D" shell '
		mode=$(cat /sys/class/display/mode 2>/dev/null)
		num=$(sed -n "s/^[[:space:]]*sync_duration_num:[[:space:]]*//p" /sys/class/display/vinfo 2>/dev/null | head -1)
		den=$(sed -n "s/^[[:space:]]*sync_duration_den:[[:space:]]*//p" /sys/class/display/vinfo 2>/dev/null | head -1)
		rate=$(awk -v n="$num" -v d="$den" "BEGIN { if (n+0 > 0 && d+0 > 0) printf \"%.3f\", n/d }" 2>/dev/null)
		[ -n "$mode" ] || mode=n/a
		[ -n "$rate" ] || rate=n/a
		printf "%s | n/a, n/a, n/a | rate=%s Hz | n/a\n" "$mode" "$rate"
	'
	exit $?
fi

# Новый путь: один вызов adb — dumpsys SurfaceFlinger читается один раз (он
# большой). В disp_mode h/v-частоты лежат одной строкой: «h/v_freq: <h>/<v>»
# (h — в Гц, v — в мГц: 67433/59940 для 1080p59.94). Отдельных h_freq/v_freq
# в файле нет, поэтому ищем именно эту строку и режем её по «/».
adb -s "$D" shell '
	mode=$(cat /sys/class/display/mode 2>/dev/null)
	dpm=$(cat /sys/class/amhdmitx/amhdmitx0/disp_mode 2>/dev/null)
	sname=$(printf "%s\n" "$dpm" | grep -m1 -oE "sname:? *[^ ]+" | head -1)
	hvf=$(printf "%s\n" "$dpm" | sed -n "s/^h\/v_freq: *//p" | head -1)
	hf=${hvf%%/*}
	vf=${hvf##*/}
	[ "$hf" = "$hvf" ] && hf=
	sf=$(dumpsys SurfaceFlinger 2>/dev/null)
	active=$(echo "$sf" | grep -oE "activeMode=[0-9.]+ Hz" | head -1)
	backdoor=$(echo "$sf" | grep -oE "debugDisplayModeSetByBackdoor=[a-z]+" | head -1)
	[ -n "$mode" ] || mode=n/a
	[ -n "$sname" ] || sname=n/a
	[ -n "$hf" ] || hf=n/a
	[ -n "$vf" ] || vf=n/a
	[ -n "$active" ] || active=n/a
	[ -n "$backdoor" ] || backdoor=n/a
	printf "%s | %s, %s, %s | %s | %s\n" "$mode" "$sname" "$hf" "$vf" "$active" "$backdoor"
'
