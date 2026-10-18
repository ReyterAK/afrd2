#!/bin/sh
# Разведка незнакомого Android TV бокса: сможет ли на нём работать AFRd2
# и каким путём (журнал §50).
#
# Использование:
#   sh tools/stand/recon_box.sh [адрес]
# Адрес по умолчанию — первый подключённый adb-устройства.
#
# Что проверяется (всё — теми же путями, которыми пользуется демон):
#   * платформа и разрядность (путь демона и ABI);
#   * корень (без него AFRd2 не работает вовсе);
#   * интерфейсы декодера (источник fps) — /sys/class/vdec/*;
#   * приёмник режима: backdoor SurfaceFlinger (транзакция 1035) или
#     вендорный SystemControl Amlogic (Android 10/11) или запись в mode;
#   * признаки HDMI и таблица режимов SurfaceFlinger;
#   * вспомогательные узлы (цвет, HDR, fr_hint).
# Ничего не меняет: единственная запись — проба backdoor с неверным id, которая
# если backdoor есть, снимает флаг «режим задан снаружи» (демон делает то же).

D=${1:-}
if [ -z "$D" ]; then
	D=$(adb devices | awk 'NR>1 && $2 == "device" { print $1; exit }')
	[ -z "$D" ] && { echo "нет подключённых adb-устройств" >&2; exit 3; }
fi

dev () { adb -s "$D" shell "$@" 2>/dev/null | tr -d '\r'; }
rt ()  { adb -s "$D" shell "su -c \"$1\"" 2>/dev/null | tr -d '\r'; }
has () { [ "$(dev "test -e $1 && echo yes")" = yes ]; }
val () { dev "getprop $1"; }
ok ()  { printf '  [x] %s\n' "$1"; }
no ()  { printf '  [ ] %s\n' "$1"; }

echo "== устройство $D"
MODEL=$(val ro.product.model)
DEV=$(val ro.product.device)
PLAT=$(val ro.board.platform)
HW=$(val ro.hardware)
SDK=$(val ro.build.version.sdk)
ABI=$(val ro.product.cpu.abilist)
KERN=$(dev uname -r)
DESC=$(val ro.build.description)
printf '  модель: %s (%s)\n' "$MODEL" "$DEV"
printf '  платформа: %s, hardware: %s\n' "$PLAT" "$HW"
printf '  Android SDK %s, ядро %s\n' "$SDK" "$KERN"
printf '  ABI: %s\n' "$ABI"
printf '  сборка: %s\n' "$DESC"
printf '  slimBOXtv: %s\n' "$(dev 'grep -c "slimBOXtv" /system/build.prop' 2>/dev/null)"

echo
echo "== root"
if [ "$(rt id | grep -c 'uid=0')" = 1 ]; then
	ok "su работает (uid 0)"
else
	no "su недоступен — AFRd2 не запустится (нужен root)"
fi
printf '  Magisk: %s\n' "$(dev 'test -d /data/adb/modules && echo да || echo нет')"

echo
echo "== разрядность userspace (путь к бинарнику демона)"
if [ "$(dev 'test -d /system/lib64 && echo yes')" = yes ]; then
	echo "  64-бит (есть /system/lib64) -> arm64-v8a"
else
	echo "  32-бит (нет /system/lib64) -> armeabi-v7a"
fi

echo
echo "== декодер: источник частоты кадров"
if has /sys/class/vdec/vdec_status; then
	ok "/sys/class/vdec/vdec_status"
	printf '  содержимое: %s\n' "$(rt 'cat /sys/class/vdec/vdec_status' | head -3 | tr '\n' ' ')"
	[ "$(dev 'test -e /sys/class/vdec/dump_vdec_chunks && echo yes')" = yes ] && \
		echo "  (есть dump_vdec_chunks/dump_vdec_blocks — демон их не читает: риск паники, журнал §4)"
else
	no "/sys/class/vdec/vdec_status — нет источника fps от декодера"
fi
for p in /sys/class/vdec /sys/kernel/debug/vdec /proc/amvdec; do
	[ "$(dev "test -d $p && echo yes")" = yes ] && echo "  есть каталог $p"
done

echo
echo "== приёмник смены режима"
# 1) backdoor SurfaceFlinger (Android 12+)
OUT=$(dev 'service call SurfaceFlinger 1035 i32 -1')
case "$OUT" in
*"Invalid argument"*) ok "backdoor SurfaceFlinger 1035 (setActiveModeFromBackdoor)"; BACKDOOR=1 ;;
*"Result:"*)          no "транзакция 1035 есть, но значит другое: $OUT"; BACKDOOR=0 ;;
*)                    no "backdoor 1035 недоступен: $OUT"; BACKDOOR=0 ;;
esac
# 2) вендорный SystemControl (Amlogic 10/11)
if [ "$(dev 'lshal 2>/dev/null | grep -c "systemcontrol@1.1"')" != 0 ]; then
	ok "вендорный SystemControl @1.1 (Android 10/11)"
	[ "$(dev 'test -f /system_ext/priv-app/droidlogic-res/droidlogic-res.apk || test -f /system/priv-app/droidlogic-res/droidlogic-res.apk && echo yes')" = yes ] && \
		echo "  droidlogic-res.apk на месте (нужен для HIDL-классов)"
else
	no "вендорного SystemControl нет"
fi
# 3) прямая запись в mode (только чтение состояния на новых ядрах)
if has /sys/class/display/mode; then
	ok "/sys/class/display/mode (текущий режим: $(dev 'cat /sys/class/display/mode'))"
else
	no "/sys/class/display/mode — нет"
fi
[ "$(dev 'test -f /vendor/bin/meson_display_client && echo yes')" = yes ] && \
	echo "  есть /vendor/bin/meson_display_client (цвет/HDR)"

echo
echo "== таблица режимов SurfaceFlinger"
printf '  режимов 1920x1080: %s\n' "$(dev 'dumpsys SurfaceFlinger 2>/dev/null | grep -E "^ *[{]id=" | grep -c 1920x1080')"
printf '  activeMode: %s\n' "$(dev 'dumpsys SurfaceFlinger 2>/dev/null | grep -m1 -o "activeMode=[0-9.]*"')"
printf '  режимов всего: %s\n' "$(dev 'dumpsys SurfaceFlinger 2>/dev/null | grep -cE "^ *[{]id="')"

echo
echo "== HDMI и вспомогательные узлы"
for p in /sys/class/extcon/extcon0/state /sys/class/switch/hdmi/state \
         /sys/class/switch/hdmi/cable.0/state /sys/class/display/vinfo \
         /sys/class/display/fr_hint /sys/class/display/fr_policy \
         /sys/class/amhdmitx/amhdmitx0/disp_cap /sys/class/amhdmitx/amhdmitx0/attr; do
	if has "$p"; then
		ok "$p"
	else
		no "$p"
	fi
done

echo
echo "== вердикт"
if [ "$BACKDOOR" = 1 ] && has /sys/class/vdec/vdec_status; then
	echo "  Полная поддержка: путь ATV14 (опрос декодера + backdoor 1035)."
elif [ "$BACKDOOR" = 1 ]; then
	echo "  Частично: backdoor есть, но нет источника fps от декодера."
	echo "  Демон сможет переключать режим только по подсказке плеера (API) —"
	echo "  для работы «из коробки» нужен другой источник частоты."
elif [ "$(dev 'lshal 2>/dev/null | grep -c "systemcontrol@1.1"')" != 0 ]; then
	echo "  Путь Android 10/11 (вендорный SystemControl) — нужен Amlogic и vdec_status."
elif has /sys/class/switch/hdmi/state || has /sys/class/switch/hdmi/cable.0/state; then
	echo "  Старый путь (Android 6-9): uevent + запись в /sys/class/display/mode."
else
	echo "  Не поддерживается: ни backdoor, ни вендорного переключателя, ни старого пути."
fi
