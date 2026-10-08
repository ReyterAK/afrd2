#!/bin/sh
# AFRd2: сборка Magisk-модуля из собранного демона и профиля.
#
# Модуль НЕ устанавливается этим скриптом — только собирается zip-архив, который
# потом ставится через приложение Magisk (Modules → Install from storage).
# Установка меняет ОС бокса, поэтому делается только по решению владельца.
#
# Использование: sh install/magisk/build-module.sh [abi]
#   abi — armeabi-v7a (по умолчанию — как у стенда) или arm64-v8a
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ABI=${1:-armeabi-v7a}
BIN="$ROOT/libs/$ABI/afrd"
INI="$ROOT/config/afrd2.ini"
OUT="$ROOT/out/magisk"
ZIP="$ROOT/out/afrd2-magisk-$ABI.zip"

[ -x "$BIN" ] || { echo "нет $BIN — сначала соберите: ./mka (ABI $ABI)" >&2; exit 1; }
[ -f "$INI" ] || { echo "нет $INI" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT"
cp "$ROOT/install/magisk/module.prop" "$OUT/"
cp "$ROOT/install/magisk/service.sh" "$OUT/"
cp "$ROOT/install/magisk/uninstall.sh" "$OUT/"
cp "$BIN" "$OUT/afrd"
cp "$INI" "$OUT/afrd.ini"
chmod 755 "$OUT/service.sh" "$OUT/uninstall.sh" "$OUT/afrd"

# Вендорный помощник смены режима (Android 10/11): dex и обёртка едут вместе с
# модулем, service.sh раскладывает их и указывает путь в конфиге. Без него
# демон на 10/11 честно уходит в пассивный режим, поэтому сборка модуля без
# помощника — предупреждение, а не ошибка.
DISP="$ROOT/tools/droidlogic"
if [ -f "$DISP/afrd-disp.dex" ] && [ -f "$DISP/afrd-disp.sh" ]; then
	cp "$DISP/afrd-disp.dex" "$OUT/afrd-disp.dex"
	cp "$DISP/afrd-disp.sh" "$OUT/afrd-disp.sh"
	chmod 755 "$OUT/afrd-disp.sh"
else
	echo "ВНИМАНИЕ: нет $DISP/afrd-disp.{dex,sh} — модуль без смены режима на Android 10/11" >&2
	echo "           соберите dex: sh tools/droidlogic/build-dex.sh" >&2
fi

mkdir -p "$ROOT/out"
rm -f "$ZIP"
( cd "$OUT" && zip -qr "$ZIP" . )

echo "готово: $ZIP"
echo "установка: Magisk → Modules → Install from storage → выбрать этот zip → перезагрузка"
