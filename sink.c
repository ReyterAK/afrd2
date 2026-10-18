#include <stdio.h>
#include "afrd.h"
#include "exec.h"
#include "sink.h"
#include "dispserve.h"

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

int sink_build_vendor_set (char *buf, size_t n, const char *cli, const char *mode_name)
{
	if (!cli || !*cli || !mode_name || !*mode_name)
		return -1;

	int r = snprintf (buf, n, "sh %s %s", cli, mode_name);
	return (r < 0 || (size_t)r >= n) ? -1 : 0;
}

int sink_release_backdoor (sink_kind_t kind)
{
	/* Вендорный путь (Android 10/11): «флага удержания» нет — режим держит
	 * вендорный сервис, а возврат делается обычным применением исходного
	 * режима. Снимать нечего. */
	if (kind == SINK_VENDOR_MODE)
		return 0;

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

	if (kind == SINK_VENDOR_MODE)
	{
		if (!legacy_mode_name || !*legacy_mode_name)
			return -1;

		/* Постоянный помощник: он держит VM и классы HIDL загруженными, поэтому
		 * смена режима перестаёт включать ~490 мс на запуск app_process
		 * (журнал §49.9). Любой отказ — работаем разовым запуском. */
		if (g_vendor_disp_persist)
		{
			int prc = dispserve_apply (g_vendor_disp_cli, legacy_mode_name);
			if (prc == 0)
			{
				trace (1, "Переключаю режим (вендорный SystemControl, помощник) на %s\n",
				       legacy_mode_name);
				return 0;
			}
			if (prc == -2)
				return -1;	/* режим отвергнут — повторять нечего */
		}

		/* Путь к помощнику берём из конфига тем же глобалом, что и вендорский
		 * клиент цветового пространства на ATV14 (afrd.c: g_vendor_disp_cli). */
		char cmd [256], out [256];
		if (sink_build_vendor_set (cmd, sizeof (cmd), g_vendor_disp_cli, legacy_mode_name) != 0)
			return -1;

		trace (1, "Переключаю режим (вендорный SystemControl) на %s\n", legacy_mode_name);

		/* Успех — код возврата помощника: он вернёт ненулевой, если сервис
		 * недоступен или метод отверг режим (см. AfrdDispMode.java). */
		int rc = exec_status (cmd, out, sizeof (out));
		if (rc == EXEC_TIMEOUT)
		{
			/* Зависший помощник (например, заклинивший вызов HIDL): дерево
			 * процессов убито по дедлайну, цикл демона не встал — говорим об
			 * этом отдельно от «отверг режим», это разные поломки. */
			trace (0, "вендорный SystemControl не ответил за отведённое время — "
			          "режим %s не применён, помощник прерван\n", legacy_mode_name);
			return -1;
		}

		if (rc != 0)
		{
			trace (1, "вендорный SystemControl не применил режим %s (код %d: %s)\n",
			       legacy_mode_name, rc, out);
			return -1;
		}
		return 0;
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
