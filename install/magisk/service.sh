#!/system/bin/sh
# AFRd2: Magisk late_start service — поднимает демон после загрузки Android.
#
# Модуль НЕ трогает системные разделы: бинарник и конфиг лежат внутри самого
# модуля (/data/adb/modules/afrd2/), демон пишет лог по log.file из конфига
# (/data/local/afrd.log). Корень нужен демону для backdoor-транзакции
# SurfaceFlinger 1035 — в late_start он уже есть.
MODDIR=${0%/*}
BIN="$MODDIR/afrd"
INI="$MODDIR/afrd.ini"

# Права на запуск выставляем сами: установщик Magisk распаковывает файлы модуля
# без битов прав из zip (живая проверка 2026-10-08 на ATV14, Magisk 24.3: `afrd`
# лёг как 0644 и демон не поднялся — service.sh уходил в отказ ниже). Выставляем
# право явно, чтобы модуль работал независимо от того, что сделал установщик.
chmod 755 "$BIN" 2>/dev/null

# Вендорный помощник смены режима (Android 10/11, Amlogic): dex и обёртка лежат
# в модуле, путь к обёртке указываем демону ключом vendor.disp.cli (он в профиле
# может быть задан под другой каталог — правим). Без помощника демон на этих
# версиях уходит в пассивный режим, поэтому отсутствие файлов — не ошибка.
if [ -f "$MODDIR/afrd-disp.dex" ] && [ -f "$MODDIR/afrd-disp.sh" ]; then
	chmod 755 "$MODDIR/afrd-disp.sh" 2>/dev/null
	if grep -q '^vendor.disp.cli=' "$INI" 2>/dev/null; then
		sed -i "s|^vendor.disp.cli=.*|vendor.disp.cli=$MODDIR/afrd-disp.sh|" "$INI"
	else
		echo "vendor.disp.cli=$MODDIR/afrd-disp.sh" >> "$INI"
	fi
fi

if [ ! -x "$BIN" ]; then
	echo "afrd2: нет исполняемого $BIN — модуль собран неверно" > /dev/kmsg 2>/dev/null || true
	exit 1
fi

# Ждём завершения загрузки (иначе SurfaceFlinger и дисплей ещё не готовы).
i=0
while [ "$(getprop sys.boot_completed)" != "1" ] && [ "$i" -lt 180 ]; do
	sleep 1
	i=$(( i + 1 ))
done
# небольшая пауза: дать подняться SurfaceFlinger и прочитать EDID
sleep 8

# Один экземпляр. Сначала прошлый НАДЗОР (service.sh могут запустить повторно —
# обновление модуля, ручной вызов): иначе поднялись бы два цикла и два демона.
# Свой pid и родителя исключаем: шаблон совпадает и с нашей командной строкой.
for p in $(pgrep -f "afrd2/servi[c]e\.sh" 2>/dev/null); do
	[ "$p" = "$$" ] && continue
	[ "$p" = "$PPID" ] && continue
	kill "$p" 2>/dev/null
done
sleep 1
rm -f /dev/run/afrd.supervisor 2>/dev/null

# Затем демон (если остался) — штатным SIGTERM, чтобы он вернул исходный режим
# и снял флаг backdoor.
pkill -TERM -f "^$BIN" 2>/dev/null
sleep 2

# Надзор за демоном. Он нужен не ради «чтобы работало»: демон держит режим
# флагом SurfaceFlinger, и если его убьют (kill -9, паника ядра), режим
# остаётся удержанным до перезагрузки — сам SurfaceFlinger его не сбрасывает
# (живая проверка 2026-10-08). Поднятый заново демон возвращает режим по
# «записке» в разделяемой памяти (/dev/run/afrd.ipc): см. atv14_recover_stale.
# Приложению для этого хватает своего сторожа; модуль без приложения иначе
# оставлял бы экран в низкой частоте.
#
# AFRD_ONESHOT=1 — запустить демон один раз и не следить за ним (так проверяет
# контракт запуска host-тест tests/test_magisk_module.sh).
if [ "${AFRD_ONESHOT:-0}" = "1" ]; then
	setsid "$BIN" -D "$INI" >/dev/null 2>&1 &
else
	(
		while true; do
			# Без -D: процесс остаётся нашим потомком, и цикл видит его
			# смерть. С -D демон ушёл бы в фон, а цикл поднимал бы копии.
			"$BIN" "$INI" >/dev/null 2>&1 &
			wait $!
			sleep 5
		done
	) >/dev/null 2>&1 &
	mkdir -p /dev/run 2>/dev/null
	echo $! > /dev/run/afrd.supervisor 2>/dev/null || true
fi
