#include <unistd.h>
#include <string.h>
#include "afrd.h"
#include "exec.h"
#include "platform.h"
#include "sink.h"

#define VDVC_STATUS	"/sys/class/vdec/vdec_status"
#define HDMI_STATE_NEW	"/sys/class/extcon/extcon0/state"
#define HDMI_STATE_OLD	"/sys/class/switch/hdmi/state"
#define HDMI_STATE_OLD_CABLE	"/sys/class/switch/hdmi/cable.0/state"

static bool sf_backdoor_available (void)
{
	/* Проба: вызов с заведомо неверным id. Если backdoor есть — получим
	 * BAD_VALUE ("Invalid argument"), и флаг "режим задан снаружи" снимется.
	 * На старых Android этот код вернёт PERMISSION_DENIED/"unknown". */
	char cmd [128], out [256];
	sink_build_sf_reset (cmd, sizeof (cmd));
	if (exec_capture (cmd, out, sizeof (out)) != 0)
		return false;

	return strstr (out, "Invalid argument") != NULL;
}

platform_t platform_detect (void)
{
	platform_t p;
	memset (&p, 0, sizeof (p));

	p.vdec_status = VDVC_STATUS;
	/* Дампы фильтруются на стороне команды: на ATV14 `dumpsys SurfaceFlinger`
	 * отдаёт 126–150 КБ (зависит от того, что на экране), а буферы приёма
	 * в afrd.c — 64 КБ (static). Усечённый дамп доходил до парсера без
	 * таблицы режимов и без activeMode, и AFR молча не переключал (живая
	 * приёмка). Нужны ровно две вещи: строка "activeMode=" и строки
	 * таблицы "{id=…, hwcId=…, resolution=…}".
	 * `[{]` вместо `\{` — чтобы `{` не читался как начало интервала ERE.
	 * grep берётся из PATH (/system/bin, toybox) — как и `pidof` ниже. */
	p.sf_dump_cmd = "dumpsys SurfaceFlinger | grep -E 'activeMode=|^ *[{]id='";
	/* display-дамп (~30 КБ) тоже режем: нужны только активное разрешение
	 * ("real 1920 x 1080") и предпочтение пользователя. -o обязателен: строка
	 * mBaseDisplayInfo содержит и то, и другое, но целиком длиннее буфера. */
	p.display_dump_cmd = "dumpsys display | grep -o -E "
	                     "'mUserPreferredModeId=-?[0-9]+|real [0-9]+ x [0-9]+'";
	/* Пакет приложения на переднем плане — для поправки частоты конкретного
	 * плеера (fps.correct.list). Дамп activity большой, поэтому режем на
	 * стороне команды: нужна ровно одна строка topResumedActivity. */
	p.activity_dump_cmd = "dumpsys activity activities | grep -m1 topResumedActivity";

	bool have_vdec = (sysfs_exists (VDVC_STATUS) == 0);
	/* Android 6/7: switch/hdmi/state; Android 8/9: switch/hdmi/cable.0/state.
	 * Оба пути обязаны увести на legacy-ветку: иначе живым A8/A9-боксам
	 * выключится их штатный AFR (R18). */
	bool have_old_hdmi = (sysfs_exists (HDMI_STATE_OLD) == 0) ||
	                     (sysfs_exists (HDMI_STATE_OLD_CABLE) == 0);

	if (have_vdec && !have_old_hdmi)
	{
		p.kind = PLAT_ATV14;
		p.sink = SINK_SF_BACKDOOR;
		p.hdmi_state = HDMI_STATE_NEW;
		p.sf_backdoor = sf_backdoor_available ();
	}
	else
	{
		p.kind = PLAT_LEGACY;
		p.sink = SINK_LEGACY;
		p.hdmi_state = (sysfs_exists (HDMI_STATE_OLD) == 0)
		             ? HDMI_STATE_OLD : HDMI_STATE_OLD_CABLE;
		p.sf_backdoor = false;
	}

	trace (1, "Окружение: %s (sink=%s, backdoor=%s)\n",
	       (p.kind == PLAT_ATV14) ? "ATV14" : "legacy",
	       (p.sink == SINK_SF_BACKDOOR) ? "sf-1035" : "sysfs",
	       p.sf_backdoor ? "да" : "нет");

	if (p.kind == PLAT_ATV14 && !p.sf_backdoor)
		trace (0, "ВНИМАНИЕ: backdoor SurfaceFlinger недоступен — AFR работать не будет\n");

	return p;
}
