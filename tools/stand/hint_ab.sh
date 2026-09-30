#!/system/bin/sh
# AFRd2: A/B-проверка режимов на живом канале через API-подсказку.
#
# Зачем: на IPTV-потоках замер по счётчику кадров показывает темп декодирования
# (R51/README), поэтому «правильную» частоту сравнивают глазами: глаже та, что
# совпадает с фактической подачей кадров. Подсказка применяется и когда режим уже
# удержан (R53), так что канал можно переключать на ходу.
#
# Запуск (канал должен играть):
#   adb -s 192.168.88.50:5555 push tools/stand/hint_ab.sh /data/local/tmp/
#   adb -s 192.168.88.50:5555 shell 'sh /data/local/tmp/hint_ab.sh'
#   adb -s ... shell 'sh /data/local/tmp/hint_ab.sh 23976 25000 60000'   # свой список
#
# Скрипт исполняется НА БОКСЕ (как и остальные файлы /data/local/tmp), а не на
# хосте: он читает /sys/class/display/* и шлёт UDP в 127.0.0.1:50505.
#
# Аргументы — частоты в миллигерцах (23976 = 23.976), по умолчанию 24 -> 25 -> 60 -> 25.
# DWELL — сколько секунд держать каждый режим (по умолчанию 12).
DWELL=${DWELL:-12}
LIST=${*:-"23976 25000 60000 25000"}

mode_now () { cat /sys/class/display/mode; }
sync_now () { sed -n 's/^[[:space:]]*sync_duration_num[[:space:]]*:[[:space:]]*//p' /sys/class/display/vinfo | head -1; }

echo "# старт $(date +%H:%M:%S) mode=$(mode_now) sync=$(sync_now)"
for hz in $LIST; do
	echo "frame_rate_hint $hz" | timeout 2 nc -u 127.0.0.1 50505 >/dev/null 2>&1
	sleep 3
	echo "== $(date +%H:%M:%S)  подсказка $hz мГц -> mode=$(mode_now) sync=$(sync_now)"
	i=0
	while [ "$i" -lt "$DWELL" ]; do
		sleep 1
		i=$((i + 1))
	done
done
echo "# конец $(date +%H:%M:%S) mode=$(mode_now) sync=$(sync_now)"
