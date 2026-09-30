/*
 * Заглушки upstream-символов для host-тестов.
 * На устройстве trace()/sysfs_write()/g_mode_path определены в main.c/sysfs.c,
 * в host-сборку они не попадают.
 */
#include "../afrd.h"

const char *g_mode_path = NULL;

void trace (int level, const char *format, ...)
{
	(void)level;
	(void)format;
}

int sysfs_write (const char *device_attr, const char *value)
{
	(void)device_attr;
	(void)value;
	return 0;
}
