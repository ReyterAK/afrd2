#!/system/bin/sh
# AFRd2: длинный замер частоты кадров живого видео (эталон для проверки замера демона).
#
# Печатает строки «<uptime сек> <frame count> <display mode>» и всё содержимое
# vdec_status, чтобы эталонную частоту можно было посчитать как Δframes/Δt за
# десятки секунд — там, где окно демона (1.5–6 с) ещё не различает 23.976/24/25.
#
# Одним вызовом adb shell (бокс в сне «холодный» вызов стоит минуты, поэтому
# все замеры должны идти внутри одного вызова):
#   adb -s 192.168.88.50:5555 shell 'sh /data/local/tmp/probe_stream.sh 30' > probe.txt
#
# Использование: probe_stream.sh [секунды] [интервал, мс]
DUR=${1:-30}
STEP_MS=${2:-1000}
OUT=/data/local/tmp/probe_stream.txt

: > "$OUT"
n=$(( DUR * 1000 / STEP_MS ))
i=0
while [ "$i" -le "$n" ]; do
	u=$(cut -d' ' -f1 /proc/uptime)
	fc=$(sed -n 's/^frame count[[:space:]]*:[[:space:]]*//p' /sys/class/vdec/vdec_status)
	m=$(cat /sys/class/display/mode)
	echo "$u $fc $m" >> "$OUT"
	sleep $(( STEP_MS / 1000 )) || break
	i=$(( i + 1 ))
done
echo "# --- vdec_status (последний снимок) ---" >> "$OUT"
cat /sys/class/vdec/vdec_status >> "$OUT"
echo "# --- vinfo ---" >> "$OUT"
grep -E 'name|sync_duration_num|meas_frame_rate' /sys/class/display/vinfo >> "$OUT" 2>/dev/null
cat "$OUT"
