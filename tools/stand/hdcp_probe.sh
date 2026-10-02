#!/bin/sh
# AFRd2: снимок состояния HDCP на боксе (хост-скрипт) — для теста R35.
#
# Одной командой adb снимает всё, что относится к HDCP, вместе с текущим режимом
# вывода: узлы ядра, состояние служб, флаг backdoor. Этим снимком фиксируется
# состояние «до» и «после» переключения настройки бокса и смены режима, чтобы
# понять, переподтверждать ли HDCP после смены режима (чёрный экран на
# защищённом стриминге).
#
#   sh tools/stand/hdcp_probe.sh                      # снимок «как есть»
#   SWITCH=23.98 sh tools/stand/hdcp_probe.sh         # сначала переключить режим
#   AUTH=1 sh tools/stand/hdcp_probe.sh               # сначала попросить вендорский
#                                                     #   сервис подтвердить HDCP
#   OUT=/tmp/atv9/hdcp.txt sh tools/stand/hdcp_probe.sh
#
# Настройки ОС не меняет: SWITCH — та же транзакция 1035, что использует демон
# (через tools/stand/ab_rate.sh), AUTH по умолчанию не вызывается вовсе.

D=${D:-192.168.88.50:5555}
OUT=${OUT:-}

probe_cmd='
	for f in hdcp_mode hdcp_type_policy hdcp_lstore hdcp_ver; do
		printf "%s=[%s] " "$f" "$(su -c "cat /sys/class/amhdmitx/amhdmitx0/$f" 2>&1)"
	done
	echo
	echo "службы hdcp_tx22/tee_hdcp: $(getprop init.svc.hdcp_tx22) / $(getprop init.svc.vendor.tee_hdcp)"
	echo "режим: $(cat /sys/class/display/mode) / $(su -c "grep -m1 sync_duration_num /sys/class/display/vinfo" | tr -d " ")"
	echo "backdoor: $(dumpsys SurfaceFlinger 2>/dev/null | grep -m1 -o "debugDisplayModeSetByBackdoor=[a-z]*")"
'

if [ -n "$SWITCH" ]; then
	echo "переключаю режим на $SWITCH Гц:"
	D="$D" sh "$(dirname "$0")/ab_rate.sh" "$SWITCH" || exit 1
fi

if [ -n "$AUTH" ]; then
	echo "переподтверждение HDCP через вендорский сервис:"
	adb -s "$D" shell "su -c '/vendor/bin/meson_display_client -r userSpaceHDCPTxAuth'" 2>&1 | tail -3
	sleep 2
fi

label=${LABEL:-снимок}
line="$(date '+%H:%M:%S') $label:"
body=$(adb -s "$D" shell "$probe_cmd" 2>/dev/null)
printf '%s\n%s\n' "$line" "$body"
if [ -n "$OUT" ]; then
	printf '%s\n%s\n' "$line" "$body" >> "$OUT"
	echo "(дописано в $OUT)"
fi
