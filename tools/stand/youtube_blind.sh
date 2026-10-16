#!/bin/sh
# AFRd2: слепой прогон «зажима FPS» в штатном YouTube TV (стенд ATV9, `.76`).
#
# Вопрос прогона: зависит ли плавность ролика от того, ПОД КАКИМ режимом экрана
# ролик стартовал (гипотеза §28.9: плеер выбирает темп отрисовки в момент старта
# как min(частота ролика, частота экрана) и потом его не поднимает). На `.76`
# числовых опор по подаче кадров нет (§30.13: `SurfaceFlinger --latency` по
# видео-слою не обновляется), поэтому вердикт даёт глаз владельца.
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
#   D=192.168.88.76:5555 sh tools/stand/youtube_blind.sh prep
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
ORDER=${YTBLIND_ORDER:-5 1 3 4 6 2}

PKG=com.google.android.youtube.tv
SVC=ru.reyter.afrd2/ru.cobra.zap.afrd.gui.AFRService

EV="$DIR/events.txt"
mkdir -p "$DIR" || exit 1

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

mode_of () { dev 'cat /sys/class/display/mode'; }

# Частота выхода по vinfo (это и есть режим, на который смотрит плеер).
rate_of () {
	dev 'cat /sys/class/display/vinfo' | awk -F: '
		/^ *sync_duration_num:/ { n = $2 + 0 }
		/^ *sync_duration_den:/ { d = $2 + 0 }
		END { if (d > 0) printf "%.3f", n / d; else print "n/a" }'
}

# Счётчик декодированных кадров: по нему видно, идёт ли воспроизведение и с
# какой частотой плеер реально подаёт кадры (не путать с подачей на экран).
frames_of () { suroot "grep -m1 'frame count' /sys/class/vdec/vdec_status" | tr -dc '0-9'; }

daemon_of () { dev 'pidof afrd'; }

hz_to_mode () {
	case "$1" in
	23.98|23.976|24)	echo 1080p24hz ;;
	25)			echo 1080p25hz ;;
	29.97|30)		echo 1080p30hz ;;
	50)			echo 1080p50hz ;;
	59.94|60)		echo 1080p60hz ;;
	*)			echo "" ;;
	esac
}

cmd_mode () { # <Гц>
	m=$(hz_to_mode "$1")
	if [ -z "$m" ]; then
		echo "не знаю режим для $1 Гц (24/25/30/50/60)" >&2
		return 1
	fi
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
	3)	echo "QPGnVP1PiMo 24 8 60" ;;
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
	3)	echo "30.0 к/с, старт под 24 Гц, подъём до 60 Гц на 8-й с — ожидание: РЫВКИ (зажим ~24 к/с)" ;;
	4)	echo "25.0 к/с, старт под 60 Гц, спуск до 50 Гц на 2-й с — ожидание: ПЛАВНО (ровное 2:2)" ;;
	5)	echo "23.976 к/с, режим 60 Гц всё время — ожидание: ПЛАВНО (3:2, ваш вердикт 02.10)" ;;
	6)	echo "60.0 к/с, старт под 50 Гц, подъём до 60 Гц на 8-й с — ожидание: РЫВКИ" ;;
	*)	return 1 ;;
	esac
}

plan_title () {
	case "$1" in
	1|6)	echo "Costa Rica in 4K 60fps HDR (LXb3EKWsInQ)" ;;
	2)	echo "Big Buck Bunny 60fps 4K (aqz-KE-bpKQ)" ;;
	3)	echo "China Isn't Catching Up (QPGnVP1PiMo)" ;;
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
		sleep 3
		p=$(daemon_of)
		if [ -n "$p" ]; then
			say "daemon stop: служба снята, но pid $p жив (сторож вернул демон?)"
			return 3
		fi
		say "daemon stop: демона нет"
		;;
	start)
		dev "am start-foreground-service -n $SVC" >/dev/null
		sleep 5
		say "daemon start: pid $(daemon_of)"
		;;
	*)	say "daemon state: pid $(daemon_of)" ;;
	esac
}

cmd_prep () {
	need_stand
	if [ -f "$DIR/afrd.ini.orig" ]; then
		say "prep: отметка о подготовке уже есть"
	else
		suroot "cat /data/user/0/ru.reyter.afrd2/cache/afrd.ini" > "$DIR/afrd.ini.orig"
		say "prep: afrd.ini сохранён на всякий случай ($DIR/afrd.ini.orig)"
	fi
	cmd_daemon stop || return 3
	cmd_mode "$BASE_HZ" >/dev/null || true
	dev "am force-stop $PKG" >/dev/null
	say "prep: режим $BASE_HZ Гц (ядро $(mode_of), vinfo $(rate_of) Гц), плеер остановлен"
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
	cmd_daemon start || true
	cmd_mode "$BASE_HZ" >/dev/null || true
	say "rollback: режим $BASE_HZ Гц (ядро $(mode_of), vinfo $(rate_of) Гц), демон pid $(daemon_of)"
	echo "  проверьте: su -c 'grep -E \"^(enable|log.enable|switch.skip.pkg)\" \
	    /data/user/0/ru.reyter.afrd2/cache/afrd.ini'"
}

# --- предъявление ------------------------------------------------------------

wait_playing () { # ждём роста счётчика кадров, до 40 с
	i=0
	prev=$(frames_of)
	[ -z "$prev" ] && prev=0
	while [ "$i" -lt 40 ]; do
		sleep 2
		i=$((i + 2))
		cur=$(frames_of)
		[ -z "$cur" ] && cur=0
		if [ "$cur" -gt "$prev" ]; then
			say "воспроизведение идёт (t+${i}s, декодировано $cur кадров)"
			return 0
		fi
		prev=$cur
	done
	say "кадры не растут 40 с — плеер не начал играть (реклама? каталог? сеть?)"
	return 1
}

cmd_run () { # <номер предъявления>
	need_stand
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

	pid=$(daemon_of)
	if [ -n "$pid" ]; then
		say "предъявление $P: демон жив (pid $pid) — снимаю"
		cmd_daemon stop || return 3
	fi

	dev "am force-stop $PKG" >/dev/null
	cmd_mode "$start" || return 3
	say "ПРЕДЪЯВЛЕНИЕ $P: старт под $start Гц (ядро $(mode_of), vinfo $(rate_of) Гц)"
	dev "am start -a android.intent.action.VIEW -d 'https://www.youtube.com/watch?v=$id'" | head -2
	say "ПРЕДЪЯВЛЕНИЕ $P: запущен https://www.youtube.com/watch?v=$id"

	wait_playing || { say "ПРЕДЪЯВЛЕНИЕ $P: не начало играть — предъявление повторить"; return 3; }

	elapsed=0
	switched=0
	while [ "$elapsed" -lt "$SECS" ]; do
		m=$(mode_of)
		r=$(rate_of)
		f=$(frames_of)
		say "П$P t+${elapsed}s режим=$m vinfo=${r}Гц кадров=$f"
		if [ "$at" != 0 ] && [ "$switched" = 0 ] && [ "$elapsed" -ge "$at" ]; then
			cmd_mode "$target" >/dev/null
			say "П$P t+${elapsed}s: СМЕНА РЕЖИМА на $target Гц (ядро $(mode_of), vinfo $(rate_of) Гц)"
			switched=1
		fi
		sleep 1
		elapsed=$((elapsed + 1))
	done

	say "КОНЕЦ ПРЕДЪЯВЛЕНИЯ $P (режим $(mode_of), vinfo $(rate_of) Гц)"
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
	echo "=== частота декодера по предъявлениям (кадров/с) ==="
	awk '
		/ПРЕДЪЯВЛЕНИЕ [0-9]+: старт/ {
			p = $0; sub (/^.*ПРЕДЪЯВЛЕНИЕ /, "", p); sub (/:.*/, "", p)
			first = 0; last = 0; t0 = 0; t1 = 0
		}
		/ t\+[0-9]+s / {
			v = $0; sub (/^.* t\+/, "", v); sub (/s .*/, "", v); t = v + 0
			v = $0; sub (/^.*кадров=/, "", v); sub (/[^0-9].*/, "", v); f = v + 0
			if (first == 0) { first = f; t0 = t }
			last = f; t1 = t
		}
		/КОНЕЦ ПРЕДЪЯВЛЕНИЯ/ {
			if (last > first && t1 > t0)
				printf "  предъявление %s: %.2f к/с (за %d с)\n", p, (last - first) / (t1 - t0), t1 - t0
		}
	' "$EV" 2>/dev/null
	echo "  (для справки: 60-к/с ролик должен дать ~60, 30-к/с ~30, 25-к/с ~25, 23.976 ~24)"
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
		printf "     ожидание: %s\n" "$(plan_expect "$it")"
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
status)		need_stand; cmd_daemon state; echo "режим $(mode_of), vinfo $(rate_of) Гц, плеер $(dev "pidof $PKG")" ;;
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

  prep                   снять демон, поставить базовый режим, показать порядок
  run <1..N>             выполнить предъявление N (условия задаёт план)
  mark <N> smooth|jerky|ad   вердикт владельца по предъявлению N
  report                 вердикты, что делал скрипт, частота декодера
  key                    условия и ожидания — ЧИТАТЬ ПОСЛЕ ВЕРДИКТОВ
  items                  порядок предъявлений (без условий)
  status                 снимок стенда
  rollback               поднять демон и вернуть режим $BASE_HZ Гц

Переменные: D=$D  BASE_HZ=$BASE_HZ  SECS=$SECS  YTBLIND_DIR=$DIR
EOF
	exit 1
	;;
esac
