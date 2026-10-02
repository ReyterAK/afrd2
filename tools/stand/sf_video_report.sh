#!/bin/sh
# AFRd2: разбор снимка `dumpsys SurfaceFlinger` (хост-скрипт) — что с видео-слоем
# и override частоты. Нужен для триажа «плохого» прогона (stutter_probe.sh
# сохраняет снимки как <label>.lat.<pre|steady>.sf).
#
# Запуск: sh tools/stand/sf_video_report.sh <файл.sf> [...]
#
# Что печатает по каждому снимку:
#   - строку FrameRateOverrides (там бывает залипший override на низкую частоту);
#   - для каждого слоя SurfaceView (BLAST): размер буфера и тип композиции
#     (норма — DEVICE; CLIENT означает GPU-композицию видео, это и даёт «10 fps»).

for f in "$@"; do
	[ -f "$f" ] || { echo "$f: нет файла"; continue; }
	echo "=== $f"
	grep -m1 'FrameRateOverrides=' "$f" | sed 's/^ */  /'
	awk '
	/^\* Layer 0x/ { name = $0; sub(/^\* Layer 0x[0-9a-f]+ /, "", name); buf = ""; comp = "" }
	/geomBufferSize=/ { if (buf == "") buf = $0 }
	/composition type=/ { if (comp == "") { comp = $0; sub(/^ */, "", comp) } }
	/SurfaceView\[/ && name != "" {
		# слой SurfaceView закончился — печатаем, если это видео-слой VLC/плеера
		printf "  %s\n    %s\n    %s\n", name, buf, comp
	}
	' "$f"
done
