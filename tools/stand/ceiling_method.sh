#!/system/bin/sh
# AFRd2: замер сброса «потолка» частоты на конкретном боксе (§60–63 журнала).
#
# Отвечает на три вопроса для ЭТОГО устройства:
#   1) какой сервис кодека здесь есть (Codec2 на Android 12+, OMX на 8-11);
#   2) сколько стоит перезапуск кодека против смены плотности экрана;
#   3) снимает ли перезапуск кодека зажим и сохраняется ли позиция ролика.
#
# Зажим создаётся вручную, чтобы не зависеть от версии демона: режим
# закрепляется низким, плеер стартует под ним, затем режим поднимается —
# плеер остаётся «зажатым» на низкой частоте (§60).
#
# Путь смены режима выбирается сам:
#   * Android 12-14 (backdoor) — транзакция 1035 SurfaceFlinger;
#   * Android 10/11 (вендорный) — помощник afrd-disp.sh из кэша приложения
#     AFRd2 (транзакция 1035 там отвергается с «Operation not permitted»,
#     живая находка на ATV11/`.42`, §63).
#
# Запуск (демон AFRd2 ОСТАНОВЛЕН, ТВ включён):
#   sh /data/local/tmp/ceiling_method.sh             # ролик по ссылке
#   START=manual sh /data/local/tmp/ceiling_method.sh  # ролик открываю руками
#
# Переменные:
#   URL    — ролик (по умолчанию Big Buck Bunny 60 к/с);
#   PLAYER — компонент плеера, если ссылку надо открыть именно им
#            (например com.brouken.player/.PlayerActivity);
#   MIME   — MIME-тип для VIEW (по умолчанию video/mp4);
#   START  — `auto` (ссылка; по умолчанию) или `manual` (ролик запускает
#            человек — нужно там, где плеер не принимает ссылку: на ATV11
#            Cobalt показывает «Видео недоступно», а клик по плитке играет);
#   LOW    — режим, под которым стартуем (по умолчанию 1080p24hz);
#   HIGH   — режим, до которого поднимаем (по умолчанию 1080p60hz);
#   WAIT   — сколько ждать старта видео, с (по умолчанию 25).
#
# Код возврата: 0 — проверки выполнены, 1 — зажим создать не удалось.

URL=${URL:-https://youtu.be/aqz-KE-bpKQ}
MIME=${MIME:-video/mp4}
START=${START:-auto}
LOW=${LOW:-1080p24hz}
HIGH=${HIGH:-1080p60hz}
WAIT=${WAIT:-25}
OUT=/data/local/tmp/ceiling_method.txt
HELPER=/data/user/0/ru.reyter.afrd2/cache/afrd-disp.sh

say () { echo "$@"; echo "$@" >> "$OUT"; }

# --- подача видео-слоя -------------------------------------------------------
# Печатает: «<к/с> <интервал в периодах дисплея> <период, мс> <гистограмма>».
#
# Вердикт ставится НЕ по подаче самой по себе, а в сравнении с частотой
# декодера (он показывает реальную частоту контента): зажим — это когда
# декодер выдаёт 60 к/с, а на экран уходит ~31. Доля «целых периодов» для этого
# не годится: зажатый 60-фпс ролик чередует 1 и 2 периода, и обе величины —
# целые (проверено на стенде, §63).
#
# Средняя подача тоже обманчива сама по себе: один мусорный интервал из буфера
# --latency сдвигает её до бессмысленного значения (§61.1), поэтому печатается
# ещё и медианный интервал в периодах — он к выбросам устойчив.
stats () {
	# Имя видео-слоя зависит от версии: на Android 12+ это
	# `SurfaceView[...](BLAST)#N`, на 8-11 — `SurfaceView - pkg/Activity#N`.
	L=$(dumpsys SurfaceFlinger --list 2>/dev/null | grep 'SurfaceView' \
	    | grep -v -i background | grep -E '\(BLAST\)|SurfaceView - ' | head -1)
	[ -z "$L" ] && { echo "n/a n/a n/a n/a"; return; }
	dumpsys SurfaceFlinger --latency "$L" > /data/local/tmp/cm.lat 2>/dev/null
	awk 'NR == 1 { p = $1 / 1e6; next }
	     NF == 3 && $2 + 0 > 0 && $2 + 0 < 1e17 { n++; t[n] = $2 + 0 }
	     END { if (n < 3) { print "n/a n/a n/a n/a"; exit }
	           m = 0; s = 0
	           for (i = 2; i <= n; i++) {
	             d = (t[i] - t[i-1]) / 1e6
	             if (d <= 0) continue
	             m++; s += d; med[m] = d
	           }
	           if (!m) { print "n/a n/a n/a n/a"; exit }
	           # медиана интервалов (пузырьковая сортировка на 126 значениях
	           # дешевле вызова sort, которого в toybox может не быть)
	           for (i = 1; i <= m; i++)
	             for (j = i + 1; j <= m; j++)
	               if (med[j] < med[i]) { x = med[i]; med[i] = med[j]; med[j] = x }
	           mm = med[int ((m + 1) / 2)]
	           # гистограмма по кратности периода: только ненулевые корзины
	           for (i = 1; i <= m; i++) { k = int (med[i] / p + 0.5); h[k]++ }
	           hist = ""
	           for (k = 1; k <= 6; k++) if (h[k]) hist = hist sprintf ("%dx%d ", k, h[k])
	           printf "%.2f %.2f %.2f %s", 1000 / (s / m), mm / p, p, hist }' \
		/data/local/tmp/cm.lat
}

pos () { dumpsys media_session 2>/dev/null | grep -o 'position=[0-9]*' | head -1 | cut -d= -f2; }
decrate () { # кадры/с по счётчику декодера — в обход подачи
	a=$(awk '/frame count/ { print $4 }' /sys/class/vdec/vdec_status 2>/dev/null); a=${a:-0}
	sleep 5
	b=$(awk '/frame count/ { print $4 }' /sys/class/vdec/vdec_status 2>/dev/null); b=${b:-0}
	echo $(( (b - a) / 5 ))
}

# --- смена режима: 1035 (12-14) или вендорный помощник (10/11) ---------------
BACKDOOR=
mode_set () { # <имя режима>; 0 — применено
	cur=$(cat /sys/class/display/mode 2>/dev/null)
	[ "$cur" = "$1" ] && return 0

	# Путь выбирается по наличию помощника, а не по пробе 1035: на Android 11
	# транзакция отвечает не «not permitted», а «Broken pipe», и проба давала
	# ложное «backdoor работает» (живая находка на `.42` после перепрошивки, §64).
	# Помощник кладёт приложение AFRd2 только на вендорном пути (10/11).
	if [ -x "$HELPER" ]; then
		sh "$HELPER" "$1" >/dev/null 2>&1
		if [ "$(cat /sys/class/display/mode 2>/dev/null)" = "$1" ]; then return 0; fi
		say "  !! помощник не дал режим $1 (сейчас $(cat /sys/class/display/mode 2>/dev/null))"
		return 1
	fi

	# Backdoor (Android 12-14): id режима берём из таблицы display по
	# разрешению и частоте; имя вида «1080p24hz» разбираем на части.
	h=$(echo "$1" | sed -n 's/^\([0-9][0-9]*\)p[0-9.]*hz$/\1/p')
	f=$(echo "$1" | sed -n 's/^[0-9][0-9]*p\([0-9.]*\)hz$/\1/p')
	if [ -z "$h" ] || [ -z "$f" ]; then
		say "  !! имя режима «$1» не разобрать (ожидается <высота>p<частота>hz)"
		return 1
	fi

	id=$(dumpsys display 2>/dev/null \
	     | grep -oE 'id=[0-9]+, width=[0-9]+, height=[0-9]+, fps=[0-9.]+' \
	     | awk -F'[=,]' -v h="$h" -v f="$f" '
	        { id = $2; hh = $6; ff = $8 + 0
	          if (hh != h) next
	          d = ff - f; if (d < 0) d = -d
	          if (d < f * 0.02) { print id; exit } }')
	if [ -z "$id" ]; then
		say "  !! режим $1 не найден в таблице display"
		return 1
	fi

	service call SurfaceFlinger 1035 i32 "$id" >/dev/null 2>&1
	[ "$(cat /sys/class/display/mode 2>/dev/null)" = "$1" ] && return 0
	say "  !! 1035 с id $id не дал режим $1"
	return 1
}

codec_svc () {
	for s in android-hardware-media-c2-hal-1-2 android-hardware-media-c2-hal-1-1 \
	         android-hardware-media-c2-hal-1-0 vendor.media.omx media.omx \
	         vendor.media.c2 media.swcodec; do
		[ "$(getprop init.svc.$s)" = running ] && { echo "$s"; return 0; }
	done
	return 1
}

# Перезапуск сервиса кодека с подтверждением: на 10+ по init.svc_debug_pid,
# на 8-9 (свойства нет, хотя перезапуск работает) — по смене pid процесса.
# Чем процесс сервиса виден в `ps`: имя init-сервиса и имя бинарника не
# совпадают (android-hardware-media-c2-hal-1-2 -> media.c2@1.2).
codec_bin () {
	case "$1" in
		android-hardware-media-c2-hal-1-2) echo "media.c2@1.2" ;;
		android-hardware-media-c2-hal-1-1) echo "media.c2@1.1" ;;
		android-hardware-media-c2-hal-1-0) echo "media.c2@1.0" ;;
		vendor.media.omx|media.omx)        echo "media.omx" ;;
		*)                                 echo "$1" ;;
	esac
}

codec_restart () { # <сервис>; печатает мс
	bin=$(codec_bin "$1")
	p0=$(getprop init.svc_debug_pid.$1)
	b0=$(ps -A -o PID,ARGS 2>/dev/null | grep "$bin" | grep -v grep | awk '{ print $1 }' | tr '\n' ',')
	t0=$(date +%s%3N)
	setprop ctl.restart "$1"
	i=0
	while [ $i -lt 120 ]; do
		p=$(getprop init.svc_debug_pid.$1)
		a=$(ps -A -o PID,ARGS 2>/dev/null | grep "$bin" | grep -v grep | awk '{ print $1 }' | tr '\n' ',')
		if [ -n "$p" ] && [ "$p" != "$p0" ]; then echo $(( $(date +%s%3N) - t0 )); return 0; fi
		if [ -z "$p0" ] && [ -n "$a" ] && [ "$a" != "$b0" ]; then echo $(( $(date +%s%3N) - t0 )); return 0; fi
		i=$((i + 1)); sleep 0.1
	done
	echo 0; return 1
}

density_cost () {
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

wait_video () { # ждёт появления декодера; 0 — появился
	i=0
	while [ $i -lt $((WAIT * 2)) ]; do
		c=$(awk '/frame count/ { print $4 }' /sys/class/vdec/vdec_status 2>/dev/null)
		[ -n "$c" ] && return 0
		i=$((i + 1)); sleep 0.5
	done
	return 1
}

# Плеер может открыть файл на паузе — Just Player так и делает (живая находка
# на `.42` после перепрошивки, §64: замер подачи возвращал n/a, потому что
# кадры не шли, а режим при этом менялся). Дожимаем Play и проверяем, что
# позиция ролика растёт.
ensure_playing () {
	p1=$(pos)
	input keyevent KEYCODE_MEDIA_PLAY >/dev/null 2>&1
	sleep 3
	p2=$(pos)
	if [ -n "$p1" ] && [ -n "$p2" ] && [ "$p1" = "$p2" ]; then
		# ещё попытка: часть плееров реагирует только на центр/DPAD_CENTER
		input keyevent KEYCODE_DPAD_CENTER >/dev/null 2>&1
		sleep 3
		p3=$(pos)
		if [ "$p1" = "$p3" ]; then
			say "  !! позиция ролика не растёт ($p1) — пауза не снята"
			return 1
		fi
	fi
	return 0
}

start_video () {
	am force-stop com.google.android.youtube.tv >/dev/null 2>&1
	am force-stop org.videolan.vlc >/dev/null 2>&1
	am force-stop net.gtvbox.videoplayer >/dev/null 2>&1
	am force-stop com.brouken.player >/dev/null 2>&1
	if [ "$START" = manual ]; then
		say "  запустите ролик на боксе (у вас $WAIT с)…"
		wait_video
		return $?
	fi

	# PLAYER — компонент активности плеера. Нужен там, где на file:// не
	# находится обработчик либо плеер по умолчанию не тот (на ATV11 после
	# сброса из плееров только Vimu со своим AFR; годный — Just Player).
	if [ -n "$PLAYER" ]; then
		am start -n "$PLAYER" -a android.intent.action.VIEW -d "$URL" -t "$MIME" >/dev/null 2>&1
	else
		am start -a android.intent.action.VIEW -d "$URL" >/dev/null 2>&1
	fi
	wait_video
}

clamp () { # создать зажим: старт под LOW, подъём до HIGH
	mode_set "$LOW" || { say "  !! режим $LOW не выставился"; return 1; }
	sleep 2
	start_video || { say "  !! видео не стартовало"; return 1; }
	ensure_playing
	sleep 3
	set -- $(stats)
	say "    под $LOW:      подача $1 к/с, интервал $2 периода(ов) [$4], декодер $(decrate) к/с"
	mode_set "$HIGH" || { say "  !! режим $HIGH не выставился"; return 1; }
	sleep 7
	set -- $(stats)
	say "    при $HIGH:     подача $1 к/с, интервал $2 периода(ов) [$4], декодер $(decrate) к/с  <- зажим, если подача много ниже декодера"
}

# --- 1. устройство -----------------------------------------------------------
say "=== устройство: sdk $(getprop ro.build.version.sdk), $(getprop ro.product.model), $(getprop ro.board.platform)"
say "--- режим сейчас: $(cat /sys/class/display/mode 2>/dev/null)"
SVC=$(codec_svc)
for s in android-hardware-media-c2-hal-1-2 android-hardware-media-c2-hal-1-1 \
         android-hardware-media-c2-hal-1-0 vendor.media.omx media.omx \
         vendor.media.c2 media.swcodec; do
	st=$(getprop init.svc.$s)
	[ -n "$st" ] && say "    $s = $st"
done
[ -n "$SVC" ] && say "--- выбран сервис кодека: $SVC" || say "!! запущенного сервиса кодека нет"

# --- 2. цена вмешательств ----------------------------------------------------
say ""
say "=== цена вмешательств (для этого бокса)"
if [ -n "$SVC" ]; then
	t=$(codec_restart "$SVC")
	if [ "$t" -gt 0 ]; then say "  перезапуск кодека: ${t} мс"; else say "  перезапуск кодека: НЕ подтверждён"; fi
fi
d=$(density_cost)
if [ "$d" -gt 0 ]; then say "  смена плотности (туда-обратно): ${d} мс"; else say "  смена плотности: не измерить"; fi

# --- 3. зажим и снятие -------------------------------------------------------
say ""
say "=== зажим и снятие перезапуском кодека"
if clamp; then
	p0=$(pos); say "    позиция до: ${p0:-н/д}"
	if [ -n "$SVC" ]; then
		t=$(codec_restart "$SVC")
		say "    перезапуск: ${t} мс"
		sleep 10
		set -- $(stats)
		say "    после:         подача $1 к/с, интервал $2 периода(ов) [$4], декодер $(decrate) к/с"
		say "    позиция: ${p0:-н/д} -> $(pos)"
	fi
fi

say ""
say "=== зажим и снятие сменой плотности"
if clamp; then
	p0=$(pos); say "    позиция до: ${p0:-н/д}"
	d=$(density_cost); say "    смена плотности: ${d} мс"
	sleep 10
	set -- $(stats)
	say "    после:         подача $1 к/с, интервал $2 периода(ов) [$4], декодер $(decrate) к/с"
	say "    позиция: ${p0:-н/д} -> $(pos)"
fi

say ""
say "=== контроль: без вмешательства"
if clamp; then
	sleep 10
	set -- $(stats)
	say "    остаётся:      подача $1 к/с, интервал $2 периода(ов) [$4], декодер $(decrate) к/с"
fi

am force-stop com.google.android.youtube.tv >/dev/null 2>&1
mode_set "$HIGH" >/dev/null 2>&1
say ""
say "=== итог записан в $OUT"
