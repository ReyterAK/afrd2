#!/bin/sh
# Стенд ATV14: матрица приёмки AFRd2 (спека §6, план Task 9).
#
# Для каждого клипа:
#   1) запустить клип в VLC (у плеера нет своего AFR — работает только демон);
#   2) через HOLD_WAIT снять состояние — ожидается удержание режима под видео;
#   3) остановить плеер, через STOP_WAIT снять состояние — ожидается возврат
#      исходного режима и снятый флаг backdoor.
# Опционально: шаг 2a — пауза/возобновление (PAUSE_TEST=1) и шаг 3 — плеер
# со своим AFR (OWN_AFR_TEST=1).
#
# Эталон ожиданий. Три клипа подтверждены живым прогоном 2026-09-29
# (`/tmp/afrd-acceptance-2208.log`, затем полный прогон `/tmp/afrd-acceptance-fix2.log`:
# OK=19 FAIL=0 SKIP=0), значения в таблице — измеренные:
#
#   клип             | во время видео                  | после стопа
#   long_24000_1001  | 1080p24hz + activeMode=23.98 Hz | 1080p60hz + 59.94 Hz + backdoor=false
#   long_25          | 1080p25hz + activeMode=25.00 Hz | 1080p60hz + 59.94 Hz + backdoor=false
#   long_50          | 1080p50hz + activeMode=50.00 Hz | 1080p60hz + 59.94 Hz + backdoor=false
#
# Для 25 fps ожидание было 1080p50hz (ошибка плана, R48): резолвер ATV14 берёт
# точную частоту из таблицы SurfaceFlinger, поэтому 25 fps -> 1080p25hz, а не
# удвоение. Исправлено по факту живого прогона.
#
# Запуск:
#   sh tools/stand/acceptance_matrix.sh
#   PAUSE_TEST=1 sh tools/stand/acceptance_matrix.sh
#   API_TEST=1 OWN_AFR_TEST=1 sh tools/stand/acceptance_matrix.sh
#   LOG=acceptance-$(date +%Y%m%d-%H%M).log sh tools/stand/acceptance_matrix.sh
#
# Переменные:
#   ADB_DEV (или D) — адрес adb-устройства (192.168.88.50:5555);
#   CLIPS           — клипы без расширения (long_24000_1001 long_25 long_50);
#   CLIP_DIR        — каталог с клипами на устройстве (/sdcard/Download/afr);
#   ORIG_MODE       — режим, куда демон обязан вернуться (1080p60hz);
#   HOLD_WAIT/STOP_WAIT — паузы в секундах (8 / 5);
#   PLAYER          — компонент плеера без своего AFR (org.videolan.vlc/.StartActivity);
#   PAUSE_TEST      — 1: добавить проверку паузы; PAUSE_CLIP — клип для неё;
#   PAUSE_EXPECT    — keep (профиль pause.keep=1, по умолчанию) | restore (pause.keep=0);
#   OWN_AFR_TEST    — 1: добавить проверку плеера со своим AFR; OWN_AFR_ACT — его
#                     активность, OWN_AFR_CLIP — клип для проверки;
#   API_TEST        — 1: добавить проверку API (шаг 3a): status, приоритет подсказки
#                     плеера и допущение R34в (сброс счётчика кадров); AFRD_LOG —
#                     путь к логу демона (/data/local/afrd.log), API_RESET_CLIP_A/B и
#                     API_HINT_CLIP — клипы для этих проверок;
#   API_TIMEOUT     — таймаут обёртки `timeout` вокруг вызовов `nc -u` (по умолчанию
#                     2 с: на стенде toybox nc с -u не завершается после ответа,
#                     код 124 от timeout — норма);
#   LOG             — файл, в который дописывается протокол (иначе только stdout).
#
# Для PAUSE_EXPECT=restore нужно предварительно выставить pause.keep=0 в конфиге
# НА УСТРОЙСТВЕ и перезапустить демон (reconf в ATV14-пути не действует):
#   adb -s $D shell "sed -i 's/^pause.keep=.*/pause.keep=0/' /data/local/tmp/afrd.ini"
#   adb -s $D shell '/data/local/tmp/afrd -k -D /data/local/tmp/afrd.ini'
#   PAUSE_TEST=1 PAUSE_EXPECT=restore sh tools/stand/acceptance_matrix.sh
#
# Код возврата: 0 — все проверки OK, 1 — есть FAIL, 2 — проверка не выполнена
#               (стенд недоступен, демон не запущен или одни SKIP).

D=${ADB_DEV:-${D:-192.168.88.50:5555}}
SDIR=$(cd "$(dirname "$0")" && pwd)
CLIP_DIR=${CLIP_DIR:-/sdcard/Download/afr}
CLIPS=${CLIPS:-"long_24000_1001 long_25 long_50"}
ORIG_MODE=${ORIG_MODE:-1080p60hz}
HOLD_WAIT=${HOLD_WAIT:-8}
STOP_WAIT=${STOP_WAIT:-5}
PLAYER=${PLAYER:-org.videolan.vlc/.StartActivity}
PAUSE_TEST=${PAUSE_TEST:-0}
PAUSE_CLIP=${PAUSE_CLIP:-long_24000_1001}
PAUSE_EXPECT=${PAUSE_EXPECT:-keep}
OWN_AFR_TEST=${OWN_AFR_TEST:-0}
OWN_AFR_ACT=${OWN_AFR_ACT:-net.gtvbox.videoplayer/.PlayerActivity}
OWN_AFR_CLIP=${OWN_AFR_CLIP:-long_24000_1001}
API_TEST=${API_TEST:-0}
AFRD_LOG=${AFRD_LOG:-/data/local/afrd.log}
# На стенде toybox `netcat 0.8.9-android` с -u не завершается после ответа
# (-w для UDP игнорируется) — поэтому все вызовы API обёрнуты в `timeout`
# (измерено вживую: ответ приходит, команда висит). Код 124 от timeout нормален.
API_TIMEOUT=${API_TIMEOUT:-2}
API_RESET_CLIP_A=${API_RESET_CLIP_A:-long_24000_1001}
API_RESET_CLIP_B=${API_RESET_CLIP_B:-long_50}
API_HINT_CLIP=${API_HINT_CLIP:-long_50}
LOG=${LOG:-}

if [ -n "$LOG" ]; then
	exec >> "$LOG" 2>&1
fi

oks=0
fails=0
skips=0

ok ()
{
	oks=$((oks + 1))
	echo "OK: $*"
}

fail ()
{
	fails=$((fails + 1))
	echo "FAIL: $*"
}

skip ()
{
	skips=$((skips + 1))
	echo "SKIP: $*"
}

# Ждём, пока декодер освободится после остановки тестового плеера. Если играет
# чужое приложение (например, IPTV-канал владельца), демон справедливо удерживает
# режим, и вердикты «режим не вернулся к $ORIG_MODE» и «флаг backdoor не снят»
# были бы ложными FAIL'ами (живой случай 2026-09-30). Возврат: 0 — свободен,
# 1 — занят чужим видео.
decoder_idle ()
{
	i=0
	while [ "$i" -lt "${IDLE_TRIES:-20}" ]; do
		out=$(adb -s "$D" shell 'cat /sys/class/vdec/vdec_status' 2>/dev/null)
		case "$out" in
			'')          return 0 ;;   # прочитать не удалось (adb/стенд) — не наш вердикт
			*'No vdec'*) return 0 ;;   # декодер свободен
		esac
		sleep 1
		i=$((i + 1))
	done
	return 1
}

# Снимок состояния: одна строка «mode | hdmi | activeMode=… | backdoor=…».
snap ()
{
	sh "$SDIR/check_mode.sh"
}

# Поле снимка по номеру 1..4 (поля разделены «|»), краевые пробелы убраны.
fld ()
{
	printf '%s\n' "$1" | cut -d'|' -f"$2" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//'
}

# Годен ли снимок для сравнений: он непустой и в нём есть поле режима.
# Пустые поля «равны» друг другу, поэтому любое сравнение по снимку без режима
# дало бы ложный OK (снимок с нечитаемым /sys/class/display/mode выглядит как
# непустая строка с пустым первым полем).
snap_ok ()
{
	_sn=$1
	[ -n "$_sn" ] || return 1
	_sm=$(fld "$_sn" 1)
	[ -n "$_sm" ] && [ "$_sm" != n/a ]
}

echo "== матрица приёмки afrd2/ATV14 =="
echo "дата: $(date '+%Y-%m-%d %H:%M:%S %Z')"
echo "устройство: $D"

if ! adb -s "$D" shell true >/dev/null 2>&1; then
	echo "стенд $D недоступен — матрица НЕ прогнана (ни одна проверка не выполнена)" >&2
	exit 2
fi

# Без запущенного демона проверять нечего: по контракту скрипта это «ни одна
# проверка не выполнена» (код 2), а не провал приёмки.
daemon_pid=$(adb -s "$D" shell pidof afrd | tr -d '\r')
if [ -z "$daemon_pid" ]; then
	echo "SKIP: демон afrd не запущен на $D — ни одна проверка не выполнена" >&2
	echo "      Запустите его и повторите: D=$D sh tools/stand/build_push.sh" >&2
	exit 2
fi
echo "демон afrd: pid $daemon_pid"

# Занятость декодера на старте: предупреждение (не FAIL — возможно, остаток
# предыдущего шага, но вердикты по возврату режима будут ложными). Проверка идёт
# после проверок стенда и демона: при мёртвом adb decoder_idle() возвращает
# «свободен» и не тормозит прогон на IDLE_TRIES секунд.
if ! decoder_idle; then
	echo "ВНИМАНИЕ: декодер занят до начала прогона — играет другое приложение" \
	     "(чужое видео). Вердикты по возврату режима могут быть ложными."
fi

# Состав клипов не выдумываем — печатаем то, что реально лежит на устройстве.
echo "клипы на устройстве ($CLIP_DIR):"
adb -s "$D" shell "ls -l $CLIP_DIR 2>&1" | tr -d '\r'

mode0=$(adb -s "$D" shell 'cat /sys/class/display/mode' | tr -d '\r')
echo "режим до прогона: $mode0"

for clip in $CLIPS; do
	echo
	echo "=== $clip"

	if ! sh "$SDIR/play_clip.sh" start "$clip"; then
		skip "$clip: не запущен (нет клипа или ошибка adb) — проверка не выполнена"
		continue
	fi

	sleep "$HOLD_WAIT"
	hold=$(snap)
	echo "в удержании: ${hold:-<пустой снимок>}"

	case "$clip" in
		*23976*|*24000*) exp_hz=23.98; exp_mode=1080p24hz ;;
		*_25)            exp_hz=25.00; exp_mode=1080p25hz ;;
		*_50)            exp_hz=50.00; exp_mode=1080p50hz ;;
		*)               exp_hz=;      exp_mode= ;;
	esac

	if ! snap_ok "$hold"; then
		skip "$clip: снимок удержания не снят (пустой снимок или пустое поле режима) — вердикт не выносится"
	elif [ -z "$exp_mode" ]; then
		skip "$clip: эталона в плане нет — вердикт не выносится (снимок выше)"
	else
		m=$(fld "$hold" 1)
		a=$(fld "$hold" 3)
		if [ "$m" = "$exp_mode" ]; then
			ok "$clip: режим удержания $m"
		else
			fail "$clip: режим удержания $m, ожидался $exp_mode"
		fi
		if printf '%s' "$a" | grep -q "$exp_hz"; then
			ok "$clip: SF activeMode = $a"
		else
			fail "$clip: SF activeMode = $a, ожидался $exp_hz Hz"
		fi
	fi

	sh "$SDIR/play_clip.sh" stop
	if ! decoder_idle; then
		skip "$clip: после стопа декодер занят (играет другое приложение) — вердикт по возврату режима не выносится"
		continue
	fi
	sleep "$STOP_WAIT"
	after=$(snap)
	echo "после стопа: ${after:-<пустой снимок>}"

	if ! snap_ok "$after"; then
		skip "$clip: снимок после стопа не снят (пустой снимок или пустое поле режима) — вердикт не выносится"
	else
		m=$(fld "$after" 1)
		b=$(fld "$after" 4)
		if [ "$m" = "$ORIG_MODE" ]; then
			ok "$clip: возврат в $ORIG_MODE"
		else
			fail "$clip: после стопа режим $m, ожидался $ORIG_MODE"
		fi
		case "$b" in
			*false*) ok "$clip: флаг backdoor снят ($b)" ;;
			*)       fail "$clip: флаг backdoor НЕ снят ($b)" ;;
		esac
	fi
done

# Шаг 2a: пауза. Эталон — план Task 9: при pause.keep=1 режим на паузе не
# меняется, при pause.keep=0 возвращается исходный; после возобновления режим
# под видео снова удерживается.
if [ "$PAUSE_TEST" = 1 ]; then
	echo
	echo "=== пауза ($PAUSE_CLIP, ожидание: $PAUSE_EXPECT)"

	before=
	pp=
	pn=

	if ! sh "$SDIR/play_clip.sh" start "$PAUSE_CLIP"; then
		skip "пауза: клип $PAUSE_CLIP не запущен — проверка не выполнена"
	else
		sleep "$HOLD_WAIT"
		before=$(snap)
		echo "до паузы: ${before:-<пустой снимок>}"

		adb -s "$D" shell 'input keyevent KEYCODE_MEDIA_PAUSE' >/dev/null
		sleep 4
		pp=$(snap)
		echo "на паузе: ${pp:-<пустой снимок>}"

		adb -s "$D" shell 'input keyevent KEYCODE_MEDIA_PLAY' >/dev/null
		sleep 4
		pn=$(snap)
		echo "после возобновления: ${pn:-<пустой снимок>}"

		sh "$SDIR/play_clip.sh" stop
	fi

	# Пустой снимок или пустое ПОЛЕ РЕЖИМА сравнивать нельзя: пустые поля
	# «равны» друг другу и дали бы OK без измерения.
	if ! snap_ok "$before" || ! snap_ok "$pp" || ! snap_ok "$pn"; then
		skip "пауза: состояние снять не удалось (пустой снимок или пустой режим) — проверка не выполнена"
	else
		hold_mode=$(fld "$before" 1)
		if [ "$hold_mode" = "$ORIG_MODE" ]; then
			# без состоявшегося удержания обе ветки выглядят одинаково —
			# это была бы ложноположительная проверка
			skip "пауза: удержание не поднялось (режим $hold_mode) — проверка не выполнена"
		else
			pm=$(fld "$pp" 1)
			pb=$(fld "$pp" 4)
			rm=$(fld "$pn" 1)

			if [ "$PAUSE_EXPECT" = restore ]; then
				if [ "$pm" = "$ORIG_MODE" ]; then
					ok "pause.keep=0: на паузе вернулись в $ORIG_MODE"
				else
					fail "pause.keep=0: на паузе режим $pm, ожидался $ORIG_MODE"
				fi
				case "$pb" in
					*false*) ok "pause.keep=0: флаг backdoor на паузе снят ($pb)" ;;
					*)       fail "pause.keep=0: флаг backdoor на паузе не снят ($pb)" ;;
				esac
			else
				if [ "$pm" = "$hold_mode" ]; then
					ok "pause.keep=1: на паузе режим удержан ($pm)"
				else
					fail "pause.keep=1: на паузе режим $pm, ожидался $hold_mode"
				fi
			fi

			if [ "$rm" = "$hold_mode" ]; then
				ok "после возобновления режим снова $hold_mode"
			else
				fail "после возобновления режим $rm, ожидался $hold_mode"
			fi
		fi
	fi
fi

# Шаг 3: плеер со своим AFR. Он должен выставить режим сам, а демон — не
# вмешиваться (в логе ATV14-пути отдельной строки про «режим уже подходит» нет,
# поэтому признак — снятый флаг backdoor: режим выставлен не нами).
if [ "$OWN_AFR_TEST" = 1 ]; then
	echo
	echo "=== плеер со своим AFR ($OWN_AFR_ACT)"

	file="$CLIP_DIR/$OWN_AFR_CLIP.mp4"
	adb -s "$D" shell "am force-stop ${PLAYER%%/*}" >/dev/null
	sleep 2
	if ! decoder_idle; then
		skip "свой AFR: декодер занят другим приложением — проверка не выполнена"
	elif ! adb -s "$D" shell "test -f '$file'" >/dev/null 2>&1; then
		skip "свой AFR: клип $file не найден — проверка не выполнена"
	else
		adb -s "$D" shell "am start -n $OWN_AFR_ACT -a android.intent.action.VIEW -d file://$file -t video/mp4" >/dev/null
		sleep "$HOLD_WAIT"
		s=$(snap)
		echo "состояние: ${s:-<пустой снимок>}"
		if ! snap_ok "$s"; then
			# снимок без поля режима дал бы «режим выставлен нашим демоном
			# (backdoor=)» — это неверный вердикт, а не измерение
			skip "свой AFR: снимок состояния (или поле режима) не снят — проверка не выполнена"
		else
			m=$(fld "$s" 1)
			b=$(fld "$s" 4)
			if [ "$m" = 1080p24hz ]; then
				ok "плеер выставил режим сам ($m)"
			else
				fail "режим $m, ожидался 1080p24hz"
			fi
			case "$b" in
				*false*) ok "демон не вмешался (backdoor=false)" ;;
				*)       fail "режим выставлен нашим демоном (backdoor=$b) — совместимость нарушена" ;;
			esac
		fi
		adb -s "$D" shell "am force-stop ${OWN_AFR_ACT%%/*}" >/dev/null
	fi
fi

# Шаг 3a: API (UDP 50505), приоритет подсказки плеера и допущение R34в
# (счётчик кадров нового инстанса декодера начинается с нуля).
if [ "$API_TEST" = 1 ]; then
	echo
	echo "=== API (UDP 50505), подсказка плеера и R34в"

	# 1) status: на ATV14-пути update_stats() берёт поля из состояния цикла,
	#    поэтому current hz обязан быть не нулевым
	st=$(adb -s "$D" shell "echo status | timeout $API_TIMEOUT nc -u -w1 127.0.0.1 50505" | tr -d '\r')
	echo "status: $(printf '%s' "$st" | tr '\n' ' ')"
	if printf '%s' "$st" | grep -qE '^current hz:[1-9]'; then
		ok "API: status отвечает, current hz не нулевой"
	else
		fail "API: в status нет ненулевого current hz (вывод выше)"
	fi

	# 2) R34в: счётчик кадров нового инстанса декодера должен начинаться с нуля
	frames ()
	{
		adb -s "$D" shell "grep -oE 'frame count *: *[0-9]+' /sys/class/vdec/vdec_status" |
			sed -n 1p | tr -cd '0-9'
	}

	clip_a=$API_RESET_CLIP_A
	clip_b=$API_RESET_CLIP_B
	if ! sh "$SDIR/play_clip.sh" start "$clip_a"; then
		skip "R34в: клип $clip_a не запущен — проверка не выполнена"
	else
		sleep "$HOLD_WAIT"
		c1=$(frames)
		echo "frame count на $clip_a: ${c1:-нет данных}"

		# второй файл стартуем сами: нужен именно момент появления нового
		# инстанса декодера, а play_clip.sh делает лишнюю паузу в 2 с
		file="$CLIP_DIR/$clip_b.mp4"
		if ! adb -s "$D" shell "test -f '$file'" >/dev/null 2>&1; then
			skip "R34в: клип $file не найден — проверка не выполнена"
		else
			adb -s "$D" shell "am force-stop ${PLAYER%%/*}" >/dev/null
			adb -s "$D" shell "am start -n $PLAYER -a android.intent.action.VIEW -d file://$file -t video/mp4" >/dev/null
			sleep 1
			c2=$(frames)
			echo "frame count сразу после старта $clip_b: ${c2:-нет данных}"
			if [ -z "$c1" ] || [ -z "$c2" ]; then
				skip "R34в: счётчик кадров не читается — проверка не выполнена"
			elif [ "$c2" -lt "$c1" ]; then
				ok "R34в: счётчик нового инстанса начался с нуля ($c2 < $c1)"
			else
				fail "R34в НЕ подтверждено ($c2 >= $c1): детект сброса счётчика в run_atv14() надо выносить отдельной заявкой"
			fi
		fi
	fi

	# 3) подсказка плеера (fps.prefer.api=1) должна опережать собственный замер:
	#    шлём её сразу после am start, пока демон ещё не решил режим
	sh "$SDIR/play_clip.sh" stop
	sleep "$STOP_WAIT"

	file="$CLIP_DIR/$API_HINT_CLIP.mp4"
	if ! adb -s "$D" shell "test -f '$file'" >/dev/null 2>&1; then
		skip "подсказка: клип $file не найден — проверка не выполнена"
	else
		# Смотрим только строки, появившиеся ПОСЛЕ отправки подсказки: иначе
		# строка от прошлого прогона дала бы OK без применения подсказки.
		lines0=$(adb -s "$D" shell "wc -l < $AFRD_LOG 2>/dev/null" | tr -cd '0-9')
		[ -n "$lines0" ] || lines0=0

		adb -s "$D" shell "am start -n $PLAYER -a android.intent.action.VIEW -d file://$file -t video/mp4" >/dev/null
		adb -s "$D" shell "echo 'frame_rate_hint 23976' | timeout $API_TIMEOUT nc -u -w1 127.0.0.1 50505" >/dev/null
		sleep "$HOLD_WAIT"

		hint=$(adb -s "$D" shell "tail -n +$((lines0 + 1)) $AFRD_LOG 2>/dev/null | grep 'по подсказке' | tail -1" | tr -d '\r')
		s=$(snap)
		echo "подсказка в новых строках лога (после $lines0): ${hint:-нет}"
		echo "состояние: $s"
		if [ -z "$hint" ]; then
			fail "в новых строках лога ($AFRD_LOG, ключ log.file) нет «по подсказке плеера» — подсказка не применена"
		elif printf '%s' "$hint" | grep -q '23\.98'; then
			ok "подсказка плеера применена (23.98 Гц): $hint"
		else
			fail "подсказка применена не с ожидаемой частотой 23.98 Гц: $hint"
		fi
		# режим печатаем информативно: подсказка живёт 1 с, и при неудачном
		# стечении с дебаунсом 500 мс она может истечь до решения демона
		echo "ИНФО: режим при подсказке 23976 на клипе $API_HINT_CLIP — $(fld "$s" 1) (ожидается 1080p24hz)"

		sh "$SDIR/play_clip.sh" stop
		sleep "$STOP_WAIT"
	fi
fi

echo
echo "== итог: OK=$oks FAIL=$fails SKIP=$skips =="
echo "эталон ожиданий — в комментарии в начале этого файла;"
echo "при FAIL/SKIP приложите полный вывод этого прогона."

[ "$fails" = 0 ] || exit 1

# «Все пункты в SKIP» — это не успех: ни одна проверка не выполнена
# (клипов нет, стенд отвалился посреди прогона), результат не получен.
if [ "$oks" = 0 ]; then
	echo "ни одна проверка не выполнена (одни SKIP) — результат не получен" >&2
	exit 2
fi

exit 0
