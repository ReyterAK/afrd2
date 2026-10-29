#!/bin/sh
# Стенд Android 11 (Amlogic, вендорный SystemControl): таблица приёмки спеки §7.
#
# Каждый клип: запустить, ждать ожидаемый режим (с замером времени), проверить
# статистику в удержании, остановить, проверить возврат исходного режима.
#
# ВАЖНО про время: драйвер на этом боксе заявляет частоту НЕ всегда честно
# (живой случай: на реальном 25 fps отдаёт 30), поэтому часть решений приходит
# только от замера — это ~17 с, а не 5 с. Скрипт поэтому ждёт до WAIT_MAX и
# печатает, сколько секунд заняло; строки «быстрое решение» и замер в журнале
# различаются отдельными проверками.
#
# Проверки (нумерация — для отчёта):
#   1. 23.976 fps  -> 1080p24hz + возврат исходного
#   2. 25 fps      -> 1080p50hz + возврат исходного + статистика (switched, 50/60 Гц)
#   3. 60 fps      -> режим не трогается
#   4. синтетика   -> режим меняется, в журнале есть решение (быстрое/замер)
#   5. fps.declare.fast=0 -> решение только замером (без строк «быстрое решение»)
#   6. помощник недоступен (dex испорчен) -> предупреждение, режим не тронут,
#      повторов <= 2, приложение восстанавливает помощника
#   7. помощник завис (sleep 60) -> демон не встаёт: отвечает и пишет про предел
#   8. vendor.disp.cap в никуда -> ровно одно предупреждение, режим не тронут
#
# Предусловие: журнал демона включён. Приложение при запуске перезаписывает ini
# из своих настроек, поэтому правка log.enable в ini не держится — включать надо
# в настройках приложения (Прочее -> Журнал). Без журнала проверки 4-8, которые
# его читают, помечаются SKIP: они не выполнены по внешней причине, и в FAIL им
# попадать нельзя (живой прогон 2026-10-15 с log.enable=0 дал пять ложных FAIL).
#
# Использование:  ./tools/stand/acceptance_atv11.sh [устройство]
# Переменные: PLAYER (пакет плеера), CLIPS (каталог), C23976/C25/C60/CSYNTH
# (имена клипов), WAIT_MAX, LOG_WAIT. Клипы и плеер проверяются до первого
# изменения на боксе: без них прогон заканчивается сообщением и кодом 3.

set -u

D=${1:-192.168.88.44:5555}
PKG=ru.reyter.afrd2
# Плеер и набор клипов задаются переменными: на одном стенде стоит Vimu, на
# другом только Just Player, и клипы лежат в разных каталогах. Раньше значения
# были зашиты (net.gtvbox.videoplayer и /sdcard/Download/afr45), поэтому на
# стенде без Vimu прогон не начинался вовсе (живая находка 27.10 на `.42`).
PLAYER=${PLAYER:-net.gtvbox.videoplayer}
CLIPS=${CLIPS:-/sdcard/Download/afr45}
# Имена клипов в наборе: 23.976, 25, 60 и «синтетика 23.976».
C23976=${C23976:-real_23976.mkv}
C25=${C25:-real_25.mkv}
C60=${C60:-t60.mp4}
CSYNTH=${CSYNTH:-long_23976_synth.mp4}
CACHE=/data/user/0/$PKG/cache
INI=$CACHE/afrd.ini
LOG=/data/local/afrd.log
MODE_PATH=/sys/class/display/mode
HELPER=$CACHE/afrd-disp.sh
WAIT_MAX=${WAIT_MAX:-35}   # предел ожидания режима (замер бывает ~17 с)
LOG_WAIT=${LOG_WAIT:-6}    # пауза после возврата, чтобы журнал дописался

OK=0
FAIL=0
SKIP=0

say () { echo "$*"; }
sh_ () { adb -s "$D" shell "$1" 2>&1 | tr -d '\r'; }
# Внимание: su -c оборачивается в ОДИНАРНЫЕ кавычки, поэтому внутри нельзя
# использовать одинарные (иначе сломается разбор на устройстве).
su_ () { adb -s "$D" shell su -c "$(printf '%s' "$1" | sed "s/'/\\\\'/g")" 2>&1 | tr -d '\r'; }

mode () { su_ "cat $MODE_PATH" | tail -1; }
log_len () { su_ "wc -l < $LOG" | tail -1 | tr -d ' '; }
log_since () { su_ "tail -n +$1 $LOG"; }

pass () { OK=$((OK + 1)); say "  OK   $*"; }
fail () { FAIL=$((FAIL + 1)); say "  FAIL $*"; }
# Проверка не выполнена по внешней причине (например, журнал демона выключен) —
# это не дефект демона, и в FAIL такие строки попадать не должны: прогон с
# выключенным журналом уже один раз дал «FAIL=5» на исправной сборке
# (2026-10-15: приложение перезаписывает ini из своих настроек, поэтому
# log.enable в ini не держится — см. проверку предусловий ниже).
skip () { SKIP=$((SKIP + 1)); say "  SKIP $*"; }

# Ждать режим до $2 секунд; печатает, сколько ждал. Возврат 0 при совпадении.
wait_mode () { # wait_mode <режим> <секунд>
	i=0
	while [ "$i" -lt "$2" ]; do
		if [ "$(mode)" = "$1" ]; then
			echo "$i"
			return 0
		fi
		sleep 1
		i=$((i + 1))
	done
	echo "$i"
	return 1
}

# MIME по расширению: набор бывает и .mkv, и .mp4 — с неверным типом плеер
# молча открывает пустоту, и прогон получается ложным.
mime_of () {
	case "$1" in
		*.mkv) echo video/x-matroska ;;
		*.mp4) echo video/mp4 ;;
		*.ts)  echo video/mp2t ;;
		*)     echo video/* ;;
	esac
}
play () { sh_ "am start -a android.intent.action.VIEW -d file://$CLIPS/$1 -t $(mime_of "$1") -n $PLAYER/.PlayerActivity" >/dev/null; }
stop_player () { sh_ "am force-stop $PLAYER" >/dev/null; }

restart_daemon () {
	sh_ "am force-stop $PKG" >/dev/null
	sleep 1
	sh_ "monkey -p $PKG -c android.intent.category.LAUNCHER 1" >/dev/null 2>&1
	sleep 9
	stop_player
}

restore_ini () {
	su_ "sed -i s@^vendor.disp.cap=.*@vendor.disp.cap=/sys/class/amhdmitx/amhdmitx0/disp_cap@ $INI" >/dev/null
	su_ "sed -i s@^vendor.disp.cli=.*@vendor.disp.cli=$HELPER@ $INI" >/dev/null
	su_ "sed -i s@^fps.declare.fast=.*@fps.declare.fast=1@ $INI" >/dev/null
}

# Одна проверка клипа: <№> <файл> <ожидаемый режим|-> <макс. секунд>
check_clip () { # $1 номер, $2 файл, $3 режим, $4 предел
	say "$1. $2 -> $3"
	mark=$(log_len)
	play "$2"
	secs=$(wait_mode "$3" "$4")
	if [ "$(mode)" = "$3" ]; then
		pass "режим $3 за $secs с"
	else
		fail "режим $(mode), ожидался $3 за $4 с"
	fi
	sleep 2
	log_since "$mark" | sed 's/^/       | /' | grep -E 'быстрое решение|заявлено|замер|выбран|Переключаю' | head -6
}

# --- предусловия -----------------------------------------------------------
adb -s "$D" shell true >/dev/null 2>&1 || { say "нет устройства $D"; exit 2; }

# Плеер и клипы проверяются ДО любых изменений на боксе: без них первые же
# проверки дают «режим не сменился», и это выглядит как дефект демона, хотя
# виноват набор стенда (живой прогон 27.10 на `.42`: Vimu не установлен).
missing=""
for c in "$C23976" "$C25" "$C60" "$CSYNTH"; do
	# Проверка идёт через adb напрямую, без sh_: у sh_ вывод идёт через tr,
	# и статус выхода всегда нулевой — отсутствие клипов не замечалось вовсе
	# (живой прогон 27.10 на `.42`: все проверки упали, потому что каталога
	# /sdcard/Download/afr45 на боксе нет, а предусловие это «пропустило»).
	adb -s "$D" shell "test -f '$CLIPS/$c'" >/dev/null 2>&1 || missing="$missing $c"
done
if [ -n "$missing" ]; then
	say "в $CLIPS нет клипов:$missing"
	say "положите их (или задайте CLIPS=… C23976=… C25=… C60=… CSYNTH=…) и повторите;"
	say "ничего на боксе не менялось."
	exit 3
fi
if ! adb -s "$D" shell "pm list packages $PLAYER" 2>/dev/null | grep -q "package:$PLAYER"; then
	say "плеер $PLAYER не установлен на $D; задайте PLAYER=<пакет> (нужен плеер без своего AFR,"
	say "например com.brouken.player или net.gtvbox.videoplayer) — на боксе ничего не менялось."
	exit 3
fi

ORIG_AFR=$(su_ "getprop persist.sys.autoframemode" | tail -1)
MODE0=$(mode)
# Фактическая частота исходного режима: 60.000 и 59.940 в узле называются
# одинаково (1080p60hz), а `afrd -s` на вендорном пути печатает факт из vinfo.
# Раньше проверка ждала литерал «60.00Hz» и на стенде с 59.94 падала (живой
# прогон 27.10 на `.42`: единственный FAIL прогона).
ORIG_HZ=$(su_ "cat /sys/class/display/vinfo" | awk '/sync_duration_num/{n=$2} /sync_duration_den/{d=$2} END{if(d+0>0) printf "%.2f", n/d}')
case "$ORIG_HZ" in ""|*[!0-9.]*) ORIG_HZ=60.00 ;; esac
say "стенд $D: исходный режим $MODE0; системный AFR был '$ORIG_AFR'"
say ""

su_ "setprop persist.sys.autoframemode 0" >/dev/null
su_ "sh $HELPER $MODE0" >/dev/null 2>&1
restore_ini
restart_daemon

# Предусловие: журнал демона включён. Проверяем ПОСЛЕ restart_daemon, потому что
# приложение при запуске перезаписывает ini из своих настроек: правка
# log.enable в ini живёт до следующего monkey-запуска и на приёмку не влияет
# (живая находка 2026-10-15 — прогон с log.enable=0 дал пять ложных FAIL).
LOG_OK=1
if [ "$(su_ "grep -c '^log.enable=1' $INI" | tail -1)" != "1" ]; then
	LOG_OK=0
	say "! журнал демона выключен: приложение пишет в ini log.enable=0 (из своих настроек)."
	say "! Включите «Журнал» в настройках приложения (Прочее -> Журнал) и прогоните снова:"
	say "! проверки, читающие журнал, будут помечены SKIP (причина — не дефект демона)."
	say ""
fi

restore () {
	stop_player
	restore_ini
	su_ "rm -f /data/local/tmp/hang.sh /data/local/tmp/sleeper" >/dev/null
	sh_ "am force-stop $PKG" >/dev/null
	sleep 1
	su_ "setprop persist.sys.autoframemode ${ORIG_AFR:-2}" >/dev/null
	su_ "sh $HELPER $MODE0" >/dev/null 2>&1
	sh_ "monkey -p $PKG -c android.intent.category.LAUNCHER 1" >/dev/null 2>&1
	sleep 6
	stop_player
}
trap 'restore; exit 1' INT TERM

# --- 1. 23.976 --------------------------------------------------------------
check_clip 1 "$C23976" 1080p24hz "$WAIT_MAX"
stop_player
secs=$(wait_mode "$MODE0" 15)
[ "$(mode)" = "$MODE0" ] && pass "возврат $MODE0 за $secs с" || fail "после стопа $(mode), ожидался $MODE0"
say ""

# --- 2. 25 fps + статистика -------------------------------------------------
check_clip 2 "$C25" 1080p50hz "$WAIT_MAX"
st=$(su_ "$CACHE/afrd -s -p /dev/run/afrd.pid")
say "$(echo "$st" | grep -E 'switched|refresh' | sed 's/^/       /')"
case "$st" in *"switched: yes"*) pass "stats: switched=yes" ;; *) fail "stats: switched не yes" ;; esac
case "$st" in *"Current display refresh rate: 50.00Hz"*) pass "stats: текущая 50.00 Гц" ;; *) fail "stats: текущая не 50.00 Гц" ;; esac
case "$st" in *"Original display refresh rate: ${ORIG_HZ}Hz"*) pass "stats: исходная $ORIG_HZ Гц" ;; *) fail "stats: исходная не $ORIG_HZ Гц" ;; esac
stop_player
secs=$(wait_mode "$MODE0" 15)
[ "$(mode)" = "$MODE0" ] && pass "возврат $MODE0 за $secs с" || fail "после стопа $(mode), ожидался $MODE0"
say ""

# --- 3. 60 fps: режим не трогается -----------------------------------------
say "3. 60 fps ($C60)"
sleep 3		# возврат исходного режима предыдущего клипа успевает дописаться в журнал
mark=$(log_len)
play "$C60"
sleep 5
[ "$(mode)" = "1080p60hz" ] && pass "режим остался 1080p60hz" || fail "режим $(mode)"
# Переключением считаем только уход в ДРУГОЙ режим: строка о применении того же
# 1080p60hz — это возврат исходного режима от предыдущего клипа, а не реакция на
# 60-фпс контент (гонка в самом скрипте, а не дефект демона).
other=$(log_since "$mark" | grep 'Переключаю' | grep -v "на $MODE0$" | head -2)
[ -z "$other" ] && pass "переключений в другой режим нет" || fail "были переключения: $other"
stop_player
sleep 2
say ""

# --- 4. синтетика 23.976 ----------------------------------------------------
say "4. синтетика ($CSYNTH)"
mark=$(log_len)
play "$CSYNTH"
secs=$(wait_mode 1080p24hz "$WAIT_MAX")
if [ "$(mode)" = "1080p24hz" ]; then
	pass "режим 1080p24hz за $secs с"
else
	fail "режим $(mode), ожидался 1080p24hz"
fi
lines=$(log_since "$mark")
if [ "$LOG_OK" = 1 ]; then
	echo "$lines" | grep -qE 'быстрое решение|заявлено|правлю режим|по огибающей' && pass "решение есть в журнале" || fail "решения в журнале нет"
	echo "$lines" | sed 's/^/       | /' | grep -E 'быстрое решение|заявлено|правлю режим|по огибающей' | head -4
else
	skip "решение в журнале — журнал демона выключен"
fi
stop_player
secs=$(wait_mode "$MODE0" 15)
[ "$(mode)" = "$MODE0" ] && pass "возврат $MODE0 за $secs с" || fail "после стопа $(mode)"
say ""

# --- 5. fps.declare.fast=0: только замер ------------------------------------
say "5. fps.declare.fast=0 (решение только замером)"
su_ "sed -i s@^fps.declare.fast=.*@fps.declare.fast=0@ $INI" >/dev/null
restart_daemon
mark=$(log_len)
play "$C25"
secs=$(wait_mode 1080p50hz "$WAIT_MAX")
if [ "$(mode)" = "1080p50hz" ]; then
	pass "замер вывел 1080p50hz за $secs с"
else
	fail "режим $(mode), ожидался 1080p50hz"
fi
if [ "$LOG_OK" = 1 ]; then
	log_since "$mark" | grep -q 'быстрое решение' && fail "быстрый путь работал при fps.declare.fast=0" || pass "строк «быстрое решение» нет"
else
	skip "строк «быстрое решение» нет — журнал демона выключен"
fi
stop_player
wait_mode "$MODE0" 15 >/dev/null || fail "нет возврата после fps.declare.fast=0"
su_ "sed -i s@^fps.declare.fast=.*@fps.declare.fast=1@ $INI" >/dev/null
restart_daemon
say ""

# --- 6. помощник недоступен -------------------------------------------------
say "6. помощник недоступен (dex испорчен)"
mark=$(log_len)
su_ "echo МУСОР > $CACHE/afrd-disp.dex" >/dev/null
play "$C25"
# Ждём с запасом: драйвер на этом стенде заявляет 30 fps и для честных 25-фпс
# клипов, поэтому попытка применить режим приходит только от замера (~15 с).
sleep 26
lines=$(log_since "$mark")
attempts=$(echo "$lines" | grep -c 'Переключаю режим')
say "       попыток применения за 26 с: $attempts"
if [ "$LOG_OK" = 1 ]; then
	[ "$attempts" -ge 1 ] && pass "попытка применения была" || fail "попыток нет — проверять нечего"
	[ "$attempts" -le 2 ] && pass "повтор прекращён" || fail "попыток $attempts — повтор не ограничен"
	echo "$lines" | grep -qE 'не применил|не ответил|не пробую' && pass "предупреждение об отказе есть" || fail "нет предупреждения"
else
	skip "попытки и предупреждение помощника — журнал демона выключен"
fi
[ "$(mode)" = "$MODE0" ] && pass "режим не тронут" || fail "режим $(mode), ожидался $MODE0"
stop_player
restart_daemon
size=$(su_ "stat -c %s $CACHE/afrd-disp.dex" | tail -1)
say "       размер dex после перезапуска: $size"
if [ "${size:-0}" -gt 1000 ] 2>/dev/null; then pass "помощник восстановлен приложением"; else fail "помощник не восстановлен"; fi
say ""

# --- 7. помощник завис: демон не встаёт -------------------------------------
say "7. помощник завис (sleep 60) — демон обязан не вставать"
# Файл кладём push-ем с хоста: printf внутри su -c разбирается шеллом до su,
# из-за чего однажды получился пустой файл и «зависание» не проверялось вовсе.
printf '#!/system/bin/sh\nsleep 60\n' > /tmp/afrd-hang.sh
adb -s "$D" push /tmp/afrd-hang.sh /data/local/tmp/hang.sh >/dev/null 2>&1
rm -f /tmp/afrd-hang.sh
su_ "chmod 755 /data/local/tmp/hang.sh" >/dev/null
hsize=$(su_ "stat -c %s /data/local/tmp/hang.sh" | tail -1)
if [ "${hsize:-0}" -gt 10 ] 2>/dev/null; then pass "скрипт-заглушка на месте ($hsize байт)"; else fail "заглушка не создана ($hsize)"; fi
su_ "sed -i s@^vendor.disp.cli=.*@vendor.disp.cli=/data/local/tmp/hang.sh@ $INI" >/dev/null
sleep 4		# демон перечитывает конфиг и переходит на заглушку
mark=$(log_len)
play "$C25"
# Ждём не «12 секунд», а появления строки: попытка применить режим приходит от
# замера (~15 с на этом стенде, драйвер заявляет 30 fps и для 25-фпс клипа), а
# демон отвечает ещё через ~10 с — при фиксированных 12 с проверка ложно падала
# (живая находка 2026-10-15: строка появлялась на 10-й секунде ПОСЛЕ попытки).
HANG_WAIT=${HANG_WAIT:-40}
i=0
hang_line=""
while [ "$i" -lt "$HANG_WAIT" ]; do
	hang_line=$(log_since "$mark" | grep -m1 'не ответил за отведённое время')
	[ -n "$hang_line" ] && break
	sleep 2
	i=$((i + 2))
done
t0=$(date +%s%N)
st=$(su_ "$CACHE/afrd -s -p /dev/run/afrd.pid" | grep -E 'enabled|refresh rate is switched')
t1=$(date +%s%N)
say "       ответ afrd -s за $(( (t1 - t0) / 1000000 )) мс"
say "$(echo "$st" | sed 's/^/       /')"
echo "$st" | grep -qE 'enabled: yes' && pass "демон отвечает (не встал на зависшем помощнике)" || fail "демон не отвечает"
if [ "$LOG_OK" != 1 ]; then
	skip "предел времени в журнале — журнал демона выключен"
elif [ -n "$hang_line" ]; then
	pass "в журнале отмечен предел времени (за ${i} с ожидания)"
else
	fail "нет строки о пределе времени за ${HANG_WAIT} с"
fi
echo "$(log_since "$mark" | grep -E 'Переключаю|не ответил|не пробую' | tail -3)" | sed 's/^/       | /'
stop_player
restore_ini
su_ "rm -f /data/local/tmp/hang.sh" >/dev/null
restart_daemon
say ""

# --- 8. список режимов не читается -----------------------------------------
say "8. vendor.disp.cap не читается"
sh_ "am force-stop $PKG" >/dev/null
sleep 1
su_ "sed -i s@^vendor.disp.cap=.*@vendor.disp.cap=/nonexistent/disp_cap@ $INI" >/dev/null
mark=$(log_len)
sh_ "monkey -p $PKG -c android.intent.category.LAUNCHER 1" >/dev/null 2>&1
sleep 9
play "$C25"
sleep 12
lines=$(log_since "$mark")
warng=$(echo "$lines" | grep -c 'список режимов ТВ не прочитан')
say "       предупреждений: $warng"
if [ "$LOG_OK" != 1 ]; then
	skip "предупреждение о нечитаемом списке — журнал демона выключен"
elif [ "$warng" -ge 1 ] && [ "$warng" -le 3 ]; then
	pass "предупреждение есть и не потоком"
else
	fail "предупреждений $warng"
fi
[ "$(mode)" = "$MODE0" ] && pass "режим не тронут" || fail "режим $(mode), ожидался $MODE0"
say ""

# --- итог -------------------------------------------------------------------
restore
say "=============================================="
say "ИТОГ: OK=$OK FAIL=$FAIL SKIP=$SKIP"
say "восстановлено: режим $(mode), системный AFR $(su_ 'getprop persist.sys.autoframemode' | tail -1)"
[ "$FAIL" = 0 ]
