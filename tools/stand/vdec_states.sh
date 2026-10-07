#!/bin/sh
# Стенд: чем отличаются состояния декодера «играет / пауза / видео остановлено,
# а канал залип». Нужно, чтобы решить, можно ли отличить «настоящую паузу» от
# «залипшего канала» без эвристик (журнал §31.18, случай 3 сторожа).
#
# Скрипт печатает временной ряд: на каждой пробе — поля канала vdec_status
# (device/frames/status/frame dur), приложение на переднем плане, состояние
# media_session и наличие видео-слоя в SurfaceFlinger. Порядок работы:
#
#   1. Запустить скрипт (он снимает пробы N секунд).
#   2. Пока он идёт — НЕ трогая пульт, дождаться, пока демон переключит режим
#      (в журнале «Переключаю режим»), затем:
#        ~1/3 времени — видео играет;
#        ~1/3 — пауза в плеере (кнопка паузы, не выход);
#        ~1/3 — остановить видео, оставив плеер на его экране (Back в списке
#               воспроизведения, но не Home).
#   3. Смотреть, какие поля разошлись между участками.
#
# Запуск:
#   sh tools/stand/vdec_states.sh [секунд] [период_с]
# Переменные: ADB_DEV (или D) — адрес adb-устройства, по умолчанию 192.168.88.50:5555.
#
# Одновременно полезен журнал демона (`log.enable=1`): он пишет status декодера
# на старте видео, при остановке счётчика и при снятии удержания.
#
# Код возврата: 0 — пробы сняты, 2 — стенд недоступен.

D=${ADB_DEV:-${D:-192.168.88.50:5555}}
DURATION=${1:-120}
INTERVAL=${2:-2}

if ! adb -s "$D" shell true >/dev/null 2>&1; then
	echo "стенд $D недоступен — пробы не сняты" >&2
	exit 2
fi

echo "# состояние декодера: пробы каждые ${INTERVAL} с, всего ${DURATION} с"
echo "# время | device | frames | status | dur | mode | top | media_session | video_layer"
echo "#    (играет / пауза / видео остановлено — участки размечать по времени)"

# Всё внутри одного вызова adb на пробу: dumpsys на устройстве дорог, а
# round-trip adb ещё дороже.
SAMPLE='
	mode=$(cat /sys/class/display/mode 2>/dev/null || echo n/a)
	# первый блок vdec_status: device name / frame count / status / frame dur
	vs=$(cat /sys/class/vdec/vdec_status 2>/dev/null)
	dev=$(printf "%s\n" "$vs" | sed -n "s/^ *device name *: *//p" | head -1)
	frames=$(printf "%s\n" "$vs" | sed -n "s/^ *frame count *: *//p" | head -1)
	st=$(printf "%s\n" "$vs" | sed -n "s/^ *status *: *//p" | head -1)
	dur=$(printf "%s\n" "$vs" | sed -n "s/^ *frame dur *: *//p" | head -1)
	# приложение на переднем плане
	top=$(dumpsys activity activities 2>/dev/null | grep -m1 ResumedActivity |
	      sed -n "s/.*[ {]u[0-9]* \([^ /]*\).*/\1/p")
	# состояние воспроизведения: строка вида "state=PlaybackState {state=3, ...}"
	ms=$(dumpsys media_session 2>/dev/null | grep -m1 -o "state=PlaybackState {state=[0-9]*" |
	     sed -n "s/.*state=//p")
	# есть ли видео-слой (у плееров он обычно SurfaceView[<pkg>...]#0)
	layer=$(dumpsys SurfaceFlinger --list 2>/dev/null | grep -c -i "SurfaceView\[")
	echo "$(date +%H:%M:%S) | ${dev:-n/a} | ${frames:-n/a} | ${st:-n/a} | ${dur:-n/a} | ${mode:-n/a} | ${top:-n/a} | ${ms:-none} | ${layer:-0}"
'

n=0
while [ "$n" -lt "$DURATION" ]; do
	adb -s "$D" shell "$SAMPLE" 2>/dev/null
	n=$((n + INTERVAL))
	sleep "$INTERVAL"
done

echo "# готово. Разметить участки по времени и сравнить поля status/dur/frames:"
echo "#   играет     — frames растут, status = ?"
echo "#   пауза      — frames стоят, status = ?"
echo "#   остановлено— frames стоят, status = ? (совпадает с паузой => сигнала нет)"
