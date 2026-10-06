#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../cfgkeys.h"
#include "cfg_parse.h"

/* Проверки применимости ключей: то, что действует на одном пути, не должно
 * молча «исчезать» на другом, а опечатка — считаться рабочей настройкой. */
static void test_notes (void)
{
	/* общие ключи действуют везде */
	assert (cfgkeys_note ("enable", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("enable", CFGKEY_LEGACY) == NULL);
	assert (cfgkeys_note ("log.file", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("switch.timeout", CFGKEY_ATV14) == NULL);

	/* удвоение частоты и чёрный список — оба пути (иначе это и был баг) */
	assert (cfgkeys_note ("mode.prefer.exact", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("mode.prefer.exact", CFGKEY_LEGACY) == NULL);
	assert (cfgkeys_note ("mode.blacklist.rates", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("mode.blacklist.rates", CFGKEY_LEGACY) == NULL);

	/* Перенесены на оба пути (политика аддитивности с переносом): «зажим»
	 * темпа плеером и определение переднего пакета есть и на Android 6-9. */
	assert (cfgkeys_note ("switch.skip.pkg", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("switch.skip.pkg", CFGKEY_LEGACY) == NULL);
	assert (cfgkeys_note ("mode.min.hz.list", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("mode.min.hz.list", CFGKEY_LEGACY) == NULL);

	/* ключи нового пути */
	assert (cfgkeys_note ("trigger.poll.ms", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("trigger.poll.ms", CFGKEY_LEGACY) != NULL);
	assert (cfgkeys_note ("pause.keep", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("pause.keep", CFGKEY_LEGACY) != NULL);

	/* новые ключи огибающей действуют на новом пути */
	assert (cfgkeys_note ("fps.measure.envelope.ms", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("fps.measure.envelope.max.ms", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("fps.measure.envelope.sub.ms", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("fps.measure.envelope.skip.ms", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("fps.correct.av1.offset", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("fps.correct.av1.pkg", CFGKEY_ATV14) == NULL);

	/* устаревшие ключи названы устаревшими, а не «не действуют» */
	assert (cfgkeys_note ("fps.measure.window.ms", CFGKEY_ATV14) != NULL);
	assert (strstr (cfgkeys_note ("fps.measure.window.ms", CFGKEY_ATV14), "устарел") != NULL);
	assert (strstr (cfgkeys_note ("fps.measure.window.max.ms", CFGKEY_ATV14), "устарел") != NULL);

	/* ключи старого пути */
	assert (cfgkeys_note ("mode.extra", CFGKEY_LEGACY) == NULL);
	assert (cfgkeys_note ("mode.extra", CFGKEY_ATV14) != NULL);
	/* cs.* с 2026-10-02 действуют на обоих путях: на ATV14 — через
	 * вендорский сервис, а не только записью в sysfs */
	assert (cfgkeys_note ("cs.select", CFGKEY_LEGACY) == NULL);
	assert (cfgkeys_note ("cs.select", CFGKEY_ATV14) == NULL);
	assert (cfgkeys_note ("uevent.filter.vdec", CFGKEY_LEGACY) == NULL);
	assert (cfgkeys_note ("uevent.filter.vdec", CFGKEY_ATV14) != NULL);

	/* настройка приложения — в конфиге допустима, но демон её не читает */
	assert (cfgkeys_note ("toast.hz", CFGKEY_ATV14) != NULL);
	assert (cfgkeys_note ("toast.hz", CFGKEY_LEGACY) != NULL);

	/* опечатка не должна считаться рабочей настройкой */
	assert (strstr (cfgkeys_note ("fps.recheck.delay", CFGKEY_ATV14), "неизвестный") != NULL);
	assert (strstr (cfgkeys_note ("Mode.prefer.exact", CFGKEY_ATV14), "неизвестный") != NULL);
}

static char g_warn [1024];

static void collect (const char *msg, void *ctx)
{
	(void)ctx;
	strncat (g_warn, msg, sizeof (g_warn) - strlen (g_warn) - 1);
	strncat (g_warn, "\n", sizeof (g_warn) - strlen (g_warn) - 1);
}

static void write_file (const char *path, const char *text)
{
	FILE *f = fopen (path, "wb");
	assert (f);
	fwrite (text, 1, strlen (text), f);
	fclose (f);
}

/* Разбор файла: считаются только ключи, не действующие на пути. */
static void test_audit (void)
{
	const char *cfg = "/tmp/test_cfgkeys.ini";
	write_file (cfg, "# комментарий, не ключ\n"
	                 "enable=1\n"
	                 "mode.prefer.exact=0\n"
	                 "switch.delay.on=300\n"       /* только legacy */
	                 "trigger.poll.ms=300\n"
	                 "toast.hz=1\n"                 /* только приложение */
	                 "fps.recheck.delay=8000\n");   /* опечатка */

	g_warn [0] = 0;
	assert (cfgkeys_audit (cfg, CFGKEY_ATV14, collect, NULL) == 3);
	assert (strstr (g_warn, "switch.delay.on") != NULL);
	assert (strstr (g_warn, "toast.hz") != NULL);
	assert (strstr (g_warn, "неизвестный") != NULL);
	assert (strstr (g_warn, "enable") == NULL);

	/* на старом пути те же записи дают три предупреждения: ключ нового пути
	 * (trigger.poll.ms), ключ приложения (toast.hz) и опечатка */
	g_warn [0] = 0;
	assert (cfgkeys_audit (cfg, CFGKEY_LEGACY, collect, NULL) == 3);
	assert (strstr (g_warn, "trigger.poll.ms") != NULL);
	assert (strstr (g_warn, "switch.delay.on") == NULL);

	/* нечитаемый файл — -1, а не «всё хорошо» */
	assert (cfgkeys_audit ("/tmp/test_cfgkeys_нет_такого.ini", CFGKEY_ATV14, NULL, NULL) == -1);
}

/* Наши собственные профили обязаны быть чистыми на своём пути: любой ключ,
 * не подключённый к демону (или опечатка), уронит этот тест. */
static void test_profiles (void)
{
	g_warn [0] = 0;
	assert (cfgkeys_audit ("config/afrd2.ini", CFGKEY_ATV14, collect, NULL) == 0);
	assert (g_warn [0] == 0);

	g_warn [0] = 0;
	assert (cfgkeys_audit ("config/afrd-android8+.ini", CFGKEY_LEGACY, collect, NULL) == 0);
	assert (g_warn [0] == 0);

	g_warn [0] = 0;
	assert (cfgkeys_audit ("config/afrd-android7-.ini", CFGKEY_LEGACY, collect, NULL) == 0);
	assert (g_warn [0] == 0);
}

int main (void)
{
	test_notes ();
	test_audit ();
	test_profiles ();

	printf ("cfgkeys: OK\n");
	return 0;
}
