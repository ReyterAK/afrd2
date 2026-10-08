#!/bin/sh
# Проверка контракта запуска Magisk-модуля: service.sh обязан сам выставить право
# на запуск демона и запустить его.
#
# Почему это важно (живой дефект 2026-10-08, ATV14, Magisk 24.3): установщик
# Magisk распаковывает файлы модуля БЕЗ битов прав из zip — `afrd` лёг как 0644,
# service.sh уходил в ветку «нет исполняемого» и писал отказ в dmesg, демон не
# поднимался после перезагрузки. Проверка zip на 0755 такой дефект не ловит:
# в архиве права были верные.
#
# Тест запускает настоящий service.sh в поддельном MODDIR с неисполняемым
# бинарником-заглушкой и окружением без Android (getprop/sleep/setsid/pkill —
# заглушки). Ожидание: бинарник стал исполняемым и был запущен.

set -e
cd "$(dirname "$0")/.."

fail () { echo "test_magisk_module: $1" >&2; exit 1; }

WORK=out/host-tests/magisk
MODDIR="$WORK/module"
STUBS="$WORK/bin"
rm -rf "$WORK"
mkdir -p "$MODDIR" "$STUBS"

cp install/magisk/service.sh "$MODDIR/service.sh"
cp install/magisk/uninstall.sh "$MODDIR/uninstall.sh"
cp install/magisk/module.prop "$MODDIR/module.prop"
cp config/afrd2.ini "$MODDIR/afrd.ini"

# Заглушка демона: отмечает факт запуска рядом с собой. Именно 0644 — как после
# установки модуля Magisk'ом (проверено живьём).
printf '#!/bin/sh\necho "args: $*" > "$(dirname "$0")/launched"\n' > "$MODDIR/afrd"
chmod 644 "$MODDIR/afrd"
[ -x "$MODDIR/afrd" ] && fail "заглушка неожиданно исполняемая — сценарий не воспроизводит дефект"

# Заглушки окружения: загрузка считается завершённой, ожидания не тратят время.
printf '#!/bin/sh\necho 1\n' > "$STUBS/getprop"
printf '#!/bin/sh\nexit 0\n' > "$STUBS/sleep"
printf '#!/bin/sh\nexit 1\n' > "$STUBS/pkill"
printf '#!/bin/sh\nexec "$@"\n' > "$STUBS/setsid"
chmod 755 "$STUBS"/*

rc=0
AFRD_ONESHOT=1 PATH="$STUBS:$PATH" sh "$MODDIR/service.sh" || rc=$?

[ -x "$MODDIR/afrd" ] ||
	fail "service.sh не выставил право на запуск: файлы модуля не запустятся после установки"

[ "$rc" -eq 0 ] ||
	fail "service.sh завершился с ошибкой (rc=$rc)"

# Демон запускается в фоне (`setsid ... &`), поэтому ждём появления отметки.
i=0
while [ ! -f "$MODDIR/launched" ] && [ "$i" -lt 50 ]; do
	sleep 0.1
	i=$((i + 1))
done

[ -f "$MODDIR/launched" ] ||
	fail "service.sh не запустил демон"

grep -q -- "-D $MODDIR/afrd.ini" "$MODDIR/launched" ||
	fail "демон запущен без своего конфига: $(cat "$MODDIR/launched")"

echo "test_magisk_module: ok"
