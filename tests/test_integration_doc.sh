#!/bin/sh
# Документ для встраивания AFRd2 в прошивку (docs/integration-rom.md) обязан
# перечислять РОВНО те ключи, которые знает демон: это внешний документ, по
# нему настраивают чужую сборку, и устаревшая сводка обойдётся дороже теста.
#
# Источник правды — cfgkeys.c (та же таблица проверяет конфиг демона на старте,
# см. test_cfgkeys.c). Приложение А документа сверяется с ней как множество:
# лишний ключ в документе и пропущенный ключ в документе — оба случай ошибки.

set -e
cd "$(dirname "$0")/.."

DOC=docs/integration-rom.md
KEYS=cfgkeys.c
OUT=out/host-tests
mkdir -p "$OUT"

[ -f "$DOC" ] || { echo "нет $DOC"; exit 1; }
[ -f "$KEYS" ] || { echo "нет $KEYS"; exit 1; }

# Разделы, без которых документ теряет смысл (защита от усечения файла).
for anchor in "## 5. Минимальный набор настроек" "## 8. Штатная АПЧ" "## Приложение А" "## Приложение Б"; do
	grep -q "^$anchor" "$DOC" || { echo "$DOC: нет раздела «$anchor»"; exit 1; }
done

# Ключи из приложения А (таблица «| `ключ` | ... |»).
awk '/^## Приложение А/{ f = 1; next } /^## Приложение Б/{ f = 0 }
     f && /^\| `/ { line = $0; sub (/^\| `/, "", line); sub (/`.*/, "", line); print line }' \
	"$DOC" | sort -u > "$OUT/doc_keys.txt"

# Ключи, которые знает демон.
sed -n 's/.*{ *"\([^"]*\)".*/\1/p' "$KEYS" | sort -u > "$OUT/cfg_keys.txt"

if ! cmp -s "$OUT/doc_keys.txt" "$OUT/cfg_keys.txt"; then
	echo "$DOC: сводка ключей расходится с $KEYS"
	echo "--- только в документе:"
	comm -23 "$OUT/doc_keys.txt" "$OUT/cfg_keys.txt" | sed 's/^/    /'
	echo "--- только в коде:"
	comm -13 "$OUT/doc_keys.txt" "$OUT/cfg_keys.txt" | sed 's/^/    /'
	exit 1
fi

# Ключи, названные в таблице минимального набора (§5), обязаны быть в сводке:
# иначе разработчик прошивки увидит ключ, которого нет в приложении А.
# В той же таблице есть значения (например, пакет плеера) — их отсеиваем по
# признаку «токен вообще не ключ конфига» (нет в cfgkeys.c).
awk '/^## 5\./{ f = 1 } /^## 6\./{ f = 0 }
     f && /^\|/ { while (match ($0, /`[a-z][a-z0-9.]*`/)) {
         k = substr ($0, RSTART + 1, RLENGTH - 2); print k; $0 = substr ($0, RSTART + RLENGTH) } }' \
	"$DOC" | sort -u > "$OUT/doc_min_all.txt"

: > "$OUT/doc_min_keys.txt"
while read -r k; do
	if grep -qx "$k" "$OUT/cfg_keys.txt"; then
		echo "$k" >> "$OUT/doc_min_keys.txt"
	fi
done < "$OUT/doc_min_all.txt"

missing=
for k in $(cat "$OUT/doc_min_keys.txt"); do
	grep -qx "$k" "$OUT/doc_keys.txt" || missing="$missing $k"
done
[ -z "$missing" ] || { echo "$DOC: в таблице минимума ключи вне сводки:$missing"; exit 1; }

echo "integration-doc: OK ($(wc -l < "$OUT/doc_keys.txt" | tr -d ' ') ключей, сводка совпадает с $KEYS)"
