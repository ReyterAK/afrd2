#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../vdec.h"
#include "../fps_class.h"

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

/* Пауза/буферизация: счётчик кадров стоит -> fps_estimate = 0,
 * демон не должен "намерить" 0 fps и уйти в переключение. */
static void t_paused (void)
{
	assert (fps_estimate (100, 1000000ULL, 100, 3000000ULL) == 0.0);

	double fps = fps_estimate (100, 1000000ULL, 200, 3000000ULL);
	assert (fps > 49.5 && fps < 50.5);
}

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

	t_paused ();

	printf ("vdec: OK\n");
	return 0;
}
