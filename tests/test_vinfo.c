#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../afrd.h"
#include "../vinfo.h"

/* Реальный вывод /sys/class/display/vinfo со стенда .50 (ATV14, S905X5M,
 * 1080p50hz) — сокращён до нужных строк и соседей, чтобы проверять разбор
 * «посреди чужого текста». */
static const char *k_vinfo_50 =
	"current vinfo:\n"
	"    name:                  1080p50hz\n"
	"    mode:                  0\n"
	"    frac:                  0\n"
	"    width:                 1920\n"
	"    height:                1080\n"
	"    sync_duration_num:     50\n"
	"    sync_duration_den:     1\n"
	"    brr_duration:          0\n"
	"    (meas_frame_rate:      0.000)\n"
	"    vtotal:                1125\n";

/* Тот же узел в 23.976 (синтетическая запись режима 1080p24hz с дробной
 * политикой): 23976/1000 — точная дробь, которую по имени режима не видно. */
static const char *k_vinfo_23976 =
	"current vinfo:\n"
	"    name:                  1080p24hz\n"
	"    sync_duration_num:     23976\n"
	"    sync_duration_den:     1000\n"
	"    (meas_frame_rate:      0.000)\n";

/* Заглушка чтения sysfs: vinfo.c берёт узел через sysfs_read (afrd.h),
 * на хосте узла нет. Возвращаем фикстуру для «существующего» пути и NULL
 * для остальных — так проверяется и обёртка vinfo_sync_hz. */
char *sysfs_read (const char *path)
{
	if (!strcmp (path, "/fake/vinfo"))
	{
		size_t n = strlen (k_vinfo_23976);
		char *d = malloc (n + 1);
		assert (d);
		memcpy (d, k_vinfo_23976, n + 1);
		return d;
	}

	return NULL;
}

int main (void)
{
	int num = 0, den = 0;

	/* 50 Гц: 50/1 */
	assert (vinfo_parse_sync (k_vinfo_50, &num, &den) == 1);
	assert (num == 50 && den == 1);
	assert (vinfo_hz (num, den) == 50 * 256);

	/* 23.976: 23976/1000 -> 6138 (это и есть класс 23.976, а не 24.000) */
	assert (vinfo_parse_sync (k_vinfo_23976, &num, &den) == 1);
	assert (num == 23976 && den == 1000);
	assert (vinfo_hz (num, den) == 6138);
	assert (vinfo_hz (num, den) != 24 * 256);

	/* 60.000: 60/1 даёт ровно 60.00, 59.94 — 5994/100 */
	assert (vinfo_hz (60, 1) == 60 * 256);
	assert (vinfo_hz (5994, 100) == 15345);	/* 59.94 -> 15345 */

	/* Нет полей / мусор / нулевой знаменатель — решения нет, а не 0 Гц */
	assert (vinfo_parse_sync ("current vinfo:\n    name: 1080p50hz\n", &num, &den) == 0);
	assert (vinfo_parse_sync ("    sync_duration_num:     50\n", &num, &den) == 0);
	assert (vinfo_parse_sync ("    sync_duration_num:     0\n    sync_duration_den: 1\n",
	                          &num, &den) == 0);
	assert (vinfo_parse_sync ("    sync_duration_num:     50\n    sync_duration_den: 0\n",
	                          &num, &den) == 0);
	assert (vinfo_parse_sync ("    sync_duration_num:     abc\n    sync_duration_den: 1\n",
	                          &num, &den) == 0);
	assert (vinfo_parse_sync (NULL, &num, &den) == 0);

	assert (vinfo_hz (0, 1) == 0);
	assert (vinfo_hz (50, 0) == 0);

	/* Обёртка чтения: существующий узел разбирается, отсутствующий — 0 */
	assert (vinfo_sync_hz ("/fake/vinfo", &num, &den) == 6138);
	assert (num == 23976 && den == 1000);
	assert (vinfo_sync_hz ("/nonexistent/vinfo", &num, &den) == 0);

	printf ("vinfo: OK\n");
	return 0;
}
