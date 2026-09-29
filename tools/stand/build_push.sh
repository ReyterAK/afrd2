#!/bin/sh
set -e

# Корень репозитория — чтобы скрипт работал из любого CWD
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
D=${D:-192.168.88.50:5555}

# Идемпотентность: снять ранее запущенный демон и его файл ДО push.
# Иначе adb push в работающий бинарь падает с ETXTBSY (при set -e — аварийный
# выход), а pidof/tail ниже подтверждают работу старого бинаря.
adb -s "$D" shell 'pkill -f /data/local/tmp/afrd; rm -f /data/local/tmp/afrd' || true

adb -s "$D" push "$ROOT/libs/armeabi-v7a/afrd" /data/local/tmp/afrd
adb -s "$D" push "$ROOT/config/afrd2.ini" /data/local/tmp/afrd.ini
adb -s "$D" shell 'chmod 755 /data/local/tmp/afrd'
adb -s "$D" shell '/data/local/tmp/afrd -v -D /data/local/tmp/afrd.ini' &
sleep 3
adb -s "$D" shell 'pidof afrd && tail -20 /data/local/afrd.log'
