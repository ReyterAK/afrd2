#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include "sink.h"

typedef enum
{
	PLAT_LEGACY = 0,	/* ядра 3.14/4.9, Android 6-9 */
	PLAT_ATV14 = 1,		/* ядра 5.x, Android 10-14: поллинг + backdoor SF */
} platform_kind_t;

typedef struct
{
	platform_kind_t kind;
	sink_kind_t sink;
	const char *vdec_status;	/* /sys/class/vdec/vdec_status */
	const char *hdmi_state;		/* признак подключённого HDMI */
	const char *sf_dump_cmd;	/* команда получения таблицы режимов (с фильтром: дамп SF больше буфера) */
	const char *display_dump_cmd;	/* команда активного разрешения и предпочтения (dumpsys display, с фильтром) */
	const char *activity_dump_cmd;	/* команда пакета приложения на переднем плане (dumpsys activity, с фильтром) */
	const char *vendor_cli;		/* вендорский клиент SystemControl (/vendor/bin/meson_display_client) или NULL */
	bool sf_backdoor;		/* доступна ли транзакция 1035 */
} platform_t;

platform_t platform_detect (void);

#endif
