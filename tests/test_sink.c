#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sink.h"

/* sink.c обращается к пути вендорного помощника — в тесте он не нужен, но
 * символ должен существовать (в демоне его заполняет afrd_init из конфига). */
const char *g_vendor_disp_cli = "/data/local/tmp/afrd-disp.sh";

int main (void)
{
	char buf [128];

	assert (sink_build_sf_set (buf, sizeof (buf), 6) == 0);
	assert (strcmp (buf, "service call SurfaceFlinger 1035 i32 6") == 0);

	assert (sink_build_sf_set (buf, sizeof (buf), 0) == 0);
	assert (strcmp (buf, "service call SurfaceFlinger 1035 i32 0") == 0);

	assert (sink_build_sf_reset (buf, sizeof (buf)) == 0);
	assert (strcmp (buf, "service call SurfaceFlinger 1035 i32 -1") == 0);

	/* слишком маленький буфер -> ошибка, без переполнения */
	assert (sink_build_sf_set (buf, 10, 6) != 0);

	/* `service` недоступен (пустой PATH) -> sink_apply обязан честно вернуть
	 * -1, а не «переключено»: в выводе нет "Result: " (Review Task 3, Important) */
	setenv ("PATH", "/nonexistent", 1);
	assert (sink_apply (SINK_SF_BACKDOOR, NULL, 6, NULL) == -1);

	/* ---- вендорный путь (Android 10/11): режим применяется по ИМЕНИ ---- */
	assert (sink_build_vendor_set (buf, sizeof (buf),
	                               "/data/local/tmp/afrd-disp.sh", "1080p24hz") == 0);
	assert (strcmp (buf, "sh /data/local/tmp/afrd-disp.sh 1080p24hz") == 0);

	/* нет имени режима — применять нечего */
	assert (sink_build_vendor_set (buf, sizeof (buf), "/x/afrd-disp.sh", NULL) == -1);
	assert (sink_build_vendor_set (buf, sizeof (buf), "/x/afrd-disp.sh", "") == -1);

	/* нет пути к помощнику — тоже отказ */
	assert (sink_build_vendor_set (buf, sizeof (buf), NULL, "1080p24hz") == -1);
	assert (sink_build_vendor_set (buf, sizeof (buf), "", "1080p24hz") == -1);

	/* не влезло — отказ, а не обрезанная команда */
	assert (sink_build_vendor_set (buf, 8, "/data/local/tmp/afrd-disp.sh", "1080p24hz") == -1);

	/* без имени режима применение не выполняется */
	assert (sink_apply (SINK_VENDOR_MODE, NULL, -1, NULL) == -1);

	/* снятие удержания для вендорного вида — no-op: «флага» тут нет, режим
	 * держит вендорный сервис, а возврат делается применением исходного */
	assert (sink_release_backdoor (SINK_VENDOR_MODE) == 0);

	printf ("sink: OK\n");
	return 0;
}
