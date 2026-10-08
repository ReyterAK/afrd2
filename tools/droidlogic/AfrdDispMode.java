/*
 * AFRd2: смена режима на Android 10/11 (Amlogic) через вендорный HAL SystemControl.
 *
 * Зачем. На Android 10/11 AOSP-транзакция SurfaceFlinger, которой ATV14-путь
 * переключает режим, означает другую операцию (на 10 — setAllowedDisplayConfigs,
 * на 11 — setActiveConfig) и на amlogic-сборках 11 падает при вызове с одним
 * int. Рабочий механизм там вендорный: HIDL
 * `vendor.amlogic.hardware.systemcontrol@1.1::ISystemControl`, метод
 * `setActiveDispMode(String)` — им пользуется штатный «системный AFR»
 * (DroidLogic `FrameRateAutoByNode`); он же держит SurfaceFlinger в синхроне
 * (проверено живьём: после вызова `refresh-rate` в дампе SF меняется вместе с
 * режимом ядра). Ту же семью вендорских клиентов использует ATV14-путь для
 * цветового пространства (`/vendor/bin/meson_display_client`).
 *
 * Как вызывается (сборкой не является самостоятельной программой — это dex,
 * запускаемый штатным app_process; так же работают cmd/dumpsys):
 *
 *   CLASSPATH=<этот dex>:/system/framework/android.hidl.base-V1.0-java.jar:\
 *             /system/framework/android.hidl.manager-V1.0-java.jar:\
 *             /system_ext/priv-app/droidlogic-res/droidlogic-res.apk \
 *   app_process /system/bin AfrdDispMode <имя режима>
 *
 * HIDL-прокси (`vendor.amlogic.hardware.systemcontrol.V1_1.ISystemControl`)
 * лежит в `droidlogic-res.apk` этого же бокса, поэтому вызывается рефлексией —
 * своих HIDL-заголовков не требуется и ABI вендора не «прошивается» в демон.
 *
 * Проверено живьём на Amlogic X4, Android 11 (ядро 5.4.125), стенд .45:
 *   AfrdDispMode 1080p24hz -> vinfo=1080p24hz, SF refresh-rate=23.976 fps
 *   AfrdDispMode 1080p60hz -> vinfo=1080p60hz, SF refresh-rate=59.940 fps
 */

import java.lang.reflect.Method;

public class AfrdDispMode
{
    public static void main (String[] args) throws Exception
    {
        if (args.length != 1)
        {
            System.err.println ("usage: AfrdDispMode <mode>");
            System.exit (2);
        }

        Class<?> c = Class.forName ("vendor.amlogic.hardware.systemcontrol.V1_1.ISystemControl");
        Object svc = c.getMethod ("getService").invoke (null);
        if (svc == null)
        {
            System.err.println ("systemcontrol service not found");
            System.exit (3);
        }

        c.getMethod ("setActiveDispMode", String.class).invoke (svc, args [0]);
        System.exit (0);
    }
}
