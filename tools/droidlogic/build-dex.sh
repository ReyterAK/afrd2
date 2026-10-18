#!/bin/sh
# Сборка dex для смены режима на Android 10/11 (см. AfrdDispMode.java).
#
# Требуется d8 из Android SDK build-tools и android.jar (любой >= 27).
# Использование: sh tools/droidlogic/build-dex.sh
set -e

DIR=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$DIR/afrd-disp.dex}
D8=${D8:-$(ls "$HOME"/Android/Sdk/build-tools/*/d8 2>/dev/null | sort -V | tail -1)}
LIB=${LIB:-$(ls "$HOME"/Android/Sdk/platforms/android-*/android.jar 2>/dev/null | sort -V | tail -1)}

[ -n "$D8" ] && [ -x "$D8" ] || { echo "нет d8 (укажите D8=...)" >&2; exit 1; }
[ -n "$LIB" ] && [ -f "$LIB" ] || { echo "нет android.jar (укажите LIB=...)" >&2; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# class-файл версии 52 (Java 8): d8 из build-tools 28 иначе не читает вывод
# современного javac («Compilation failed with an internal error»).
javac --release 8 -nowarn -d "$TMP" "$DIR/AfrdDispMode.java" "$DIR/AfrdDispServe.java"
# min-api 23 (Android 6), а не 30: dex запускается и на Android 10, и прежнее
# значение ничего не давало — только сужало применимость без нужды.
"$D8" --min-api 23 --lib "$LIB" --output "$TMP" "$TMP/AfrdDispMode.class" "$TMP/AfrdDispServe.class"

cp "$TMP/classes.dex" "$OUT"
echo "готово: $OUT ($(wc -c < "$OUT") байт)"
