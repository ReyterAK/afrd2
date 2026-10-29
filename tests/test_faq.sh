#!/bin/sh
# ЧАВО, которое видит пользователь, обязано разбираться парсером приложения.
#
# Формат (FAQFragment.loadFaq): строка вопроса начинается с '?', строка ответа —
# с '!', пустая строка разделяет абзацы ответа; строка с другим первым символом
# молча игнорируется, а вопрос, оставшийся без ответа, показывает пустую страницу.
# Русский и английский варианты должны описывать одно и то же число вопросов.

set -e
cd "$(dirname "$0")/.."

check () { # check <файл> <ожидаемое число вопросов>
	file=$1
	expect=$2
	awk -v file="$file" -v expect="$expect" '
		BEGIN { mode = 0; q = 0; qn = 0; bad = 0 }
		/^[[:space:]]*$/ { if (mode == 2) mode = 3; next }
		{
			line = $0
			sub (/^[[:space:]]+/, "", line)
			t = substr (line, 1, 1)
			body = substr (line, 2)
			gsub (/^[[:space:]]+/, "", body)
			if (t == "?") {
				if (mode != 1) {
					if (q > 0 && qn == 0) {
						printf "%s: вопрос без ответа\n", file
						bad = 1
					}
					q++
					qn = 0
				}
				mode = 1
				if (body == "") { printf "%s: пустая строка вопроса\n", file; bad = 1 }
			} else if (t == "!") {
				if (mode == 0) { printf "%s: ответ до первого вопроса\n", file; bad = 1 }
				mode = 2
				qn++
				if (body == "") { printf "%s: пустая строка ответа\n", file; bad = 1 }
			} else {
				printf "%s: строка не начинается с ? или !: %s\n", file, substr (line, 1, 40)
				bad = 1
			}
		}
		END {
			if (q > 0 && qn == 0) { printf "%s: вопрос без ответа\n", file; bad = 1 }
			if (q != expect) {
				printf "%s: вопросов %d, ожидалось %d\n", file, q, expect
				bad = 1
			}
			exit bad
		}
	' "$file" || exit 1
}

check FAQ-ru.txt 20
check FAQ-en.txt 20

echo "faq: OK (20 вопросов в каждом языке)"
