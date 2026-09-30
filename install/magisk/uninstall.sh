#!/system/bin/sh
# AFRd2: удаление Magisk-модуля. Magisk запускает этот скрипт перед удалением
# каталога модуля — снимаем демон штатным SIGTERM, чтобы он вернул исходный режим
# и снял флаг backdoor (иначе флаг останется до перезагрузки/перезапуска SF).
MODDIR=${0%/*}
pkill -TERM -f "^$MODDIR/afrd" 2>/dev/null
sleep 2
pkill -KILL -f "^$MODDIR/afrd" 2>/dev/null
