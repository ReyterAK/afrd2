#include <assert.h>
#include <stdio.h>
#include "../reconf.h"

int main (void)
{
	/* обычная правка: mtime сменился -> перезагружаем */
	assert (config_mtime_changed (1000, 900));

	/* файл не менялся -> не перезагружаем */
	assert (!config_mtime_changed (1000, 1000));

	/* файл недоступен (mtime()==0) -> изменением не считаем: иначе каждый
	 * период проверки давал бы ложную перезагрузку */
	assert (!config_mtime_changed (0, 1000));
	assert (!config_mtime_changed (0, 0));

	/* запомненный mtime == -1 (afrd_reconf на legacy-пути) -> перезагружаем */
	assert (config_mtime_changed (1000, (time_t) -1));

	printf ("reconf: OK\n");
	return 0;
}
