#include <stdio.h>
#include "afrd.h"
#include "exec.h"
#include "sink.h"

int sink_build_sf_set (char *buf, size_t n, int sf_mode_id)
{
	int r = snprintf (buf, n, "service call SurfaceFlinger 1035 i32 %d", sf_mode_id);
	return (r < 0 || (size_t)r >= n) ? -1 : 0;
}

int sink_build_sf_reset (char *buf, size_t n)
{
	int r = snprintf (buf, n, "service call SurfaceFlinger 1035 i32 -1");
	return (r < 0 || (size_t)r >= n) ? -1 : 0;
}

int sink_release_backdoor (sink_kind_t kind)
{
	if (kind != SINK_SF_BACKDOOR)
		return 0;

	char cmd [128];
	if (sink_build_sf_reset (cmd, sizeof (cmd)) != 0)
		return -1;

	/* Ожидаемый ответ — ошибка EINVAL: это признак того, что backdoor есть,
	 * а флаг "режим задан снаружи" снят. Успех команды нам не нужен. */
	char out [256];
	if (exec_capture (cmd, out, sizeof (out)) != 0)
		trace (1, "backdoor reset: команда не выполнилась\n");
	else
		trace (2, "backdoor reset: %s\n", out);

	return 0;
}

int sink_apply (sink_kind_t kind, const char *mode_path, int sf_mode_id,
                const char *legacy_mode_name)
{
	if (kind == SINK_LEGACY)
	{
		if (!mode_path || !legacy_mode_name)
			return -1;

		trace (1, "Переключаю режим (legacy) на %s\n", legacy_mode_name);
		return sysfs_write (mode_path, legacy_mode_name);
	}

	char cmd [128], out [256];
	if (sink_build_sf_set (cmd, sizeof (cmd), sf_mode_id) != 0)
		return -1;

	trace (1, "Переключаю режим (backdoor) на mode id %d\n", sf_mode_id);
	if (exec_capture (cmd, out, sizeof (out)) != 0)
		return -1;

	/* Успех подтверждается только положительно: "Result: " тул печатает после
	 * реально выполненного transact (AOSP service.cpp). Если сервиса нет
	 * ("Service SurfaceFlinger does not exist"), команда не найдена
	 * ("not found") или транзакция вернула исключение (Parcel(Error: ...)),
	 * "Result: " в выводе отсутствует — это провал, а не успех. */
	if (!strstr (out, "Result:") || strstr (out, "Error:"))
	{
		trace (1, "backdoor вернул ошибку: %s\n", out);
		return -1;
	}

	return 0;
}
