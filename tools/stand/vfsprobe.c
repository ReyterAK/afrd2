/*
 * Стенд-инструмент: замер подачи видео-слоя там, где нет SurfaceFlinger --latency.
 *
 * Зачем. На Android 9 (UGOOS AM6, kernel 4.9) `dumpsys SurfaceFlinger --latency`
 * по видео-слою отдаёт нули (SF не ведёт FrameTracker для слоёв, уходящих в
 * hardware overlay), `screenrecord` видео-плоскость не захватывает, а счётчики
 * декодера повторяют частоту контента. Из-за этого §30.13/§34.10/§45.5 журнала
 * оставались без числовых опор. Рабочий источник — узел ядра
 * `/sys/class/video/vframe_states`: его поле `cur vpts` меняется ровно в момент
 * выхода нового кадра на плоскость. Интервал между сменами `cur vpts` и есть
 * интервал подачи; повтор одного и того же кадра смены не даёт (для замера это
 * и нужно: видно, сколько РАЗНЫХ кадров дошло до экрана).
 *
 * Калибровка (журнал §107, .48): 60p@60Гц -> 16.65 мс, 23.976@24Гц -> 41.47 мс,
 * 60p@50Гц -> 19.86 мс, OTT канал 1 (50 к/с)@50Гц -> 19.95 мс.
 *
 * Сборка (NDK r26d; статическая сборка на armv7 падает по TLS-выравниванию
 * bionic, поэтому линкуем динамически):
 *   NDK=$HOME/Android/Sdk/android-ndk-r26d
 *   $NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang \
 *       -O2 -o vfsprobe vfsprobe.c
 *
 * Запуск (root не обязателен, но на стендах всё идёт через su):
 *   adb push vfsprobe /data/local/tmp/vfsprobe
 *   adb shell su -c "/data/local/tmp/vfsprobe /data/local/tmp/vfs.log 10 1000"
 *   #        файл, секунд, минимальный интервал опроса в мкс (1000 = 1 мс)
 *
 * Формат строк: <монотонное время, нс> <vf index> <cur vpts>; последняя строка
 * "done <число проб>". Анализ — считать интервалы между сменами cur vpts.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

static long long now_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

int main(int argc, char **argv) {
	const char *path = "/sys/class/video/vframe_states";
	const char *out = argc > 1 ? argv[1] : "/data/local/tmp/vfs.log";
	double secs = argc > 2 ? atof(argv[2]) : 10.0;
	long every_us = argc > 3 ? atol(argv[3]) : 0;	/* 0 — опрос без паузы */

	int fd = open(path, O_RDONLY);
	FILE *f = fopen(out, "w");
	if (fd < 0 || !f) {
		perror("open");
		return 1;
	}

	char buf[4096];
	long long deadline = now_ns() + (long long)(secs * 1e9);
	long long n = 0;
	while (now_ns() < deadline) {
		ssize_t r = pread(fd, buf, sizeof(buf) - 1, 0);
		long long t = now_ns();
		long long i = -1, v = -1;
		if (r > 0) {
			buf[r] = 0;
			char *ix = strstr(buf, "vf index=");
			char *vp = strstr(buf, "cur vpts=");
			if (ix)
				i = atoll(ix + 9);
			if (vp)
				v = atoll(vp + 9);
		}
		/* r <= 0: видео ещё не поднято ("vframe no states") */
		fprintf(f, "%lld %lld %lld\n", t, i, v);
		n++;
		if (every_us > 0) {
			long long nxt = now_ns() + every_us * 1000LL;
			while (now_ns() < nxt)
				;
		}
	}
	fprintf(f, "done %lld\n", n);
	fclose(f);
	close(fd);
	return 0;
}
