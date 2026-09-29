#!/bin/sh
# Стенд ATV14: снимок тракта «AFR» одной строкой.
#
# Печатает: kernel mode | HDMI timing (sname, h_freq, v_freq) | SF activeMode | backdoor flag
#
# Запуск:
#   sh tools/stand/check_mode.sh
# Переменные:
#   ADB_DEV (или D) — адрес adb-устройства, по умолчанию 192.168.88.50:5555.
#   Скрипт рассчитан на root-adb (по умолчанию на стенде так и есть): dumpsys и
#   чтение sysfs требуют прав root.
#
# Код возврата: 0 — снимок получен, 2 — стенд недоступен (снимок не снят).
#
# Ожидаемые значения для клипа 23.976 fps (эталон плана Task 9; прогоном на
# стенде ещё НЕ подтверждён — на момент написания стенд недоступен):
#   1080p24hz | ... | activeMode=23.98 Hz | debugDisplayModeSetByBackdoor=true
# после остановки видео ожидается возврат исходного режима и backdoor=false.

D=${ADB_DEV:-${D:-192.168.88.50:5555}}

if ! adb -s "$D" shell true >/dev/null 2>&1; then
	echo "стенд $D недоступен — снимок не снят" >&2
	exit 2
fi

# Один вызов adb: dumpsys SurfaceFlinger читается один раз (он большой).
# Отсутствующие поля печатаются как n/a, чтобы «пусто» не путалось с ошибкой
# чтения (например, когда ядро не смонтировало нужный узел).
adb -s "$D" shell '
	mode=$(cat /sys/class/display/mode 2>/dev/null)
	dpm=$(cat /sys/class/amhdmitx/amhdmitx0/disp_mode 2>/dev/null)
	sname=$(printf "%s\n" "$dpm" | grep -m1 -oE "sname:? *[^ ]+" | head -1)
	hf=$(printf "%s\n" "$dpm" | grep -m1 -oE "h_freq:? *[0-9]+" | head -1)
	vf=$(printf "%s\n" "$dpm" | grep -m1 -oE "v_freq:? *[0-9]+" | head -1)
	sf=$(dumpsys SurfaceFlinger 2>/dev/null)
	active=$(echo "$sf" | grep -oE "activeMode=[0-9.]+ Hz" | head -1)
	backdoor=$(echo "$sf" | grep -oE "debugDisplayModeSetByBackdoor=[a-z]+" | head -1)
	[ -n "$sname" ] || sname=n/a
	[ -n "$hf" ] || hf=n/a
	[ -n "$vf" ] || vf=n/a
	[ -n "$active" ] || active=n/a
	[ -n "$backdoor" ] || backdoor=n/a
	printf "%s | %s, %s, %s | %s | %s\n" "$mode" "$sname" "$hf" "$vf" "$active" "$backdoor"
'
