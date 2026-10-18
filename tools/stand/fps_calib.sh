#!/system/bin/sh
# Калибровка оценки частоты кадров на боксе (журнал §52).
#
# Играет файл с ИЗВЕСТНОЙ частотой и снимает счётчик кадров декодера так же,
# как это делает демон (раз в trigger.poll.ms), затем печатает:
#   * сырые частоты по окнам 1 с и 5 с — минимум, медиану, максимум;
#   * какое значение поправки (проценты) нужно, чтобы попасть в номинал;
#   * запас до границы классов 24/25 (24.4898) и 25/50.
#
# Использование:
#   sh fps_calib.sh <файл> <номинал> [секунд] [период-мс]
# Примеры:
#   sh fps_calib.sh /sdcard/calib/h264_23976.mp4 23.976 30
#   sh fps_calib.sh /sdcard/calib/av1_25.mkv 25 30
#
# Файлы для калибровки готовятся на хосте (ffmpeg, testsrc2 — движение по всему
# кадру, CFR без дублей); для YouTube вместо файла удобнее снять замер вручную.

FILE=${1:?файл}
NOM=${2:?номинал, например 23.976}
SECS=${3:-30}
POLL=${4:-300}

# Плеер можно переопределить: на разных боксах штатный разный.
# По умолчанию — Amlogic ExoPlayer demo (стоковые прошивки).
ACT=${CALIB_ACT:-com.droidlogic.exoplayer2.demo/com.droidlogic.videoplayer.MoviePlayer}

now_ms () { awk '{ printf "%d", $1 * 1000 }' /proc/uptime; }
cnt () {
	cat /sys/class/vdec/vdec_status 2>/dev/null | awk -F: '
		/frame count/ { gsub(/[^0-9]/, "", $2); s += $2 + 0 }
		END { printf "%d", s + 0 }'
}
dec () {
	cat /sys/class/vdec/vdec_status 2>/dev/null | awk -F: '/device name/ { gsub(/^ +/, "", $2); print $2 }'
}

am force-stop "${ACT%%/*}" >/dev/null 2>&1
am start -n "$ACT" -a android.intent.action.VIEW -t video/mp4 \
	-d "file://$FILE" >/dev/null 2>&1
# Некоторые плееры открывают файл на паузе (Just Player на .50): без этого
# счётчик кадров стоит и замер пуст.
sleep 2
input keyevent KEYCODE_MEDIA_PLAY >/dev/null 2>&1
input keyevent KEYCODE_MEDIA_PLAY >/dev/null 2>&1
sleep 3

DEV=$(dec)
if [ -z "$DEV" ]; then
	echo "декодер не поднялся — файл не играет?" >&2
	exit 1
fi

OUT=/data/local/tmp/fps_calib.samples
: > "$OUT"
END=$(( $(now_ms) + SECS * 1000 ))
while [ "$(now_ms)" -lt "$END" ]; do
	echo "$(now_ms) $(cnt)" >> "$OUT"
	sleep 0.$((POLL / 100))
done

echo "файл: $FILE"
echo "декодер: $DEV, номинал: $NOM"
echo
awk -v nom="$NOM" -v warm=5000 '
	function median(a, n,   i, j, t) {
		for (i = 2; i <= n; i++) { t = a[i]; j = i - 1
			while (j > 0 && a[j] > t) { a[j+1] = a[j]; j-- } a[j+1] = t }
		return (n % 2) ? a[(n+1)/2] : (a[n/2] + a[n/2+1]) / 2
	}
	{ t[++n] = $1; f[n] = $2 }
	END {
		if (n < 6) { print "выборок мало (файл кончился?)"; exit }
		per = (t[n] - t[1]) / (n - 1)
		printf "выборок: %d, период ~%.0f мс, прогрев отброшен: %d мс\n\n", n, per, warm

		for (w = 1; w <= 2; w++) {
			win = (w == 1) ? 1000 : 5000
			m = 0
			for (i = 1; i <= n; i++) {
				if (t[i] - t[1] < warm + win) continue
				# ближайшая по времени выборка к t[i]-win
				best = 0; bd = 1e18
				for (j = 1; j < i; j++) {
					d = t[i] - t[j] - win
					if (d < 0) d = -d
					if (d < bd) { bd = d; best = j }
				}
				if ((best == 0) || (bd > win * 0.15)) continue
				dt = t[i] - t[best]
				r = (f[i] - f[best]) * 1000.0 / dt
				if (r <= 0) continue
				rw[++m] = r
			}
			if (m < 2) { printf "окно %d с: данных мало\n\n", win / 1000; continue }
			mn = rw[1]; mx = rw[1]; s = 0
			for (i = 1; i <= m; i++) { if (rw[i] < mn) mn = rw[i]; if (rw[i] > mx) mx = rw[i]; s += rw[i] }
			md = median(rw, m)
			printf "окно %d с (%d замеров): мин %.2f  медиана %.2f  макс %.2f\n", win / 1000, m, mn, md, mx
			printf "  поправка по медиане: %+.2f %%\n", (md / nom - 1) * 100
			printf "  разброс: %.2f к/с (%.1f %% номинала)\n", mx - mn, (mx - mn) / nom * 100
			if (nom < 24.5)
				printf "  граница 24/25: медиана %.2f -> %s, запас %.2f %%\n\n", md, \
					(md < 24.4898 ? "ниже (24)" : "ВЫШЕ (уйдёт в 25)"), \
					(md < 24.4898 ? (24.4898 / md - 1) * 100 : (1 - 24.4898 / md) * 100)
			else
				printf "  граница 24/25: медиана %.2f -> %s, запас %.2f %%\n\n", md, \
					(md < 24.4898 ? "ниже (уйдёт в 24!)" : "выше (25)"), \
					(md < 24.4898 ? (24.4898 / md - 1) * 100 : (1 - 24.4898 / md) * 100)
		}
	}' "$OUT"

rm -f "$OUT"
