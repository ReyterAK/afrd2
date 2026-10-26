#!/system/bin/sh
# AFRd2: убрать «застрявшую» задачу плеера, созданную запуском по deep-link.
#
# Зачем. Запуск штатного YouTube на TV (`am start -a VIEW -d https://…/watch?v=…`)
# создаёт задачу с базовым намерением VIEW+URL (`isPersistable=true`). Такая
# задача переживает и перезагрузку, и `am force-stop`: при следующем запуске
# YouTube с иконки лаунчера приложение получает **старое** намерение
# (в журнале: `wm_new_intent … android.intent.action.VIEW … https://www.youtube.com/…`)
# и сразу начинает проигрывать тот же ролик — на главный экран не выйти.
# Это поведение самого клиента YouTube TV (`ShellActivity` → `MainActivity`),
# а не AFRd2; но создают такую задачу стендовые прогоны (yt_switch_probe,
# yt_halfrate_fix, stutter_probe, youtube_blind и т.п.), поэтому чистить её
# должен стенд (§58 журнала).
#
# Что делает. Находит в recents задачи пакета, базовое намерение которых —
# VIEW с http(s)-ссылкой, и снимает их (`am stack remove <taskId>`: подкоманды
# `remove` у `am task` на ATV14 нет). Идемпотентен: повторный вызов ничего не
# находит и печатает «нечего чистить».
#
# Запуск (root не нужен, на стенде `adb shell` = uid 0):
#   sh /data/local/tmp/yt_task_clean.sh [пакет]
#
# Код возврата: 0 — чисто (или почищено), 1 — задача осталась.

PKG=${1:-com.google.android.youtube.tv}
DUMP=/data/local/tmp/yt_task_clean.dump

# Идентификаторы задач пакета с базовым намерением VIEW+http(s).
# Строка блока: «  * Recent #N: Task{… #<taskId> type=… A=<uid>:<пакет>}» —
# номер после «Recent #» это индекс в списке, а не taskId, поэтому id берём
# только из части после «Task{».
find_ids () { # <файл-дампа>
	awk -v pkg="$PKG" '
		/^  \* Recent #[0-9]+: Task\{/ {
			rest = $0
			sub(/^.*Task\{/, "", rest)
			id = ""
			if (match(rest, /#[0-9]+/)) id = substr(rest, RSTART + 1, RLENGTH - 1)
			mine = (index($0, pkg) > 0)
			next
		}
		mine && /intent=\{/ && /act=android\.intent\.action\.VIEW/ && /dat=https?:/ { print id; mine = 0 }
	' "$1"
}

dumpsys activity recents > "$DUMP" 2>/dev/null || { echo "recents недоступны"; exit 1; }
ids=$(find_ids "$DUMP")

if [ -z "$ids" ]; then
	rm -f "$DUMP"
	echo "задач с deep-link нет — чистить нечего"
	exit 0
fi

for id in $ids; do
	am stack remove "$id" >/dev/null 2>&1
	# Проверка по факту, а не по коду возврата: `am stack remove` на
	# несуществующей задаче тоже может вернуть 0.
	dumpsys activity recents > "$DUMP" 2>/dev/null
	case " $(find_ids "$DUMP") " in
		*" $id "*) echo "задача $id осталась ($PKG)" ;;
		*)         echo "снята задача $id ($PKG)" ;;
	esac
done

left=$(find_ids "$DUMP")
rm -f "$DUMP"
if [ -n "$left" ]; then
	echo "остались задачи с deep-link: $left"
	exit 1
fi
exit 0
