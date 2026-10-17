#!/bin/sh
# AFRd2: слепой прогон «зажима FPS» в штатном YouTube TV (ATV9 `.48` и ATV14 `.50`).
#
# Вопрос прогона: зависит ли плавность ролика от того, ПОД КАКИМ режимом экрана
# ролик стартовал (гипотеза §28.9: плеер выбирает темп отрисовки в момент старта
# как min(частота ролика, частота экрана) и потом его не поднимает). На ATV9
# (`.48`, бывший `.76`) числовых опор по подаче кадров нет (§30.13: `SurfaceFlinger --latency` по
# видео-слою не обновляется), поэтому вердикт даёт глаз владельца. На ATV14
# `--latency` живой, и рядом с глазным вердиктом считается числовой — но только
# в `report`, после вердиктов (иначе слепота ломается).
#
# Путь смены режима выбирается по стенду, как в `platform.c` демона:
#   legacy (Android 6-9): запись в `/sys/class/display/mode`;
#   ATV14 (sdk 31+, узел `switch/hdmi` отсутствует): транзакция **1035**
#     SurfaceFlinger (`setActiveModeFromBackdoor`) с id из таблицы режимов;
#     узел `mode` там только отражает состояние (§4е). Флаг backdoor ставится
#     на прогон (фреймворк не перебивает режим) и снимается в `rollback`.
# Android 10/11 (вендорный SystemControl) скрипт не обслуживает: там 1035 с
# одним int — другая операция, а на ATV11 вызов роняет SurfaceFlinger.
#
# Прогон слепой: условия (режим старта и момент его смены) задаёт этот скрипт,
# владелец слышит только «предъявление 1..6» и называет «плавно» либо «рывки».
# Ключ (условия + что ожидается) печатает команда `key` — до конца прогона его
# читать нельзя, иначе слепоты нет.
#
# Демон на время прогона СНЯТ: иначе он сам переключал бы режимы (то же
# требование, что в `ab_rate.sh` и `youtube_clamp.sh`). Служба приложения поднята
# обратно командой `rollback`. Ini при этом не правится — журнал демона не нужен.
#
# Запуск (окно с владельцем у ТВ):
#   D=192.168.88.50:5555 sh tools/stand/youtube_blind.sh prep
#   ... run 1 ; mark 1 smooth|jerky|ad ; run 2 ; ... (так шесть раз)
#   sh tools/stand/youtube_blind.sh report    # вердикты + что делал скрипт
#   sh tools/stand/youtube_blind.sh key       # расшифровка (после вердиктов!)
#   D=... sh tools/stand/youtube_blind.sh rollback
#
# Переменные: D (ADB_DEV), BASE_HZ (режим в конце, по умолчанию 50), SECS
# (сколько играть предъявление, по умолчанию 35), YTBLIND_DIR (протокол на
# хосте, по умолчанию /tmp/ytblind), YTBLIND_ORDER (порядок пунктов плана).
#
# Код возврата: 0 — ок, 1 — ошибка ввода, 2 — стенд недоступен, 3 — проверка
# состояния не сошлась (например, демон не снялся).

set -u

D=${ADB_DEV:-${D:-192.168.88.76:5555}}
DIR=${YTBLIND_DIR:-/tmp/ytblind}
BASE_HZ=${BASE_HZ:-50}
SECS=${SECS:-35}
ORDER=${YTBLIND_ORDER:-6 1 4 3 5 2}

PKG=com.google.android.youtube.tv
SVC=ru.reyter.afrd2/ru.cobra.zap.afrd.gui.AFRService

EV="$DIR/events.txt"
PRE_FILE="$DIR/pre-state.txt"
SINK_FILE="$DIR/sink.txt"
LATDIR="$DIR/lat"
mkdir -p "$DIR" "$LATDIR" || exit 1

SINK=${YTBLIND_SINK:-}

say () {
	echo "$@"
	echo "$(date '+%H:%M:%S') $*" >> "$EV"
}

dev () { adb -s "$D" shell "$@" 2>/dev/null | tr -d '\r'; }
suroot () { adb -s "$D" shell "su -c \"$1\"" 2>/dev/null | tr -d '\r'; }

need_stand () {
	if ! adb -s "$D" get-state >/dev/null 2>&1; then
		echo "стенд $D недоступен (adb get-state)" >&2
		exit 2
	fi
}

# --- путь смены режима (как platform.c демона) -------------------------------
#
# legacy: узел /sys/class/switch/hdmi (Android 6-9) — пишем имя режима в
# /sys/class/display/mode. ATV14: узла switch нет, режим меняет только
# транзакция 1035 SurfaceFlinger по id из таблицы режимов (§4е). Android 10/11
# (sdk 29/30) не обслуживаем: там 1035 с одним int значит другое, а на ATV11
# вызов с одним int роняет SurfaceFlinger (§33.18, platform.c).

detect_sink () { # печатает sysfs|sf; при отказе — причина в stderr, код 3
	sdk=$(dev 'getprop ro.build.version.sdk')
	case "$sdk" in
	''|*[!0-9]*)	echo "не прочитать ro.build.version.sdk ($sdk)" >&2; return 3 ;;
	esac
	if [ "$sdk" -le 28 ]; then
		echo sysfs
		return 0
	fi
	if [ "$sdk" -le 30 ]; then
		echo "Android 10/11 (sdk $sdk): режим задаёт вендорный SystemControl," \
		     "транзакция 1035 там не та — этот прогон не поддержан" >&2
		return 3
	fi
	# Проба backdoor: неверный id -> BAD_VALUE "Invalid argument", флаг снят.
	out=$(dev 'service call SurfaceFlinger 1035 i32 -1')
	case "$out" in
	*"Invalid argument"*)	echo sf ;;
	*)	echo "backdoor SurfaceFlinger недоступен: $out" >&2; return 3 ;;
	esac
}

save_sink () { # один раз на прогон (проба снимает флаг backdoor)
	SINK=${YTBLIND_SINK:-}
	if [ -z "$SINK" ]; then
		SINK=$(detect_sink) || return 3
	fi
	echo "$SINK" > "$SINK_FILE"
}

load_sink () {
	[ -n "$SINK" ] && return 0
	SINK=$(cat "$SINK_FILE" 2>/dev/null)
	[ -n "$SINK" ] && return 0
	echo "путь смены режима не определён — сначала prep" >&2
	return 3
}

sf_active_hz () {
	dev "dumpsys SurfaceFlinger 2>/dev/null | grep -m1 -o 'activeMode=[0-9.]*'" | sed 's/^ *activeMode=//'
}

sf_flag () {
	dev "dumpsys SurfaceFlinger 2>/dev/null | grep -m1 -o 'debugDisplayModeSetByBackdoor=[a-z]*'" |
		sed 's/^.*=//'
}

sf_table () { dev "dumpsys SurfaceFlinger 2>/dev/null | grep -E '^ *[{]id='"; }

sf_id_for () { # <Гц> -> id режима 1920x1080 с этой частотой
	sf_table | awk -v r="$1" '
		{
			if (match ($0, /id=[0-9]+/)) id = substr ($0, RSTART + 3, RLENGTH - 3)
			if (match ($0, /resolution=[0-9]+x[0-9]+/)) res = substr ($0, RSTART + 11, RLENGTH - 11)
			if (match ($0, /refreshRate=[0-9.]+/)) rr = substr ($0, RSTART + 12, RLENGTH - 12)
			d = (rr + 0 > r + 0) ? (rr + 0) - (r + 0) : (r + 0) - (rr + 0)
			if (id != "" && res == "1920x1080" && d < 0.02) { print id; exit }
		}'
}

sf_release_backdoor () {
	dev 'service call SurfaceFlinger 1035 i32 -1' >/dev/null
	i=0
	while [ "$i" -lt 6 ]; do
		[ "$(sf_flag)" = false ] && return 0
		sleep 0.5
		i=$((i + 1))
	done
	say "флаг backdoor не снялся (flag=$(sf_flag))"
	return 3
}

mode_of () { dev 'cat /sys/class/display/mode'; }

# Частота выхода по vinfo (это и есть режим, на который смотрит плеер).
rate_of () {
	dev 'cat /sys/class/display/vinfo' | awk -F: '
		/^ *sync_duration_num:/ { n = $2 + 0 }
		/^ *sync_duration_den:/ { d = $2 + 0 }
		END { if (d > 0) printf "%.3f", n / d; else print "n/a" }'
}

# Признак живого декодера.
# ATV9 (.48): `frame count` не растёт (16.10: при играющем ролике `frame count : 0`
# и пустое `device name`, при остановленном плеере блок исчезает — «No vdec»),
# там играющим считаем канал с объявленной частотой > 0 и frame dur > 0.
# ATV14 (.50): счётчик растёт, зато бывает залипший канал (§18.4), поэтому рост
# счётчика — основной признак, а «fps > 0» — только если счётчиков нет вовсе.
# Сама объявленная частота ненадёжна как частота клипа (§4г) — в протоколе она
# справочная, идентификация ролика — по URL, который запускает скрипт.
dec_line () { suroot "cat /sys/class/vdec/vdec_status"; }

dec_fps () { # максимальная объявленная частота по каналам (fps) или 0
	dec_line | awk -F: '/frame rate/ { gsub (/[^0-9]/, "", $2); if ($2 + 0 > m) m = $2 + 0 }
		END { print m + 0 }'
}

dec_dur () { # максимальный frame dur (1/96000 с) или 0
	dec_line | awk -F: '/frame dur/ { gsub (/[^0-9]/, "", $2); if ($2 + 0 > m) m = $2 + 0 }
		END { print m + 0 }'
}

dec_count () { # сумма frame count по каналам
	dec_line | awk -F: '/frame count/ { gsub (/[^0-9]/, "", $2); s += $2 + 0 }
		END { print s + 0 }'
}

dec_playing () {
	c0=$(dec_count)
	sleep 2
	c1=$(dec_count)
	if [ "$c1" -gt "$c0" ] 2>/dev/null; then
		return 0
	fi
	# каналов со счётчиком нет (ATV9) — тогда по объявленной частоте
	[ "$c1" = 0 ] && [ "$(dec_fps)" -gt 0 ] 2>/dev/null && [ "$(dec_dur)" -gt 0 ] 2>/dev/null
}

daemon_of () { dev 'pidof afrd'; }

state_desc () { # краткое состояние режима (SF-поля — только где они есть)
	if [ "${SINK:-}" = sf ]; then
		echo "ядро $(mode_of), vinfo $(rate_of) Гц, SF $(sf_active_hz) Гц, backdoor=$(sf_flag)"
	else
		echo "ядро $(mode_of), vinfo $(rate_of) Гц"
	fi
}

norm_hz () { # любая частота -> ближайший класс режима
	awk -v r="$1" 'BEGIN {
		n = split ("23.976 25 29.97 50 59.94", a, " ")
		best = a [1]
		for (i = 1; i <= n; i++) {
			d = a [i] - r; if (d < 0) d = -d
			b = best - r; if (b < 0) b = -b
			if (d < b) best = a [i]
		}
		print best }'
}

hz_to_mode () { # класс частоты -> имя режима для sysfs
	case "$1" in
	23.976)		echo 1080p24hz ;;
	25)		echo 1080p25hz ;;
	29.97)		echo 1080p30hz ;;
	50)		echo 1080p50hz ;;
	59.94)		echo 1080p60hz ;;
	*)		echo "" ;;
	esac
}

cmd_mode () { # <Гц>
	r=$(norm_hz "$1")
	[ -z "$r" ] && { echo "не разобрать частоту $1" >&2; return 1; }

	if [ "${SINK:-}" = sf ]; then
		id=$(sf_id_for "$r")
		if [ -z "$id" ]; then
			say "режим $r Гц (1920x1080) не найден в таблице SurfaceFlinger"
			return 3
		fi
		dev "service call SurfaceFlinger 1035 i32 $id" >/dev/null
		i=0
		while [ "$i" -lt 10 ]; do
			sleep 0.5
			got=$(rate_of)
			if awk -v a="$got" -v b="$r" 'BEGIN { exit !(a + 0 > 0 && a - b < 0.1 && b - a < 0.1) }'; then
				return 0
			fi
			i=$((i + 1))
		done
		say "режим не применился: просили $r Гц (id $id), vinfo $got Гц, SF $(sf_active_hz) Гц"
		return 3
	fi

	m=$(hz_to_mode "$r")
	suroot "echo $m > /sys/class/display/mode" >/dev/null
	got=$(mode_of)
	if [ "$got" != "$m" ]; then
		say "режим не применился: просили $m, ядро отдаёт $got"
		return 3
	fi
}

# --- план прогона ------------------------------------------------------------
#
# Пункт: «клип | режим старта | секунда смены (0 — не менять) | целевой режим».
# Сам порядок предъявлений задаёт ORDER (номера пунктов плана).

plan_line () {
	case "$1" in
	1)	echo "LXb3EKWsInQ 60 0 60" ;;
	2)	echo "aqz-KE-bpKQ 50 8 60" ;;
	3)	echo "ydurPHaIU24 24 8 60" ;;
	4)	echo "Q7NZK6h9Tvo 60 2 50" ;;
	5)	echo "zhApeaHMvfs 60 0 60" ;;
	6)	echo "LXb3EKWsInQ 50 8 60" ;;
	*)	return 1 ;;
	esac
}

plan_expect () { # ключ: печатать только после вердиктов
	case "$1" in
	1)	echo "60.0 к/с, режим 60 Гц с самого старта — ожидание: ПЛАВНО (1:1)" ;;
	2)	echo "60.0 к/с, старт под 50 Гц, подъём до 60 Гц на 8-й с — ожидание: РЫВКИ (зажим 50 к/с на 60 Гц)" ;;
	3)	echo "29.97 к/с, старт под 24 Гц, подъём до 60 Гц на 8-й с — ожидание: РЫВКИ (зажим ~24 к/с)" ;;
	4)	echo "25.0 к/с, старт под 60 Гц, спуск до 50 Гц на 2-й с — ожидание: ПЛАВНО (ровное 2:2)" ;;
	5)	echo "23.976 к/с, режим 60 Гц всё время — ожидание: ПЛАВНО (3:2, ваш вердикт 02.10)" ;;
	6)	echo "60.0 к/с, старт под 50 Гц, подъём до 60 Гц на 8-й с — ожидание: РЫВКИ" ;;
	*)	return 1 ;;
	esac
}

plan_fps () { # номинальная частота контента (для числовой сверки)
	case "$1" in
	1|6)	echo 59.94 ;;
	2)	echo 60.000 ;;
	3)	echo 29.97 ;;
	4)	echo 25.000 ;;
	5)	echo 23.976 ;;
	*)	return 1 ;;
	esac
}

plan_title () {
	case "$1" in
	1|6)	echo "Costa Rica in 4K 60fps HDR (LXb3EKWsInQ)" ;;
	2)	echo "Big Buck Bunny 60fps 4K (aqz-KE-bpKQ)" ;;
	3)	echo "Tower: Horizontal Panning, 4K (ydurPHaIU24)" ;;
	4)	echo "Madagascar: AI Trainers / ARTE (Q7NZK6h9Tvo)" ;;
	5)	echo "JUMANJI: OPEN WORLD — трейлер (zhApeaHMvfs)" ;;
	esac
}

item_of () { echo "$ORDER" | awk -v p="$1" '{ print $p }'; }

# --- подготовка и откат ------------------------------------------------------

cmd_daemon () { # stop|start|state
	case "${1:-state}" in
	stop)
		dev "am stopservice -n $SVC" >/dev/null
		sleep 2
		p=$(daemon_of)
		if [ -n "$p" ]; then
			# Служба приложения возвращает демон за ≤15 с (§27.1), поэтому её
			# снятия недостаточно: останавливаем процесс приложения (иначе
			# сторож поднимет демон в середине фазы), затем сам демон.
			say "daemon stop: служба снята, но pid $p жив — снимаю приложение и демон"
			dev "am force-stop ru.reyter.afrd2" >/dev/null
			sleep 2
			p=$(daemon_of)
			if [ -n "$p" ]; then
				suroot "kill -9 $p" >/dev/null
				sleep 2
				p=$(daemon_of)
			fi
			if [ -n "$p" ]; then
				say "daemon stop: демон не снимается (pid $p)"
				return 3
			fi
		fi
		say "daemon stop: демона нет"
		;;
	start)
		dev "am start-foreground-service -n $SVC" >/dev/null
		sleep 5
		if [ -z "$(daemon_of)" ]; then
			# Приложение могло быть снято командой `daemon stop` (иначе сторож
			# возвращает демон в середину фазы): стартуем его активность,
			# дальше службу поднимает само приложение.
			say "daemon start: служба не поднялась — запускаю приложение"
			dev "am start -n ru.reyter.afrd2/ru.cobra.zap.afrd.gui.MainActivity" >/dev/null
			i=0
			while [ -z "$(daemon_of)" ] && [ "$i" -lt 20 ]; do
				sleep 2
				i=$((i + 2))
			done
		fi
		say "daemon start: pid $(daemon_of)"
		;;
	*)	say "daemon state: pid $(daemon_of)" ;;
	esac
}

cmd_prep () {
	need_stand
	# Исходное состояние стенда — до любой пробы: backdoor-проба снимает флаг.
	{
		echo "date=$(date '+%F %T')"
		echo "sdk=$(dev 'getprop ro.build.version.sdk')"
		echo "mode=$(mode_of)"
		echo "vinfo=$(rate_of)"
		echo "sf_active=$(sf_active_hz)"
		echo "sf_backdoor=$(sf_flag)"
		echo "content_detection=$(dev 'getprop ro.surface_flinger.use_content_detection_for_refresh_rate')"
		echo "frame_rate_override=$(dev 'getprop ro.surface_flinger.enable_frame_rate_override')"
		echo "match_content=$(dev 'settings get secure match_content_frame_rate')"
	} > "$PRE_FILE"

	save_sink || return 3
	say "prep: путь смены режима — $SINK (sdk $(dev 'getprop ro.build.version.sdk'))"

	# Штатный AFR SurfaceFlinger на ATV14 должен быть выключен, иначе режим
	# перебивает фреймворк, а не скрипт (T1/T2, §4д).
	if grep -q '^content_detection=true' "$PRE_FILE" ||
	   grep -q '^frame_rate_override=true' "$PRE_FILE" ||
	   ! grep -q '^match_content=0$' "$PRE_FILE"; then
		say "ВНИМАНИЕ: штатный AFR не выключен — числовой вердикт будет смешан:"
		grep -E '^(content_detection|frame_rate_override|match_content)=' "$PRE_FILE" | sed 's/^/    /'
	fi

	if [ -f "$DIR/afrd.ini.orig" ]; then
		say "prep: отметка о подготовке уже есть"
	else
		suroot "cat /data/user/0/ru.reyter.afrd2/cache/afrd.ini" > "$DIR/afrd.ini.orig"
		say "prep: afrd.ini сохранён на всякий случай ($DIR/afrd.ini.orig)"
	fi
	cmd_daemon stop || return 3
	dev "am force-stop $PKG" >/dev/null
	[ "$SECS" -lt 16 ] && say "ВНИМАНИЕ: SECS=$SECS — смена режима на 8-й секунде не покроется замером (нужно ≥ 16)"
	cmd_mode "$BASE_HZ" || return 3
	say "prep: режим $BASE_HZ Гц ($(state_desc)), плеер остановлен"
	echo
	echo "  Порядок прогона (условия владельцу не сообщать):"
	P=1
	for it in $ORDER; do
		echo "    предъявление $P — $(plan_title "$it")"
		P=$((P + 1))
	done
	echo
	echo "  Дальше по одному: run <номер> → вердикт владельца → mark <номер> smooth|jerky"
	echo "  Реклама перед роликом → mark <номер> ad (предъявление повторяем)."
}

cmd_rollback () {
	need_stand
	dev "am force-stop $PKG" >/dev/null
	load_sink || { save_sink || true; }

	# Возвращаем исходный режим из пред-состояния (а не BASE_HZ): это состояние
	# владельца, каким скрипт его застал.
	pre_rate=$(sed -n 's/^vinfo=//p' "$PRE_FILE" 2>/dev/null)
	if [ -n "$pre_rate" ] && [ "$pre_rate" != n/a ]; then
		cmd_mode "$pre_rate" >/dev/null || cmd_mode "$BASE_HZ" >/dev/null || true
	else
		cmd_mode "$BASE_HZ" >/dev/null || true
	fi
	# Порядок важен: сначала вернуть режим, потом снять флаг (§4е).
	if [ "$SINK" = sf ]; then
		sf_release_backdoor || true
	fi
	cmd_daemon start || true
	say "rollback: $(state_desc), демон pid $(daemon_of)"
	echo "  проверьте: su -c 'grep -E \"^(enable|log.enable|switch.skip.pkg)\" \
	    /data/user/0/ru.reyter.afrd2/cache/afrd.ini'"
}

# --- предъявление ------------------------------------------------------------

video_layer () {
	L=$(dev "dumpsys SurfaceFlinger --list 2>/dev/null | grep -E 'SurfaceView\\[' | grep -i blast | head -1")
	[ -z "$L" ] && L=$(dev "dumpsys SurfaceFlinger --list 2>/dev/null | grep -E 'SurfaceView\\[' | head -1")
	echo "$L"
}

capture_lat () { # <файл> — дамп подачи видео-слоя + строка состояния
	L=$(video_layer)
	{
		echo "mode=$(mode_of) vinfo=$(rate_of) sf=$(sf_active_hz)"
		if [ -z "$L" ]; then
			echo "layer=(нет)"
		else
			echo "layer=$L"
			dev "dumpsys SurfaceFlinger --latency '$L'"
		fi
	} > "$1"
	if [ -z "$L" ]; then
		say "lat: видео-слой не найден — числовой опоры для этого замера нет"
		return 1
	fi
	return 0
}

wait_playing () { # ждём появления живого декодера, до 40 с
	i=0
	while [ "$i" -lt 40 ]; do
		sleep 2
		i=$((i + 2))
		if dec_playing; then
			say "воспроизведение идёт (t+${i}s, декодер: $(dec_fps) к/с, frame dur $(dec_dur), кадров $(dec_count))"
			return 0
		fi
	done
	say "декодера нет 40 с — плеер не начал играть (реклама? каталог? сеть?)"
	return 1
}

cmd_run () { # <номер предъявления>
	need_stand
	load_sink || return 3
	P=${1:-}
	item=$(item_of "$P")
	if [ -z "$item" ]; then
		echo "usage: $0 run <1..$(echo "$ORDER" | wc -w)>" >&2
		return 1
	fi
	set -- $(plan_line "$item")
	id=$1
	start=$2
	at=$3
	target=$4
	rm -f "$LATDIR/P$P".pre "$LATDIR/P$P".post

	pid=$(daemon_of)
	if [ -n "$pid" ]; then
		say "предъявление $P: демон жив (pid $pid) — снимаю"
		cmd_daemon stop || return 3
	fi

	dev "am force-stop $PKG" >/dev/null
	cmd_mode "$start" || return 3
	say "ПРЕДЪЯВЛЕНИЕ $P: старт под $start Гц ($(state_desc))"
	dev "am start -a android.intent.action.VIEW -d 'https://www.youtube.com/watch?v=$id'" | head -2
	say "ПРЕДЪЯВЛЕНИЕ $P: запущен https://www.youtube.com/watch?v=$id"

	wait_playing || { say "ПРЕДЪЯВЛЕНИЕ $P: не начало играть — предъявление повторить"; return 3; }

	# Числовые срезы: до смены режима и за 5 с до конца. Считаются только в
	# `report` — во время прогона их не печатаем, иначе слепота ломается.
	if [ "$at" != 0 ]; then
		pre_at=$((at - 2)); [ "$pre_at" -lt 1 ] && pre_at=1
	else
		pre_at=5
	fi
	post_at=$((SECS - 5))
	if [ "$at" != 0 ] && [ "$post_at" -le "$at" ]; then
		post_at=$((at + 3))
		say "П$P: SECS=$SECS маловат — срез «после» смещён на t+$post_at"
		if [ "$post_at" -ge "$SECS" ]; then
			say "П$P: SECS=$SECS не покрывает смену режима (нужно ≥ $((at + 8))) — прогон остановлен"
			return 1
		fi
	fi

	elapsed=0
	switched=0
	while [ "$elapsed" -lt "$SECS" ]; do
		m=$(mode_of)
		r=$(rate_of)
		df=$(dec_fps)
		dd=$(dec_dur)
		say "П$P t+${elapsed}s режим=$m vinfo=${r}Гц декодер=${df}к/с dur=$dd"
		# Срез «до смены» обязан сниматься ДО переключения (при at=2 иначе
		# попал бы уже новый режим).
		[ "$elapsed" = "$pre_at" ] && capture_lat "$LATDIR/P$P.pre" >/dev/null
		if [ "$at" != 0 ] && [ "$switched" = 0 ] && [ "$elapsed" -ge "$at" ]; then
			if cmd_mode "$target"; then
				say "П$P t+${elapsed}s: СМЕНА РЕЖИМА на $target Гц ($(state_desc))"
				switched=1
			else
				say "П$P t+${elapsed}s: СМЕНА РЕЖИМА НЕ УДАЛАСЬ — предъявление повторить"
				dev "am force-stop $PKG" >/dev/null
				return 3
			fi
		fi
		[ "$elapsed" = "$post_at" ] && capture_lat "$LATDIR/P$P.post" >/dev/null
		sleep 1
		elapsed=$((elapsed + 1))
	done

	say "КОНЕЦ ПРЕДЪЯВЛЕНИЯ $P ($(state_desc))"
	[ "$at" != 0 ] && [ "$switched" = 0 ] && say "П$P: смена режима так и не выполнилась — предъявление недействительно"
	echo
	echo "  Спросите владельца: пункт $P — плавно или рывки? Затем:"
	echo "    sh $0 mark $P smooth|jerky|ad"
}

cmd_mark () {
	case "${2:-}" in
	smooth|jerky|ad) ;;
	*) echo "usage: $0 mark <номер> smooth|jerky|ad" >&2; return 1 ;;
	esac
	say "ВЕРДИКТ $1: $2 (режим $(mode_of), vinfo $(rate_of) Гц)"
}

# --- отчёты ------------------------------------------------------------------

cmd_report () {
	echo "=== вердикты владельца ==="
	[ -f "$EV" ] && grep 'ВЕРДИКТ' "$EV" || echo "(нет)"
	echo
	echo "=== что делал скрипт (по предъявлениям) ==="
	awk '/ПРЕДЪЯВЛЕНИЕ/ || /СМЕНА РЕЖИМА/ || /КОНЕЦ ПРЕДЪЯВЛЕНИЯ/ || /воспроизведение идёт/ || /не начало играть/ {
		print
	}' "$EV" 2>/dev/null
	echo
	echo "=== частота по декодеру (объявленная плеером) ==="
	awk '
		/ПРЕДЪЯВЛЕНИЕ [0-9]+: старт/ {
			p = $0; sub (/^.*ПРЕДЪЯВЛЕНИЕ /, "", p); sub (/:.*/, "", p); delete c
		}
		/ t\+[0-9]+s / {
			v = $0; sub (/^.*декодер=/, "", v); sub (/к\/с.*/, "", v)
			if (v != "") c [v]++
		}
		/КОНЕЦ ПРЕДЪЯВЛЕНИЯ/ {
			s = ""
			for (k in c) s = s sprintf (" %s к/с (%d замеров)", k, c [k])
			printf "  предъявление %s:%s\n", p, (s == "" ? " н/д" : s)
		}
	' "$EV" 2>/dev/null
	echo "  (это частота, которую объявляет плеер по SPS — то есть какой ролик играет:"
	echo "   Costa Rica 60, Big Buck Bunny 60, ARTE 25, Tower 30, JUMANJI 24)"
	echo
	echo "=== условия, как их выполнил скрипт ==="
	awk '/ПРЕДЪЯВЛЕНИЕ [0-9]+: старт/ || / t\+[0-9]+s: СМЕНА РЕЖИМА/ {
		sub (/^[0-9:]+ /, "")
		print "  " $0
	}' "$EV" 2>/dev/null
	echo
	echo "=== числовая опора: подача видео-слоя (SurfaceFlinger --latency) ==="
	echo "  окно — последние ≤128 кадров слоя; «зажим» = измеренная частота ниже"
	echo "  номинала контента (плеер не поднял темп после смены режима)."
	N=$(echo "$ORDER" | wc -w)
	P=1
	while [ "$P" -le "$N" ]; do
		item=$(item_of "$P")
		nom=$(plan_fps "$item")
		at=$(plan_line "$item" | awk '{ print $3 }')
		for ph in pre post; do
			f="$LATDIR/P$P.$ph"
			[ -f "$f" ] || continue
			meta=$(sed -n '1s/^mode=\([^ ]*\) vinfo=\([^ ]*\) sf=\(.*\)$/\1 vinfo=\2Гц SF=\3/p' "$f")
			awk -v p="$P" -v ph="$ph" -v nom="$nom" -v meta="$meta" -v at="$at" '
			NR > 1 && NF == 3 && $2 + 0 > 0 && $2 + 0 < 1e17 { n++; t[n] = $2 + 0 }
			END {
				if (n < 3) {
					printf "  П%s %-4s %s: нет данных (n=%d)\n", p, ph, meta, n
					exit
				}
				m = 0; sum = 0; max = 0
				for (i = 2; i <= n; i++) {
					d = (t[i] - t[i-1]) / 1e6
					if (d <= 0) continue
					m++; sum += d
					if (d > max) max = d
				}
				if (m < 2) {
					printf "  П%s %-4s %s: нет данных (n=%d)\n", p, ph, meta, n
					exit
				}
				avg = sum / m
				hang = 0
				for (i = 2; i <= n; i++) {
					d = (t[i] - t[i-1]) / 1e6
					if (d > 1.35 * avg) hang++
				}
				fps = 1000.0 / avg
				rel = (nom > 0) ? fps / nom : 0
				if (ph == "pre" && at + 0 > 0) v = "(до смены)"
				else if (nom > 0 && rel < 0.95) v = "!! НИЖЕ КОНТЕНТА (зажим?)"
				else if (hang > 0) v = "удержания"
				else v = "ровно"
				printf "  П%s %-4s %s: n=%d avg=%.2fмс max=%.2fмс => %.2f к/с (контент %.3f), удержаний %d %s\n",
				       p, ph, meta, n, avg, max, fps, nom, hang, v
			}' "$f"
		done
		P=$((P + 1))
	done
	echo
	echo "=== режим экрана по секундам (ключ к вердиктам) ==="
	grep -E ' t\+[0-9]+s режим=' "$EV" 2>/dev/null | sed 's/^\([0-9:]*\) .*\(П[0-9]*\) t+\([0-9]*\)s режим=\([^ ]*\) vinfo=\([^ ]*\)Гц.*/\1 \2 t+\3s \4 (\5 Гц)/'
	echo
	echo "=== журнал демона (последние строки) ==="
	suroot "tail -n 5 /data/local/afrd.log" 2>/dev/null
	echo "  (журнал во время прогона не пишется: демон снят)"
}

cmd_key () {
	echo "=== условия и ожидания (ключ; читать после вердиктов) ==="
	P=1
	for it in $ORDER; do
		printf "  предъявление %d — %s\n" "$P" "$(plan_title "$it")"
		printf "     условия: %s\n" "$(plan_line "$it")"
		printf "     ожидание: %s\n" "$(plan_expect "$it" | sed 's/^ожидание: //')"
		P=$((P + 1))
	done
	echo
	echo "  Один и тот же клип предъявляется дважды с разными условиями:"
	P=1
	for it in $ORDER; do
		pid_i=$(echo "$(plan_line "$it")" | awk '{ print $1 }')
		printf "     %s — предъявление %d\n" "$pid_i" "$P"
		P=$((P + 1))
	done | sort | awk '{ c [$1] = c [$1] " " $4 } END { for (k in c) if (split (c [k], s, " ") > 1) printf "     %s:%s\n", k, c [k] }'
	echo "  Как читать: совпало во всех предъявлениях — зажим виден глазом и слепота"
	echo "  соблюдена. Расхождение вердиктов у одного и того же клипа (разные условия)"
	echo "  — оценка опирается на подачу, а не на содержимое. Всё «плавно» там, где"
	echo "  ожидались рывки, — гипотеза зажима на ATV9 не подтверждается глазом (тогда"
	echo "  пересматриваем и сам чёрный список). Рывки ровно вдвое (как 15 к/с вместо"
	echo "  30) — это не зажим, а дисплейная половинная подача (§21.9)."
}

case "${1:-}" in
status)		need_stand
		load_sink || save_sink || true
		echo "sdk $(dev 'getprop ro.build.version.sdk'), путь ${SINK:-?}"
		cmd_daemon state
		echo "режим: $(state_desc)"
		echo "плеер $(dev "pidof $PKG"), пред-состояние: $(cat "$PRE_FILE" 2>/dev/null | tr '\n' ' ')" ;;
prep)		cmd_prep ;;
rollback)	cmd_rollback ;;
run)		shift; cmd_run "$@" ;;
mark)		shift; cmd_mark "$@" ;;
report)		cmd_report ;;
key)		cmd_key ;;
items)		P=1; for it in $ORDER; do echo "$P: $(plan_title "$it")"; P=$((P + 1)); done ;;
*)
	cat >&2 <<EOF
usage: $0 <команда>

  prep                   снять демон, записать пред-состояние, поставить базовый режим
  run <1..N>             выполнить предъявление N (условия задаёт план)
  mark <N> smooth|jerky|ad   вердикт владельца по предъявлению N
  report                 вердикты, что делал скрипт, числовая опора по подаче
  key                    условия и ожидания — ЧИТАТЬ ПОСЛЕ ВЕРДИКТОВ
  items                  порядок предъявлений (без условий)
  status                 снимок стенда
  rollback               вернуть режим пред-состояния, снять backdoor, поднять демон

Путь смены режима выбирается сам: Android ≤9 — sysfs, Android 12-14 —
транзакция SurfaceFlinger 1035; Android 10/11 не поддержан.

Переменные: D=$D  BASE_HZ=$BASE_HZ  SECS=$SECS  YTBLIND_DIR=$DIR
            YTBLIND_SINK=sysfs|sf (принудительно), YTBLIND_ORDER="$ORDER"
EOF
	exit 1
	;;
esac
