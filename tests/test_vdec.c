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

static vdec_chan_t ch (const char *dev, unsigned long long frames)
{
	vdec_chan_t c;
	memset (&c, 0, sizeof (c));
	snprintf (c.device, sizeof (c.device), "%s", dev);
	c.frames = frames;
	return c;
}

/* Пауза/буферизация отсекается в самом замере: под-срез, в котором счётчик
 * кадров не вырос, делает выборку невалидной (`afrd.c`, `grown`). */

int main (void)
{
	/* --- разбор каналов --- */
	vdec_chan_t chans [VDEC_MAX_CHANNELS];

	assert (vdec_parse_channels (slurp ("tests/fixtures/vdec_status_idle.txt"),
	                             chans, VDEC_MAX_CHANNELS) == 0);

	assert (vdec_parse_channels (slurp ("tests/fixtures/vdec_status_playing.txt"),
	                             chans, VDEC_MAX_CHANNELS) == 1);
	assert (strcmp (chans [0].device, "ammvdec_h264_v4") == 0);
	assert (chans [0].frames == 13516);
	/* frame dur парсится только как уточнение (96000/4004 = 23.976) */
	assert (chans [0].decl_dur == 4004);
	/* status — сырое поле драйвера, читается ради разделения «пауза vs
	 * залипший канал» на стенде (§31.18), решений по нему нет */
	assert (chans [0].status == 6);

	/* мусор без имени устройства не должен "включать" видео */
	assert (vdec_parse_channels ("vdec channel 0 statistics:\n  frame count : 5\n",
	                             chans, VDEC_MAX_CHANNELS) == 0);
	/* без frame count канал не считается */
	assert (vdec_parse_channels ("  device name : dev1\n  frame width : 1920\n",
	                             chans, VDEC_MAX_CHANNELS) == 0);

	/* два канала: залипший первым, живой последним (§18.1/§18.4) */
	assert (vdec_parse_channels (slurp ("tests/fixtures/vdec_status_two_channels.txt"),
	                             chans, VDEC_MAX_CHANNELS) == 2);
	assert (strcmp (chans [0].device, "amvdec_av1_v4l") == 0);
	assert (chans [0].frames == 264);
	assert (chans [0].status == 53);
	assert (chans [1].frames == 1139);
	assert (chans [1].decl_dur == 3200);
	assert (chans [1].status == 5);

	/* потолок каналов */
	char many [1024] = "";
	for (int i = 0; i < 6; i++)
	{
		char blk [128];
		snprintf (blk, sizeof (blk), "  device name : dev%d\n  frame count : %d\n", i, i + 1);
		strcat (many, blk);
	}
	assert (vdec_parse_channels (many, chans, VDEC_MAX_CHANNELS) == VDEC_MAX_CHANNELS);

	/* status ищется как подпись поля: слово внутри другого слова (как в
	 * "vdec_status=5") подписью не считается, а отсутствие поля даёт 0 */
	assert (vdec_parse_channels ("  device name : dev1\n  frame count : 7\n"
	                             "      vdec_status=5\n", chans, VDEC_MAX_CHANNELS) == 1);
	assert (chans [0].status == 0);
	assert (vdec_parse_channels ("  device name : dev1\n       status : 9\n"
	                             "  frame count : 7\n", chans, VDEC_MAX_CHANNELS) == 1);
	assert (chans [0].status == 9);
	assert (vdec_parse_channels ("  device name : dev1\n  frame count : 7\n",
	                             chans, VDEC_MAX_CHANNELS) == 1);
	assert (chans [0].status == 0);
	/* поле с другим именем ("vdec_status") — это не наше поле "status" */
	assert (vdec_parse_channels ("  device name : dev1\n  frame count : 7\n"
	                             "  vdec_status : 5\n", chans, VDEC_MAX_CHANNELS) == 1);
	assert (chans [0].status == 0);

	/* --- выбор канала (R61, вариант 2) --- */
	vdec_sel_t sel;

	/* пустой список — состояния нет */
	memset (&sel, 0xAA, sizeof (sel));
	assert (vdec_select (NULL, 0, &sel) == -1);
	assert (!sel.have_prev);

	/* первый опрос, два канала: растёт не с чем сравнить -> последний (живой) */
	vdec_chan_t zl [2] = { ch ("zombie", 264), ch ("live", 1139) };
	memset (&sel, 0, sizeof (sel));
	assert (vdec_select (zl, 2, &sel) == 1);

	/* живой растёт -> он же */
	zl [1] = ch ("live", 1200);
	assert (vdec_select (zl, 2, &sel) == 1);

	/* пауза: никто не растёт -> последний */
	assert (vdec_select (zl, 2, &sel) == 1);

	/* залипший первым заморожен, живёт последний — не ослепляемся */
	vdec_chan_t a [2] = { ch ("stuck", 5000), ch ("play", 100) };
	memset (&sel, 0, sizeof (sel));
	assert (vdec_select (a, 2, &sel) == 1);
	a [1] = ch ("play", 130);
	assert (vdec_select (a, 2, &sel) == 1);

	/* одиночный канал */
	vdec_chan_t one [1] = { ch ("solo", 100) };
	memset (&sel, 0, sizeof (sel));
	assert (vdec_select (one, 1, &sel) == 0);
	one [0] = ch ("solo", 250);
	assert (vdec_select (one, 1, &sel) == 0);
	assert (vdec_select (one, 1, &sel) == 0);	/* пауза — тот же канал */

	/* залипший исчез: остался живой на месте индекса 0 */
	vdec_chan_t ab [2] = { ch ("zombie", 500), ch ("live", 1000) };
	memset (&sel, 0, sizeof (sel));
	assert (vdec_select (ab, 2, &sel) == 1);
	vdec_chan_t b [1] = { ch ("live", 1005) };
	assert (vdec_select (b, 1, &sel) == 0);

	/* новое видео: счётчик нового с нуля, никто из «прошлых» не растёт -> последний */
	vdec_chan_t old [1] = { ch ("old", 5000) };
	memset (&sel, 0, sizeof (sel));
	assert (vdec_select (old, 1, &sel) == 0);
	vdec_chan_t t2 [2] = { ch ("old", 5000), ch ("new", 0) };
	assert (vdec_select (t2, 2, &sel) == 1);
	t2 [1] = ch ("new", 40);
	assert (vdec_select (t2, 2, &sel) == 1);

	/* растут оба — берём самый свежий (последний) */
	vdec_chan_t g2 [2] = { ch ("x", 100), ch ("y", 100) };
	memset (&sel, 0, sizeof (sel));
	assert (vdec_select (g2, 2, &sel) == 1);
	g2 [0] = ch ("x", 120);
	g2 [1] = ch ("y", 130);
	assert (vdec_select (g2, 2, &sel) == 1);

	/* --- vdec_read / vdec_probe_active на файлах --- */
	vdec_status_t st;
	memset (&sel, 0, sizeof (sel));
	assert (vdec_read ("tests/fixtures/vdec_status_two_channels.txt", &st, &sel) == 0);
	assert (st.active);
	assert (strcmp (st.device, "amvdec_av1_v4l") == 0);
	assert (st.frames == 1139);		/* выбран живой (последний) */
	assert (st.decl_dur == 3200);

	memset (&sel, 0, sizeof (sel));
	assert (vdec_read ("tests/fixtures/vdec_status_idle.txt", &st, &sel) == 0);
	assert (!st.active);

	/* отсутствующий файл — ошибка чтения, состояние выбора не трогаем */
	memset (&sel, 0, sizeof (sel));
	sel.have_prev = true;
	sel.prev_frames [0] = 42;
	assert (vdec_read ("/nonexistent/vdec_status", &st, &sel) == -1);
	assert (!st.active);
	assert (sel.have_prev && sel.prev_frames [0] == 42);

	bool active = true;
	assert (vdec_probe_active ("tests/fixtures/vdec_status_playing.txt", &active) == 0);
	assert (active);
	assert (vdec_probe_active ("tests/fixtures/vdec_status_idle.txt", &active) == 0);
	assert (!active);

	printf ("vdec: OK\n");
	return 0;
}
