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

# Один экземпляр: предыдущий (если остался) снимаем штатным SIGTERM —
# он сам вернёт исходный режим и снимет флаг backdoor.
pkill -TERM -f "^$BIN" 2>/dev/null
sleep 2

setsid "$BIN" -D "$INI" >/dev/null 2>&1 &
