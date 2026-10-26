#!/system/bin/sh
# AFRd2: замер сброса «потолка» частоты на конкретном боксе (§60–62 журнала).
#
# Отвечает на три вопроса для ЭТОГО устройства:
#   1) какой сервис кодека здесь есть (Codec2 на Android 12+, OMX на 8-11);
#   2) сколько стоит перезапуск кодека против смены плотности экрана;
#   3) снимает ли перезапуск кодека зажим и сохраняется ли позиция ролика.
#
# Зажим создаётся вручную, чтобы не зависеть от версии демона: режим
# закрепляется низким (1035), плеер стартует под ним, затем режим поднимается —
# плеер остаётся «зажатым» на низкой частоте (§60).
#
# Запуск (демон AFRd2 ОСТАНОВЛЕН, ТВ включён, ролик доступен по ссылке):
#   URL="https://youtu.be/aqz-KE-bpKQ" sh /data/local/tmp/ceiling_method.sh
#
# Переменные:
#   URL   — ролик (по умолчанию Big Buck Bunny 60 к/с);
#   LOW   — id режима, под которым стартуем (по умолчанию 6 = 1080p24hz);
#   HIGH  — id режима, до которого поднимаем (по умолчанию 1 = 1080p60hz);
#   WAIT  — сколько ждать старта видео, с (по умолчанию 14).
#
# Код возврата: 0 — все проверки выполнены, 1 — не удалось создать зажим.

URL=${URL:-https://youtu.be/aqz-KE-bpKQ}
LOW=${LOW:-6}
HIGH=${HIGH:-1}
WAIT=${WAIT:-14}
OUT=/data/local/tmp/ceiling_method.txt

say () { echo "$@"; echo "$@" >> "$OUT"; }

# Подача видео-слоя: к/с, доля кадров «в темпе» периода дисплея и период.
# Доля важнее среднего: один мусорный интервал из буфера --latency сдвигает
# среднее до бессмысленного значения (живой случай §61.1), а доля кратности
# периода устойчива.
stats () {
	L=$(dumpsys SurfaceFlinger --list 2>/dev/null | grep 'SurfaceView\[' | grep -i blast | head -1)
	[ -z "$L" ] && { echo "n/a 0 0"; return; }
	dumpsys SurfaceFlinger --latency "$L" > /data/local/tmp/cm.lat 2>/dev/null
	awk 'NR == 1 { p = $1 / 1e6; next }
	     NF == 3 && $2 + 0 > 0 && $2 + 0 < 1e17 { n++; t[n] = $2 + 0 }
	     END { if (n < 3) { print "n/a 0 0"; exit }
	           m = 0; ok = 0; s = 0
	           for (i = 2; i <= n; i++) { d = (t[i] - t[i-1]) / 1e6; if (d <= 0) continue
	                                      m++; s += d; if (d / p < 1.5) ok++ }
	           if (m) printf "%.2f %d %.2f", 1000 / (s / m), 100 * ok / m, p; else print "n/a 0 0" }' \
		/data/local/tmp/cm.lat
}

pos () { dumpsys media_session 2>/dev/null | grep -A12 'starboard\|youtube\|vlc' | grep -o 'position=[0-9]*' | head -1 | cut -d= -f2; }

# --- 1. какой сервис кодека --------------------------------------------------
say "=== устройство: sdk $(getprop ro.build.version.sdk), $(getprop ro.product.model), $(getprop ro.board.platform)"
say "--- кандидаты сервисов кодека:"
SVC=
for s in android-hardware-media-c2-hal-1-2 android-hardware-media-c2-hal-1-1 \
         android-hardware-media-c2-hal-1-0 vendor.media.omx media.omx \
         vendor.media.c2 media.swcodec; do
	st=$(getprop init.svc.$s)
	[ -n "$st" ] && say "    $s = $st"
	if [ "$st" = "running" ] && [ -z "$SVC" ]; then SVC=$s; fi
done
if [ -z "$SVC" ]; then
	say "!! запущенного сервиса кодека не найдено — сброс перезапуском недоступен"
else
	say "--- выбран: $SVC"
fi

# --- 2. цена перезапуска -----------------------------------------------------
# Ждём смены pid: init молча игнорирует ctl.restart для несуществующего сервиса.
restart_timed () { # <сервис> -> мс или 0
	t0=$(date +%s%3N)
	p0=$(getprop init.svc_debug_pid.$1)
	setprop ctl.restart "$1"
	i=0
	while [ $i -lt 200 ]; do
		p=$(getprop init.svc_debug_pid.$1)
		if [ -n "$p" ] && [ "$p" != "$p0" ]; then
			echo $(( $(date +%s%3N) - t0 )); return 0
		fi
		i=$((i + 1)); sleep 0.1
	done
	echo 0; return 1
}

# Смена плотности: команда + возврат, замер по времени до готовности `wm`.
density_cost () {
	# Действующая плотность: override важнее физической (иначе +1 к физической
	# при заданном override ничего не меняет и «сброса» не происходит).
	cur=$(wm density 2>/dev/null | awk '
		/Override density/ { o = $3 }
		/Physical density/ { p = $3 }
		END { print (o != "" ? o : p) }')
	[ -z "$cur" ] && { echo 0; return 1; }
	t0=$(date +%s%3N)
	wm density $((cur + 1)) >/dev/null 2>&1
	wm density "$cur" >/dev/null 2>&1 || wm density reset >/dev/null 2>&1
	echo $(( $(date +%s%3N) - t0 ))
}

# --- 3. зажим и снятие -------------------------------------------------------
clamp () { # создать зажим: старт под LOW, подъём до HIGH
	am force-stop com.google.android.youtube.tv >/dev/null 2>&1
	am force-stop org.videolan.vlc >/dev/null 2>&1
	service call SurfaceFlinger 1035 i32 9999 >/dev/null 2>&1
	service call SurfaceFlinger 1035 i32 "$LOW" >/dev/null 2>&1
	sleep 2
	am start -a android.intent.action.VIEW -d "$URL" >/dev/null 2>&1
	i=0
	while [ $i -lt $((WAIT * 2)) ]; do
		c=$(awk '/frame count/ { print $4 }' /sys/class/vdec/vdec_status 2>/dev/null)
		[ -n "$c" ] && break
		i=$((i + 1)); sleep 0.5
	done
	sleep 3
	set -- $(stats)
	before_rate=$1; before_ok=$2
	service call SurfaceFlinger 1035 i32 "$HIGH" >/dev/null 2>&1
	sleep 6
	set -- $(stats)
	say "    зажим: до подъёма ${before_rate} к/с (в темпе ${before_ok}%), после ${1} к/с (в темпе ${2}%)"

}

say ""
say "=== цена вмешательств"
if [ -n "$SVC" ]; then
	t=$(restart_timed "$SVC")
	if [ "$t" -gt 0 ]; then say "  перезапуск кодека ($SVC): ${t} мс"; else say "  перезапуск кодека ($SVC): НЕ подтверждён (pid не сменился)"; fi
fi
d=$(density_cost)
if [ "$d" -gt 0 ]; then say "  смена плотности (туда-обратно): ${d} мс"; else say "  смена плотности: не измерить"; fi

say ""
say "=== зажим и снятие перезапуском кодека"
clamp
p0=$(pos)
say "    позиция до: ${p0:-н/д}"
if [ -n "$SVC" ]; then
	restart_timed "$SVC" >/dev/null
	sleep 10
	set -- $(stats)
	say "    после перезапуска кодека: ${1} к/с, в темпе ${2}%"
	say "    позиция после: $(pos) (было ${p0:-н/д})"
fi

say ""
say "=== зажим и снятие сменой плотности"
clamp
p0=$(pos)
density_cost >/dev/null
sleep 10
set -- $(stats)
say "    после смены плотности: ${1} к/с, в темпе ${2}%"
say "    позиция после: $(pos) (было ${p0:-н/д})"

am force-stop com.google.android.youtube.tv >/dev/null 2>&1
service call SurfaceFlinger 1035 i32 9999 >/dev/null 2>&1
say ""
say "=== итог (файл $OUT)"
