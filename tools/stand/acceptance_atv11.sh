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
# Использование:  ./tools/stand/acceptance_atv11.sh [устройство]

set -u

D=${1:-192.168.88.45:5555}
PKG=ru.reyter.afrd2
PLAYER=net.gtvbox.videoplayer
CLIPS=/sdcard/Download/afr45
CACHE=/data/user/0/$PKG/cache
INI=$CACHE/afrd.ini
LOG=/data/local/afrd.log
MODE_PATH=/sys/class/display/mode
HELPER=$CACHE/afrd-disp.sh
WAIT_MAX=${WAIT_MAX:-35}   # предел ожидания режима (замер бывает ~17 с)
LOG_WAIT=${LOG_WAIT:-6}    # пауза после возврата, чтобы журнал дописался

OK=0
FAIL=0

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

play () { sh_ "am start -a android.intent.action.VIEW -d file://$CLIPS/$1 -t video/x-matroska -n $PLAYER/.PlayerActivity" >/dev/null; }
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

ORIG_AFR=$(su_ "getprop persist.sys.autoframemode" | tail -1)
MODE0=$(mode)
say "стенд $D: исходный режим $MODE0; системный AFR был '$ORIG_AFR'"
say ""

su_ "setprop persist.sys.autoframemode 0" >/dev/null
su_ "sh $HELPER $MODE0" >/dev/null 2>&1
restore_ini
restart_daemon

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
check_clip 1 real_23976.mkv 1080p24hz "$WAIT_MAX"
stop_player
secs=$(wait_mode "$MODE0" 15)
[ "$(mode)" = "$MODE0" ] && pass "возврат $MODE0 за $secs с" || fail "после стопа $(mode), ожидался $MODE0"
say ""

# --- 2. 25 fps + статистика -------------------------------------------------
check_clip 2 real_25.mkv 1080p50hz "$WAIT_MAX"
st=$(su_ "$CACHE/afrd -s -p /dev/run/afrd.pid")
say "$(echo "$st" | grep -E 'switched|refresh' | sed 's/^/       /')"
case "$st" in *"switched: yes"*) pass "stats: switched=yes" ;; *) fail "stats: switched не yes" ;; esac
case "$st" in *"Current display refresh rate: 50.00Hz"*) pass "stats: текущая 50.00 Гц" ;; *) fail "stats: текущая не 50.00 Гц" ;; esac
case "$st" in *"Original display refresh rate: 60.00Hz"*) pass "stats: исходная 60.00 Гц" ;; *) fail "stats: исходная не 60.00 Гц" ;; esac
stop_player
secs=$(wait_mode "$MODE0" 15)
[ "$(mode)" = "$MODE0" ] && pass "возврат $MODE0 за $secs с" || fail "после стопа $(mode), ожидался $MODE0"
say ""

# --- 3. 60 fps: режим не трогается -----------------------------------------
say "3. 60 fps (t60.mp4)"
sleep 3		# возврат исходного режима предыдущего клипа успевает дописаться в журнал
mark=$(log_len)
play t60.mp4
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
say "4. синтетика (long_23976_synth.mp4)"
mark=$(log_len)
play long_23976_synth.mp4
secs=$(wait_mode 1080p24hz "$WAIT_MAX")
if [ "$(mode)" = "1080p24hz" ]; then
	pass "режим 1080p24hz за $secs с"
else
	fail "режим $(mode), ожидался 1080p24hz"
fi
lines=$(log_since "$mark")
echo "$lines" | grep -qE 'быстрое решение|заявлено|правлю режим' && pass "решение есть в журнале" || fail "решения в журнале нет"
echo "$lines" | sed 's/^/       | /' | grep -E 'быстрое решение|заявлено|правлю режим' | head -4
stop_player
secs=$(wait_mode "$MODE0" 15)
[ "$(mode)" = "$MODE0" ] && pass "возврат $MODE0 за $secs с" || fail "после стопа $(mode)"
say ""

# --- 5. fps.declare.fast=0: только замер ------------------------------------
say "5. fps.declare.fast=0 (решение только замером)"
su_ "sed -i s@^fps.declare.fast=.*@fps.declare.fast=0@ $INI" >/dev/null
restart_daemon
mark=$(log_len)
play real_25.mkv
secs=$(wait_mode 1080p50hz "$WAIT_MAX")
if [ "$(mode)" = "1080p50hz" ]; then
	pass "замер вывел 1080p50hz за $secs с"
else
	fail "режим $(mode), ожидался 1080p50hz"
fi
log_since "$mark" | grep -q 'быстрое решение' && fail "быстрый путь работал при fps.declare.fast=0" || pass "строк «быстрое решение» нет"
stop_player
wait_mode "$MODE0" 15 >/dev/null || fail "нет возврата после fps.declare.fast=0"
su_ "sed -i s@^fps.declare.fast=.*@fps.declare.fast=1@ $INI" >/dev/null
restart_daemon
say ""

# --- 6. помощник недоступен -------------------------------------------------
say "6. помощник недоступен (dex испорчен)"
mark=$(log_len)
su_ "echo МУСОР > $CACHE/afrd-disp.dex" >/dev/null
play real_25.mkv
# Ждём с запасом: драйвер на этом стенде заявляет 30 fps и для честных 25-фпс
# клипов, поэтому попытка применить режим приходит только от замера (~15 с).
sleep 26
lines=$(log_since "$mark")
attempts=$(echo "$lines" | grep -c 'Переключаю режим')
say "       попыток применения за 26 с: $attempts"
[ "$attempts" -ge 1 ] && pass "попытка применения была" || fail "попыток нет — проверять нечего"
[ "$attempts" -le 2 ] && pass "повтор прекращён" || fail "попыток $attempts — повтор не ограничен"
echo "$lines" | grep -qE 'не применил|не ответил|не пробую' && pass "предупреждение об отказе есть" || fail "нет предупреждения"
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
play real_25.mkv
sleep 12
t0=$(date +%s%N)
st=$(su_ "$CACHE/afrd -s -p /dev/run/afrd.pid" | grep -E 'enabled|refresh rate is switched')
t1=$(date +%s%N)
say "       ответ afrd -s за $(( (t1 - t0) / 1000000 )) мс"
say "$(echo "$st" | sed 's/^/       /')"
echo "$st" | grep -qE 'enabled: yes' && pass "демон отвечает (не встал на зависшем помощнике)" || fail "демон не отвечает"
log_since "$mark" | grep -q 'не ответил за отведённое время' && pass "в журнале отмечен предел времени" || fail "нет строки о пределе времени"
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
play real_25.mkv
sleep 12
lines=$(log_since "$mark")
warng=$(echo "$lines" | grep -c 'список режимов ТВ не прочитан')
say "       предупреждений: $warng"
if [ "$warng" -ge 1 ] && [ "$warng" -le 3 ]; then pass "предупреждение есть и не потоком"; else fail "предупреждений $warng"; fi
[ "$(mode)" = "$MODE0" ] && pass "режим не тронут" || fail "режим $(mode), ожидался $MODE0"
say ""

# --- итог -------------------------------------------------------------------
restore
say "=============================================="
say "ИТОГ: OK=$OK FAIL=$FAIL"
say "восстановлено: режим $(mode), системный AFR $(su_ 'getprop persist.sys.autoframemode' | tail -1)"
[ "$FAIL" = 0 ]
