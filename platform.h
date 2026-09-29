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
	const char *sf_dump_cmd;	/* команда получения таблицы режимов */
	const char *display_dump_cmd;	/* команда активного разрешения (dumpsys display) */
	bool sf_backdoor;		/* доступна ли транзакция 1035 */
} platform_t;

platform_t platform_detect (void);

#endif
