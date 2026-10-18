#!/bin/sh
# Замер времени переключения режима HDMI у конкретного телевизора (журнал §49.6).
#
# Зачем. Плеер на Android 11+ запоминает частоту дисплея в начале сессии
# проигрывания. Если ТВ переключает режим медленнее, чем плеер успевает начать
# рендер, демон не успевает поднять режим до максимума (switch.bump) и ролик
# остаётся «зажатым» — 60-фпс контент идёт ~50 к/с. Время переключения задаёт
# приёмник (HDMI-линк и телевизор) и путь смены режима, поэтому его полезно
# измерить на своём стенде.
#
# Использование:
#   tv_switch_lat.sh [-D адрес] [пары...]
# Пары по умолчанию: 1080p50hz/1080p60hz, 720p50hz/720p60hz.
# Печатает время от вызова до появления целевой частоты в vinfo, мс.
# Всё считается на самом боксе: накладные adb (50-100 мс) в результат не входят.

D=${D:-192.168.88.42:5555}
CLI=${CLI:-/data/user/0/ru.reyter.afrd2/cache/afrd-disp.sh}
RUNS=${RUNS:-3}

dev () { adb -s "$D" shell "$@" 2>/dev/null | tr -d '\r'; }
rt ()  { adb -s "$D" shell "su -c \"$1\"" 2>/dev/null | tr -d '\r'; }

PROBE=/data/local/tmp/afrd_switchtime.sh

install_probe () {
	cat > /tmp/afrd_switchtime.sh.$$ <<'EOF'
#!/system/bin/sh
CLI=$1
MODE=$2
TARGET=$3

rate () {
	cat /sys/class/display/vinfo 2>/dev/null | awk -F: '
		/sync_duration_num/ { n = $2 + 0 }
		/sync_duration_den/ { d = $2 + 0 }
		END { if (d > 0) printf "%.3f", n / d; else print "0" }'
}
now_ms () { awk '{ printf "%d", $1 * 1000 }' /proc/uptime; }

_before=$(rate)
_t0=$(now_ms)
sh "$CLI" "$MODE" >/dev/null 2>&1
_rc=$?
_i=0
while [ "$_i" -lt 200 ]; do
	_r=$(rate)
	if awk -v a="$_r" -v b="$TARGET" 'BEGIN { exit !(a + 0 > 0 && b - a < 0.1 && a - b < 0.1) }'; then
		break
	fi
	sleep 0.05
	_i=$((_i + 1))
done
_t1=$(now_ms)
echo "$_before $(rate) $_rc $((_t1 - _t0))"
EOF
	adb -s "$D" push /tmp/afrd_switchtime.sh.$$ "$PROBE" >/dev/null 2>&1
	rm -f /tmp/afrd_switchtime.sh.$$
	dev "chmod 755 $PROBE"
}

measure () { # <режим> <частота>
	rt "sh $PROBE $CLI $1 $2"
}

pairs=${*:-"1080p50hz:50.000 1080p60hz:59.940 720p50hz:50.000 720p60hz:60.000"}

install_probe || { echo "не удалось положить зонд на бокс"; exit 3; }

echo "стенд $D, путь $CLI"
for p in $pairs; do
	mode=${p%%:*}
	hz=${p##*:}
	printf '%-14s ' "$mode"
	i=0
	while [ "$i" -lt "$RUNS" ]; do
		out=$(measure "$mode" "$hz")
		echo "$out" | awk -v m="$mode" '{
			printf "%s мс", $4
			if ($1 + 0 > 0 && $2 + 0 > 0 && ($1 > $2 ? $1 - $2 : $2 - $1) > 0.2)
				printf " (частота %.3f -> %.3f)", $1, $2
			printf "; "
		}'
		i=$((i + 1))
	done
	echo
done

echo
echo "Как читать: это время до появления целевой частоты на выходе HDMI."
echo "Оно и есть запас, который нужен switch.bump до первого кадра плеера:"
echo "окно у штатного YouTube TV — ~0.6 с на смене ролика и до 4 с на холодном"
echo "старте. Если переключение дольше окна, ролик, начатый под низкой частотой,"
echo "останется зажатым (демон выправит его сбросом потолка, switch.ceiling.reset)."
