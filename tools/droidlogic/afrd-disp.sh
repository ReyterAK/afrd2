#!/system/bin/sh
# AFRd2: смена режима на Android 10/11 (Amlogic) через вендорный HAL SystemControl.
#
# Использование: afrd-disp.sh <имя режима, напр. 1080p24hz>
#
# Путь к dex — рядом со скриптом по умолчанию; переопределяется AFRD_DISP_DEX
# (приложению и Magisk-модулю удобнее положить их в свой каталог).
#
# Почему dex, а не нативный бинарь: HIDL-прокси SystemControl лежит в
# /system_ext/priv-app/droidlogic-res/droidlogic-res.apk этой прошивки, и его
# классы доступны штатному app_process (тем же способом работают cmd и dumpsys).
# Своей сборки HIDL-клиента это не требует и не «прошивает» ABI вендора.

MODE=$1
if [ -z "$MODE" ]; then
	echo "usage: $0 <mode>" >&2
	exit 2
fi

DEX=${AFRD_DISP_DEX:-/data/local/tmp/afrd-disp.dex}
RES=${AFRD_DISP_RES:-/system_ext/priv-app/droidlogic-res/droidlogic-res.apk}

CLASSPATH="$DEX:/system/framework/android.hidl.base-V1.0-java.jar:/system/framework/android.hidl.manager-V1.0-java.jar:$RES"
export CLASSPATH
exec app_process /system/bin AfrdDispMode "$MODE"
