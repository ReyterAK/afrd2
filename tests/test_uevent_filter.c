#include <assert.h>
#include <stdio.h>

#include "../afrd.h"
#include "../uevent_filter.h"

/* Повтор имени атрибута в одном uevent не должен считаться дважды: иначе
 * matches > size и фильтр молча «не срабатывает» (P3 прохода 2). */

/* uevent_filter_load() (не вызывается в тесте) тянет cfg_get_str: на хосте
 * конфига нет, тесту он не нужен. */
const char *cfg_get_str (const char *key, const char *defval)
{
	(void)key;
	return defval;
}

static void t_duplicate_attr (void)
{
	uevent_filter_t f;
	assert (uevent_filter_init (&f, "test",
	        "DEVPATH=/devices/platform/vdec/.* SUBSYSTEM=platform"));
	uevent_filter_reset (&f);

	assert (uevent_filter_match (&f, "DEVPATH", "/devices/platform/vdec/vdec.0"));
	assert (!uevent_filter_matched (&f));

	/* та же строка пришла второй раз */
	assert (uevent_filter_match (&f, "DEVPATH", "/devices/platform/vdec/vdec.0"));
	assert (!uevent_filter_matched (&f));

	assert (uevent_filter_match (&f, "SUBSYSTEM", "platform"));
	assert (uevent_filter_matched (&f));

	uevent_filter_fini (&f);
}

static void t_first_value_fails (void)
{
	uevent_filter_t f;
	assert (uevent_filter_init (&f, "test",
	        "DEVPATH=/devices/platform/vdec/.* SUBSYSTEM=platform"));
	uevent_filter_reset (&f);

	/* первое значение не подходит, второе — да; фильтр должен сработать */
	assert (!uevent_filter_match (&f, "DEVPATH", "/devices/platform/hdmi/0"));
	assert (uevent_filter_match (&f, "DEVPATH", "/devices/platform/vdec/vdec.0"));
	assert (uevent_filter_match (&f, "SUBSYSTEM", "platform"));
	assert (uevent_filter_matched (&f));

	uevent_filter_fini (&f);
}

static void t_reset_clears_hits (void)
{
	uevent_filter_t f;
	assert (uevent_filter_init (&f, "test",
	        "DEVPATH=/devices/platform/vdec/.* SUBSYSTEM=platform"));
	uevent_filter_reset (&f);
	assert (uevent_filter_match (&f, "DEVPATH", "/devices/platform/vdec/vdec.0"));
	assert (uevent_filter_match (&f, "SUBSYSTEM", "platform"));
	assert (uevent_filter_matched (&f));

	/* следующий uevent: совпадения прошлого не переносятся */
	uevent_filter_reset (&f);
	assert (uevent_filter_match (&f, "SUBSYSTEM", "platform"));
	assert (!uevent_filter_matched (&f));

	uevent_filter_fini (&f);
}

static void t_whole_value_only (void)
{
	uevent_filter_t f;
	assert (uevent_filter_init (&f, "test", "MODALIAS=platform:amvdec_av1"));
	uevent_filter_reset (&f);

	/* regex обязан покрывать значение целиком */
	assert (!uevent_filter_match (&f, "MODALIAS", "platform:amvdec_av1_v4l"));
	assert (uevent_filter_match (&f, "MODALIAS", "platform:amvdec_av1"));
	assert (uevent_filter_matched (&f));

	uevent_filter_fini (&f);
}

int main (void)
{
	t_duplicate_attr ();
	t_first_value_fails ();
	t_reset_clears_hits ();
	t_whole_value_only ();
	printf ("uevent_filter: OK\n");
	return 0;
}
