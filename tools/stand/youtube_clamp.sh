#!/bin/sh
# AFRd2: прогон «зажима FPS» в штатном YouTube глазами (стенд ATV9, старый путь).
#
# Зачем отдельный скрипт. На `.76` (Android 9, ядро 4.9) числовых опор по подаче
# кадров нет (§30.13: `SurfaceFlinger --latency` по видео-слою не обновляется,
# счётчиков показанных кадров нет) — диагноза «подача неровная» из ATV14-цикла
# там тоже нет. Значит вердикт «зажим есть / зажима нет» будет субъективным
# (глаза владельца), и ценность прогона — в точной записи условий: какая была
# фаза, какой режим, что делал демон. Скрипт держит сценарий A/B/C и пишет
# протокол, чтобы вердикт можно было сопоставить с условиями, а не вспоминать.
#
# Сценарий (§34.10):
#   A (контроль)  — 60 Гц, запустить ролик. Ожидание: плавно.
#   B (зажим)     — 24 Гц, запустить ролик, затем поднять до 60 Гц. При зажиме
#                   ожидание: дёргано, хотя режим уже 60 Гц.
#   C (лечение)   — перезапустить ролик при 60 Гц. Ожидание: плавно.
#   D (реальный)  — демон живой, YouTube не в списке: 24-фпс ролик (режим 24 Гц),
#                   затем 60-фпс ролик — демон переключает сам. Вердикт: дёргано ли
#                   ВТОРОЕ видео уже при 60 Гц (жалоба владельца целиком).
#
# Запуск (порядок для окна с владельцем у ТВ):
#   sh tools/stand/youtube_clamp.sh status
#   sh tools/stand/youtube_clamp.sh prep            # журнал вкл, YouTube из чёрного списка
#   sh tools/stand/youtube_clamp.sh run A
#   sh tools/stand/youtube_clamp.sh mark A smooth   # вердикт владельца: smooth | jerky
#   sh tools/stand/youtube_clamp.sh run B ; ... mark B jerky
#   sh tools/stand/youtube_clamp.sh run C ; ... mark C smooth
#   sh tools/stand/youtube_clamp.sh report
#   sh tools/stand/youtube_clamp.sh rollback        # вернуть ini и режим 1080p50hz
#
# Переменные:
#   D (или ADB_DEV) — адрес adb-устройства, по умолчанию 192.168.88.76:5555;
#   URL             — ролик (по умолчанию 30-фпс ролик задания §34.10);
#   URL2            — второй ролик для пары «30 и 60 fps» (фазы C и D);
#   URL24           — 24-фпс ролик для фазы D (демон на нём удержит 24 Гц);
#   BASE_HZ         — режим, в который возвращает rollback (по умолчанию 50);
#   YTCLAMP_DIR     — каталог протокола на хосте (по умолчанию /tmp/ytclamp);
#   INI             — путь к afrd.ini на устройстве.
#
# Про root: на `.76` `adb shell` — это uid 2000 (не root), а узлы и ini требуют
# root, поэтому чтения/записи идут через `su -c`. Демон стартует приложением и
# живёт как root; ini перечитывается им самим (reconf), перезапуск не нужен.
#
# Код возврата: 0 — команда выполнена, 1 — ошибка ввода, 2 — стенд недоступен,
#               3 — команда выполнена, но проверка состояния не сошлась.

set -u

D=${ADB_DEV:-${D:-192.168.88.76:5555}}
URL=${URL:-https://www.youtube.com/watch?v=aqz-KE-bpKQ}
URL2=${URL2:-}
URL24=${URL24:-}
BASE_HZ=${BASE_HZ:-50}
DIR=${YTCLAMP_DIR:-/tmp/ytclamp}
INI=${INI:-/data/user/0/ru.reyter.afrd2/cache/afrd.ini}
PKG=com.google.android.youtube.tv
LOG=/data/local/afrd.log

EV="$DIR/events.txt"
mkdir -p "$DIR" || exit 1

say () { echo "$@"; echo "$(date '+%H:%M:%S') $*" >> "$EV"; }

dev () { # обычная оболочка (uid 2000)
	adb -s "$D" shell "$@" 2>/dev/null | tr -d '\r'
}

suroot () { # одна команда под su
	adb -s "$D" shell "su -c \"$1\"" 2>/dev/null | tr -d '\r'
}

need_stand () {
	if ! adb -s "$D" get-state >/dev/null 2>&1; then
		echo "стенд $D недоступен (adb get-state)" >&2
		exit 2
	fi
}

mode_of () { dev 'cat /sys/class/display/mode'; }

rate_of () { # фактическая частота по vinfo (старый путь), Гц
	dev 'cat /sys/class/display/vinfo' | awk -F: '
		/^ *sync_duration_num:/ { n = $2 + 0 }
		/^ *sync_duration_den:/ { d = $2 + 0 }
		END { if (d > 0) printf "%.3f", n / d; else print "n/a" }'
}

hdmi_of () { dev 'cat /sys/class/switch/hdmi/cable.0/state'; }

daemon_of () { dev 'pidof afrd'; }

# --- режим ------------------------------------------------------------------

hz_to_mode () { # <Гц> -> имя режима ядра для 1080p
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
	gotrate=$(rate_of)
	say "mode $1 -> $m | ядро: $got | vinfo: ${gotrate} Гц | hdmi=$(hdmi_of)"
	if [ "$got" != "$m" ]; then
		echo "  режим не применился (просили $m, ядро отдаёт $got)" >&2
		return 3
	fi
}

cmd_status () {
	need_stand
	echo "стенд:        $D"
	echo "модель:       $(dev 'getprop ro.product.model'), Android $(dev 'getprop ro.build.version.release'), ядро $(dev 'uname -r')"
	echo "режим ядра:   $(mode_of)   vinfo: $(rate_of) Гц   HDMI=$(hdmi_of)"
	echo "демон afrd:   pid $(daemon_of)"
	echo "приложение:   $(dev "dumpsys package ru.reyter.afrd2 | grep -m1 versionName" | tr -d ' ')"
	echo "upstream:     $(dev "dumpsys package ru.cobra.zap.afrd | grep -m1 versionName" | tr -d ' ') (должен быть отключён)"
	echo "YouTube:      $(dev "dumpsys package $PKG | grep -m1 versionName" | tr -d ' ')"
	echo "конфиг:       $(suroot "grep -c . $INI >/dev/null && echo $INI")"
	suroot "grep -E '^(enable|log.enable|switch.skip.pkg|switch.delay.off|switch.blackout)' $INI" | sed 's/^/              /'
	echo "последний лог: $(suroot "ls -l $LOG" | awk '{print $6, $7, $8, $9}')"
	echo "снимок тракта: $(sh "$(dirname "$0")/check_mode.sh" 2>/dev/null | tail -1)"
}

# --- подготовка и откат ------------------------------------------------------

cmd_prep () {
	need_stand
	if [ -f "$DIR/afrd.ini.orig" ]; then
		say "prep: исходный ini уже сохранён ($DIR/afrd.ini.orig) — второй раз не сохраняю"
	else
		suroot "cat $INI" > "$DIR/afrd.ini.orig"
		if [ ! -s "$DIR/afrd.ini.orig" ]; then
			echo "не удалось прочитать $INI под root" >&2
			return 2
		fi
		say "prep: исходный ini сохранён в $DIR/afrd.ini.orig ($(wc -l < "$DIR/afrd.ini.orig") строк)"
	fi

	# log.enable=1 и пустой чёрный список плееров: заменяем строки в копии и
	# кладём её обратно. Ключа switch.skip.pkg в этом профиле может не быть —
	# тогда дописываем пустой (пустое значение = ничего не пропускать).
	sed -e 's/^log\.enable=.*/log.enable=1/' "$DIR/afrd.ini.orig" > "$DIR/afrd.ini.new"
	if grep -q '^switch\.skip\.pkg=' "$DIR/afrd.ini.new"; then
		sed -i 's/^switch\.skip\.pkg=.*/switch.skip.pkg=/' "$DIR/afrd.ini.new"
	else
		printf '\n# временно на прогон §34.10: YouTube не пропускать\nswitch.skip.pkg=\n' >> "$DIR/afrd.ini.new"
	fi

	# ini принадлежит приложению (u0_a108:u0_a108_cache, 0600) — кладём под root,
	# владельца и права сохраняем как были.
	suroot "cp $INI $INI.clamp.bak"
	adb -s "$D" push "$DIR/afrd.ini.new" /data/local/tmp/afrd.ini.clamp >/dev/null 2>&1
	suroot "cp /data/local/tmp/afrd.ini.clamp $INI && chmod 600 $INI && chown u0_a108:u0_a108_cache $INI"

	got=$(suroot "grep -E '^(log.enable|switch.skip.pkg)' $INI" | tr '\n' ' ')
	say "prep: конфиг на прогон — $got"
	echo "  журнал:  $LOG (перечитывается демоном сам)"
	echo "  список:  пустой (YouTube больше не пропускается)"
	echo "  откат:   sh $0 rollback"
	echo
	echo "  Проверьте у владельца перед прогоном:"
	echo "   * у штатного YouTube TV ВЫКЛЮЧЕН свой автоподбор частоты — иначе он"
	echo "     переключает режим сам, и вердикт будет не про нашу гипотезу (§34.10);"
	echo "   * ролик задан так, чтобы частота была известна: URL=$URL"
	echo "     (для пары «30 и 60 fps» — второй ролик через URL2=...)."
}

cmd_rollback () {
	need_stand
	if [ -f "$DIR/afrd.ini.orig" ]; then
		adb -s "$D" push "$DIR/afrd.ini.orig" /data/local/tmp/afrd.ini.orig >/dev/null 2>&1
		suroot "cp /data/local/tmp/afrd.ini.orig $INI && chmod 600 $INI && chown u0_a108:u0_a108_cache $INI && rm -f /data/local/tmp/afrd.ini.clamp /data/local/tmp/afrd.ini.orig"
		say "rollback: ini возвращён из $DIR/afrd.ini.orig"
	else
		say "rollback: исходного ini нет — конфиг не трогаю"
	fi
	cmd_mode "$BASE_HZ" >/dev/null || true
	say "rollback: режим $BASE_HZ Гц (ядро: $(mode_of), vinfo $(rate_of) Гц)"
	echo "  проверьте конфиг: su -c 'grep -E \"^(log.enable|switch.skip.pkg)\" $INI'"
}

# --- фазы --------------------------------------------------------------------

cmd_play () { # [url]
	u=${1:-$URL}
	dev "am start -a android.intent.action.VIEW -d '$u'" | head -2
	say "play $u"
}

cmd_stop () {
	dev "am force-stop $PKG" >/dev/null
	say "stop $PKG"
}

# Демон на `.76` поднимается службой приложения сама (проверено §27.1: убитый
# демон возвращается за ≤15 с), поэтому «остановить демон» = остановить службу,
# иначе она вернёт его в середину фазы. Обратно — start-foreground-service
# (обычный startservice от root на этом боксе отвергается, см. §27.1).
SVC=ru.reyter.afrd2/ru.cobra.zap.afrd.gui.AFRService

cmd_daemon () { # stop|start|state
	case "${1:-state}" in
	stop)
		dev "am stopservice -n $SVC" >/dev/null
		sleep 2
		p=$(daemon_of)
		if [ -n "$p" ]; then
			say "daemon stop: служба снята, но pid $p ещё жив (сторож?)"
			return 3
		fi
		say "daemon stop: демона нет (pid пуст)"
		;;
	start)
		dev "am start-foreground-service -n $SVC" >/dev/null
		sleep 5
		say "daemon start: pid $(daemon_of)"
		;;
	*)	say "daemon state: pid $(daemon_of)" ;;
	esac
}

cmd_run () { # A|B|C
	need_stand
	case "$1" in
	A)	cmd_daemon start
		cmd_mode 60
		cmd_play "$URL"
		;;
	B)	# Проверка зажима в чистом виде: демон снят, иначе он вернёт режим
		# своим (то же требование, что в ab_rate.sh). Сначала 24 Гц, ролик
		# стартует «зажатым», затем режим поднимаем руками — если картинка
		# остаётся дёрганой, темп зафиксировал плеер, а не дисплей.
		cmd_daemon stop
		cmd_mode 24
		cmd_play "$URL"
		sleep 15
		say "B: режим до подъёма — ядро $(mode_of), vinfo $(rate_of) Гц"
		cmd_mode 60
		echo "  режим поднят до 60 Гц на живом видео — это и есть проверка зажима"
		;;
	C)	# Лечение: тот же ролик перезапускается уже при 60 Гц.
		cmd_mode 60
		cmd_stop
		sleep 2
		cmd_play "${URL2:-$URL}"
		;;
	D)	# Реальный сценарий владельца (листание роликов): демон живой, YouTube
		# НЕ в чёрном списке. Сначала играет 24-фпс ролик — демон удерживает
		# 24 Гц, — затем запускается второй ролик (60 fps): демон через замер
		# переключит режим сам. Вердикт — про ВТОРОЙ ролик: если он дёрганый
		# уже при 60 Гц, значит темп зафиксировал плеер (это и есть зажим).
		if [ -z "$URL24" ]; then
			echo "для фазы D нужен 24-фпс ролик: URL24=<ссылка> sh $0 run D" >&2
			return 1
		fi
		cmd_daemon start
		cmd_mode 60
		cmd_stop
		cmd_play "$URL24"
		echo "  ждём, пока демон удержит 24 Гц (замер ~15-20 с)…"
		sleep 30
		say "D: после 24-фпс ролика — ядро $(mode_of), vinfo $(rate_of) Гц"
		cmd_play "${URL2:-$URL}"
		echo "  запущен второй ролик (60 fps) — смотрим его плавность"
		;;
	*)	echo "usage: $0 run A|B|C|D" >&2; return 1 ;;
	esac
	sleep 12
	say "run $1: ядро $(mode_of), vinfo $(rate_of) Гц, демон pid $(daemon_of)"
	echo
	echo "Оцените картинку глазами (владелец) и запишите вердикт:"
	echo "  sh $0 mark $1 smooth   # плавно"
	echo "  sh $0 mark $1 jerky    # дёргано / рывки"
}

cmd_mark () { # <фаза> smooth|jerky
	case "${2:-}" in
	smooth|jerky) ;;
	*) echo "usage: $0 mark A|B|C smooth|jerky" >&2; return 1 ;;
	esac
	say "ВЕРДИКТ $1: $2 (режим $(mode_of), vinfo $(rate_of) Гц)"
}

# --- протокол ----------------------------------------------------------------

cmd_log () { # [строк]
	need_stand
	n=${1:-40}
	suroot "tail -n $n $LOG"
	echo "--- ключевые строки ---"
	suroot "grep -nE 'переключ|режим|зажим|подача|пропускаю|skip' $LOG | tail -n 25"
}

cmd_report () {
	need_stand
	echo "=== протокол ($EV) ==="
	[ -f "$EV" ] && cat "$EV" || echo "(пусто — прогон не начат)"
	echo
	echo "=== вердикты ==="
	[ -f "$EV" ] && grep 'ВЕРДИКТ' "$EV" || echo "(нет)"
	echo
	echo "=== последние строки журнала демона ==="
	suroot "grep -nE 'переключ|режим|подача' $LOG | tail -n 20"
	echo
	echo "=== состояние ==="
	cmd_status
	echo
	echo "Как читать (§34.10): дёргано в B и плавно в A/C — зажим подтверждён (тогда"
	echo "возвращаться к возврату режима в момент остановки предыдущего ролика)."
	echo "Плавно и в B — зажима нет: пересматриваем гипотезу и чёрный список YouTube."
	echo "В D важен вердикт про ВТОРОЙ ролик: дёргано уже при 60 Гц — это и есть жалоба"
	echo "владельца (листание), демон тут ни при чём."
	echo "Рывки ровно вдвое (как 10-15 к/с) — это не зажим, а дисплейная половинная"
	echo "подача (§21.9): её лечение — переключение «туда-обратно»."
}

case "${1:-}" in
status)		cmd_status ;;
prep)		cmd_prep ;;
rollback)	cmd_rollback ;;
mode)		shift; cmd_mode "$@" ;;
play)		shift; cmd_play "$@" ;;
stop)		cmd_stop ;;
run)		shift; cmd_run "$@" ;;
daemon)		shift; cmd_daemon "$@" ;;
mark)		shift; cmd_mark "$@" ;;
log)		shift; cmd_log "$@" ;;
report)		cmd_report ;;
*)
	cat >&2 <<EOF
usage: $0 <команда> [аргументы]

  status                 снимок стенда (режим, ТВ, демон, конфиг, тракт)
  prep                   сохранить ini и поставить конфиг на прогон (журнал, список)
  run A|B|C|D            выполнить фазу сценария §34.10 (D — реальный сценарий)
  daemon stop|start|state   снять/поднять службу приложения (она держит демон)
  mark <фаза> smooth|jerky   записать вердикт владельца
  log [строк]            журнал демона + ключевые строки
  report                 протокол прогона, вердикты, состояние
  mode <Гц>              выставить режим 1920x1080 при заданной частоте
  play [url] | stop      запуск/остановка штатного YouTube
  rollback               вернуть ini и режим $BASE_HZ Гц

Переменные: D=$D  URL=$URL  BASE_HZ=$BASE_HZ  YTCLAMP_DIR=$DIR
EOF
	exit 1
	;;
esac
