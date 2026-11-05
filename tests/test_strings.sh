#!/bin/sh
# Подсказки ListPreference форматируются фреймворком:
# ListPreference.getSummary() делает String.format(summary, entry). Одиночный
# «%» в такой подсказке — не спецификатор, и приложение падает с
# UnknownFormatConversionException (живой случай `.42`, 06.11: «5,7 %» в
# подсказке поправки AV1 — экран «Плееры» вываливался).
#
# Проверяем ТОЛЬКО подсказки, привязанные к *ListPreference: у остальных
# типов (XEditTextPreference, XAppsPreference) подсказка выводится как есть,
# и «%» там законен. Допустимы «%%», «%s» и позиционные «%1$s».
set -e
cd "$(dirname "$0")/.."

LIST=$(awk '
	BEGIN { in_tag = 0; s = "" }
	{
		if (!in_tag && $0 ~ /ListPreference/) { in_tag = 1; s = $0 }
		else if (in_tag) { s = s " " $0 }

		if (in_tag && $0 ~ /\/>/) {
			if (match (s, /summary="@string\/[A-Za-z0-9_]+"/)) {
				t = substr (s, RSTART, RLENGTH)
				sub (/.*@string\//, "", t)
				sub (/"$/, "", t)
				print t
			}
			in_tag = 0
			s = ""
		}
	}' android/app/src/main/res/xml/preferences.xml | sort -u)

[ -n "$LIST" ] || { echo "strings: не найдено ни одной ListPreference"; exit 1; }

bad=0
for f in android/app/src/main/res/values/strings.xml android/app/src/main/res/values-ru/strings.xml; do
	awk -v list="$LIST" '
		BEGIN {
			n = split (list, a, "\n")
			for (i = 1; i <= n; i++) want [a [i]] = 1
			in_str = 0; s = ""; name = ""; bad = 0
		}
		{
			if (!in_str && $0 ~ /<string name="/) {
				in_str = 1; s = $0
				name = $0
				sub (/.*<string name="/, "", name)
				sub (/".*/, "", name)
			} else if (in_str) {
				s = s " " $0
			}

			if (in_str && $0 ~ /<\/string>/) {
				if (want [name]) {
					t = s
					gsub (/%%/, "", t)
					gsub (/%[0-9]+\$s/, "", t)
					gsub (/%s/, "", t)
					if (index (t, "%") > 0) {
						printf "%s: одиночный %% в подсказке %s\n", FILENAME, name
						bad = 1
					}
				}
				in_str = 0; s = ""; name = ""
			}
		}
		END { exit bad }
	' "$f" || bad=1
done

[ "$bad" = 0 ] || { echo "strings: FAIL"; exit 1; }
echo "strings: OK"
