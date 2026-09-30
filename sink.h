#ifndef SINK_H
#define SINK_H

#include <stddef.h>

typedef enum
{
	SINK_LEGACY = 0,	/* запись имени режима в /sys/class/display/mode (Android 6-9) */
	SINK_SF_BACKDOOR = 1,	/* service call SurfaceFlinger 1035 i32 <id> (Android 10-14) */
} sink_kind_t;

/* Строки команд (для тестов и для логов). Возврат: 0 = ок, -1 = не влезло. */
int sink_build_sf_set (char *buf, size_t n, int sf_mode_id);
int sink_build_sf_reset (char *buf, size_t n);

/* Применить режим. Возврат: 0 = ок, -1 = ошибка. */
int sink_apply (sink_kind_t kind, const char *mode_path, int sf_mode_id,
                const char *legacy_mode_name);

/* Снять флаг backdoor (безопасно и для legacy: no-op). Возврат: 0/-1. */
int sink_release_backdoor (sink_kind_t kind);

#endif
