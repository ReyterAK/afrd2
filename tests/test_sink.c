#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sink.h"

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

	printf ("sink: OK\n");
	return 0;
}
