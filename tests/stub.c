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

/* Постоянный помощник режима и его ключ: на хосте не нужны, в test_sink
 * проверяется сборка команд, а не работа вендорного HAL. */
int g_vendor_disp_persist = 0;

int dispserve_apply (const char *script, const char *mode_name)
{
	(void)script;
	(void)mode_name;
	return -1;		/* «помощник недоступен» — тест идёт разовым путём */
}

void dispserve_stop (void) { }

int sysfs_write (const char *device_attr, const char *value)
{
	(void)device_attr;
	(void)value;
	return 0;
}
