#include <unistd.h>
#include <string.h>
#include "afrd.h"
#include "colorspace.h"
#include "exec.h"
#include "platform.h"
#include "sink.h"

#define VDVC_STATUS	"/sys/class/vdec/vdec_status"
#define HDMI_STATE_NEW	"/sys/class/extcon/extcon0/state"
#define HDMI_STATE_OLD	"/sys/class/switch/hdmi/state"
#define HDMI_STATE_OLD_CABLE	"/sys/class/switch/hdmi/cable.0/state"
/* Вендорский клиент SystemControl: через него на Android 10-14 применяется
 * цветовое пространство (sysfs-узел attr там только на чтение). */
#define CS_CLI			"/vendor/bin/meson_display_client"
/* Признаки вендорного механизма смены режима (Android 10/11, Amlogic): сам
 * сервис и apk с классами HIDL-прокси (его читает помощник через app_process). */
#define VENDOR_SYSTEMCONTROL	"/vendor/bin/systemcontrol"
#define VENDOR_DROIDLOGIC_RES	"/system_ext/priv-app/droidlogic-res/droidlogic-res.apk"
/* Вывод `dumpsys activity activities` нужен только одной строкой: приложение на
 * переднем плане. Поле называется по-разному — "topResumedActivity=..." (Android
 * 10-14) и "ResumedActivity: ..." (Android 6-9), но подстрока "ResumedActivity"
 * есть в обоих, а первое совпадение в дампе — нужная строка (на ATV14 первым
 * идёт topResumedActivity). Фильтр на стороне команды обязателен: дамп activity —
 * 18-26 КБ, буфер приёма под него в afrd.c — 4 КБ, и без фильтра нужная строка
 * не доходит до парсера. Фильтр обязателен и для дампа SurfaceFlinger (буфер
 * приёма там 64 КБ, сам дамп — 126-150 КБ). */
#define ACTIVITY_DUMP_CMD "dumpsys activity activities | grep -m1 ResumedActivity"

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
	/* Нужна обоим путям: по этому пакету работает switch.skip.pkg */
	p.activity_dump_cmd = ACTIVITY_DUMP_CMD;
	/* Дампы фильтруются на стороне команды: на ATV14 `dumpsys SurfaceFlinger`
	 * отдаёт 126–150 КБ (зависит от того, что на экране), а буферы приёма
	 * в afrd.c — 64 КБ (static). Усечённый дамп доходил до парсера без
	 * таблицы режимов и без activeMode, и AFR молча не переключал (живая
	 * приёмка). Нужны ровно две вещи: строка "activeMode=" и строки
	 * таблицы "{id=…, hwcId=…, resolution=…}".
	 * `[{]` вместо `\{` — чтобы `{` не читался как начало интервала ERE.
	 * grep берётся из PATH (/system/bin, toybox) — как и `pidof` ниже. */
	/* Фильтр обязан пропускать строку частоты активного режима и на Android
	 * 10/11: там нет ни activeMode, ни таблицы {id=…}, а частота (refresh-rate)
	 * нужна статистике пассивного режима. На 12-14 строка лишняя, но парсер её
	 * игнорирует (sfmodes_parse читает только «{id=…»), а sf_active_mode ищет
	 * activeMode первым. */
	p.sf_dump_cmd = "dumpsys SurfaceFlinger | grep -E 'activeMode=|refresh-rate|^ *[{]id='";
	/* display-дамп (~30 КБ) тоже режем: нужны только активное разрешение
	 * ("real 1920 x 1080") и предпочтение пользователя. -o обязателен: строка
	 * mBaseDisplayInfo содержит и то, и другое, но целиком длиннее буфера. */
	p.display_dump_cmd = "dumpsys display | grep -o -E "
	                     "'mUserPreferredModeId=-?[0-9]+|real [0-9]+ x [0-9]+'";

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
		/* Вендорский клиент нужен и для цветового пространства, и для чтения
		 * HDR/SDR-атрибутов (hdrinfo). */
		p.vendor_cli = (access (CS_CLI, X_OK) == 0) ? CS_CLI : NULL;
		p.vendor_mode_cli = cfg_get_str ("vendor.disp.cli", DEFAULT_VENDOR_DISP_CLI);
		p.disp_cap = cfg_get_str ("vendor.disp.cap", DEFAULT_VENDOR_DISP_CAP);

		/* Android 10/11 (Amlogic): транзакция 1035 там означает другую
		 * операцию, и на сборке 11 вызов с одним int роняет SurfaceFlinger
		 * (проверено живьём). Но режим переключается вендорным SystemControl
		 * HIDL — им пользуется штатный системный AFR (журнал §33.18). Идём
		 * этим путём только если помощник и вендорный сервис на месте; иначе
		 * остаётся прежний пассивный режим с предупреждением. */
		if (!p.sf_backdoor &&
		    (sysfs_exists (VENDOR_SYSTEMCONTROL) == 0) &&
		    (sysfs_exists (VENDOR_DROIDLOGIC_RES) == 0) &&
		    p.vendor_mode_cli && (sysfs_exists (p.vendor_mode_cli) == 0))
		{
			p.kind = PLAT_ATV11;
			p.sink = SINK_VENDOR_MODE;
			trace (1, "режим задаётся вендорным SystemControl (%s)\n", p.vendor_mode_cli);
		}
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
	       (p.kind == PLAT_LEGACY) ? "legacy" :
	       (p.kind == PLAT_ATV11) ? "ATV11" : "ATV14",
	       (p.sink == SINK_SF_BACKDOOR) ? "sf-1035" :
	       (p.sink == SINK_VENDOR_MODE) ? "vendor-mode" : "sysfs",
	       p.sf_backdoor ? "да" : "нет");

	if (p.kind == PLAT_ATV14 && !p.sf_backdoor)
		trace (0, "ВНИМАНИЕ: backdoor SurfaceFlinger недоступен — AFR работать не будет\n");

	/* Цветовое пространство (правила cs.select): на ATV14 применяем через
	 * вендорский сервис — sysfs-узел attr пишется с ошибкой (проверено). */
	if (p.kind == PLAT_ATV14)
	{
		if (p.vendor_cli)
			colorspace_set_apply_cmd (CS_CLI " -s \"HDMI Color ATTR\"");
		else
			trace (1, "нет %s — цветовое пространство не применяю\n", CS_CLI);
	}

	return p;
}
