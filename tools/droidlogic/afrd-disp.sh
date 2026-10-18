#!/system/bin/sh
# AFRd2: смена режима на Android 10/11 (Amlogic) через вендорный HAL SystemControl.
#
# Использование: afrd-disp.sh <имя режима, напр. 1080p24hz>
#
# Путь к dex — рядом со скриптом по умолчанию; переопределяется AFRD_DISP_DEX
# (приложению и Magisk-модулю удобнее положить их в свой каталог).
#
# Почему dex, а не нативный бинарь: HIDL-прокси SystemControl лежит в
# droidlogic-res.apk вендора (на проверенной сборке — в /system_ext/priv-app),
# и его классы доступны штатному app_process (тем же способом работают cmd и
# dumpsys). Своей сборки HIDL-клиента это не требует и не «прошивает» ABI
# вендора.
#
# Коды возврата: 2 — не передан режим; 3 — сервис SystemControl недоступен;
# 4 — HAL вернул успех, но режим не сменился; 5 — узел режима не читается;
# 6 — не найден droidlogic-res.apk (некуда взять классы HIDL-прокси).
# Успех — только 0.

# Режим работы: по умолчанию — разовая смена (одно имя режима в аргументе).
# С `--serve` запускается постоянный помощник: он держит VM и классы HIDL
# загруженными и читает имена режимов построчно. Разовый запуск app_process
# стоит ~490 мс (замер 18.10 на ATV11) — постоянный помощник убирает эту часть
# из каждой смены режима (журнал §49.9).
SERVE=0
if [ "$1" = "--serve" ]; then
	SERVE=1
	shift
fi

MODE=$1
if [ "$SERVE" = 0 ] && [ -z "$MODE" ]; then
	echo "usage: $0 <mode> | --serve" >&2
	exit 2
fi

# Каталог скрипта: приложение и Magisk-модуль кладут dex РЯДОМ с ним (cache
# приложения, каталог модуля), поэтому по умолчанию ищем там же. Прежнее
# умолчание /data/local/tmp было ошибкой: на чистой системе dex там нет, и
# режим не переключался вовсе (нашло финальное ревью).
DIR=${0%/*}
[ -n "$DIR" ] && [ "$DIR" != "$0" ] || DIR=.
DEX=${AFRD_DISP_DEX:-$DIR/afrd-disp.dex}

# APK с классами HIDL-прокси вендора. Раздел /system_ext появился только в
# Android 11, поэтому на 10 и в части OEM-сборок тот же apk лежит в /system,
# /vendor или /product: перебираем кандидатов, а не полагаемся на один путь
# (прежний жёсткий путь system_ext означал, что на Android 10 демон уходил в
# пассивный режим). AFRD_DISP_RES задаёт один путь, AFRD_DISP_RES_DIRS — список
# каталогов (последнее — для проверок подмены раскладки).
RES=${AFRD_DISP_RES:-}
if [ -z "$RES" ]; then
	for d in ${AFRD_DISP_RES_DIRS:-/system_ext/priv-app/droidlogic-res /system/priv-app/droidlogic-res /vendor/priv-app/droidlogic-res /product/priv-app/droidlogic-res}; do
		if [ -f "$d/droidlogic-res.apk" ]; then
			RES="$d/droidlogic-res.apk"
			break
		fi
	done
fi
if [ -z "$RES" ]; then
	echo "droidlogic-res.apk not found (vendor HIDL proxy classes)" >&2
	exit 6
fi

CLASSPATH="$DEX:/system/framework/android.hidl.base-V1.0-java.jar:/system/framework/android.hidl.manager-V1.0-java.jar:$RES"
export CLASSPATH

# Код 3 — сервис SystemControl не зарегистрирован в hwservicemanager. На ATV11
# это бывает при живом процессе (lazy HAL не поднялся после сбоя): помощник
# сообщал «systemcontrol service not found», и AFR молча оставался без смены
# режима (живая находка 2026-10-17). Перезапуск сервиса лечит; делаем это один
# раз и повторяем вызов, чтобы отказ не выглядел как «сервиса нет вовсе».
if [ "$SERVE" = 1 ]; then
	exec app_process /system/bin AfrdDispServe
fi

app_process /system/bin AfrdDispMode "$MODE"
rc=$?
if [ "$rc" = "3" ]; then
	echo "SystemControl не зарегистрирован — перезапускаю сервис и повторяю" >&2
	setprop ctl.restart system_control 2>/dev/null
	i=0
	while [ "$i" -lt 20 ]; do
		sleep 0.5
		i=$((i + 1))
	done
	app_process /system/bin AfrdDispMode "$MODE"
	rc=$?
fi
exit $rc
