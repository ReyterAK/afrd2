#!/bin/sh
set -e
D=${D:-192.168.88.50:5555}
adb -s $D push libs/armeabi-v7a/afrd /data/local/tmp/afrd
adb -s $D push config/afrd2.ini /data/local/tmp/afrd.ini
adb -s $D shell 'chmod 755 /data/local/tmp/afrd'
adb -s $D shell '/data/local/tmp/afrd -v -D /data/local/tmp/afrd.ini' &
sleep 3
adb -s $D shell 'pidof afrd && tail -20 /data/local/afrd.log'
