#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../vdec.h"

static char *slurp (const char *p)
{
	static char b [8192];
	FILE *f = fopen (p, "rb");
	assert (f);
	size_t n = fread (b, 1, sizeof (b) - 1, f);
	b [n] = 0;
	fclose (f);
	return b;
}

/* Пауза/буферизация теперь отсекается в самом замере: под-срез, в котором
 * счётчик кадров не вырос, делает выборку невалидной (`afrd.c`, `grown`),
 * а `fps_classify_window (0, …)` решения не даёт — см. test_fps_class. */

int main (void)
{
	vdec_status_t st;

	assert (vdec_parse (slurp ("tests/fixtures/vdec_status_idle.txt"), &st) == 0);
	assert (!st.active);

	assert (vdec_parse (slurp ("tests/fixtures/vdec_status_playing.txt"), &st) == 0);
	assert (st.active);
	assert (strcmp (st.device, "ammvdec_h264_v4") == 0);
	assert (st.frames == 13516);

	/* мусор не должен "включать" видео */
	assert (vdec_parse ("vdec channel 0 statistics:\n  frame count : 5\n", &st) == 0);
	assert (!st.active);

	printf ("vdec: OK\n");
	return 0;
}
