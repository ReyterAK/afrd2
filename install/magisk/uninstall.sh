#!/system/bin/sh
# AFRd2: удаление Magisk-модуля. Magisk запускает этот скрипт перед удалением
# каталога модуля — снимаем демон штатным SIGTERM, чтобы он вернул исходный режим
# и снял флаг backdoor (иначе флаг останется до перезагрузки/перезапуска SF).
MODDIR=${0%/*}

# Сначала надзор за демоном: иначе цикл из service.sh поднял бы снятый демон
# заново прямо во время удаления модуля. Шаблон ищет путь модуля ГДЕ УГОДНО в
# командной строке, а не с начала: Magisk запускает скрипты как
# `busybox sh /data/adb/modules/afrd2/service.sh`, и привязка к началу строки
# оставляла цикл живым (живая проверка 2026-10-08 — демон возвращался после
# удаления модуля).
pkill -f "$MODDIR/service.sh" 2>/dev/null
sleep 1
if [ -f /dev/run/afrd.supervisor ]; then
	kill "$(cat /dev/run/afrd.supervisor)" 2>/dev/null
	rm -f /dev/run/afrd.supervisor
fi

pkill -TERM -f "^$MODDIR/afrd" 2>/dev/null
sleep 2
pkill -KILL -f "^$MODDIR/afrd" 2>/dev/null
