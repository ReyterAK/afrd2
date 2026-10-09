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
 * Как вызывается (это dex, запускаемый штатным app_process — как cmd/dumpsys):
 *
 *   CLASSPATH=<этот dex>:/system/framework/android.hidl.base-V1.0-java.jar:\
 *             /system/framework/android.hidl.manager-V1.0-java.jar:\
 *             <droidlogic-res.apk> \
 *   app_process /system/bin AfrdDispMode <имя режима>
 *
 * HIDL-прокси (`vendor.amlogic.hardware.systemcontrol.V1_1.ISystemControl`,
 * на старых сборках — V1_0) лежит в `droidlogic-res.apk` вендора, поэтому
 * вызывается рефлексией — своих HIDL-заголовков не требуется и ABI вендора не
 * «прошивается» в демон. Путь к этому apk зависит от версии системы (раздел
 * /system_ext появился только в Android 11), поэтому его перебирает обёртка
 * afrd-disp.sh, а не этот класс.
 *
 * Коды возврата (демон считает успехом только 0):
 *   0 — режим применён и подтверждён чтением узла;
 *   2 — не передан режим (или пустой);
 *   3 — сервис SystemControl недоступен (нет ни V1_1, ни V1_0);
 *   4 — HAL вернул успех, но режим не сменился (неизвестное имя режима:
 *       живая находка 2026-10-08 — на «999p999hz» HAL молчит, а режим остаётся
 *       прежним; без этой проверки демон счёл бы применение успешным);
 *   5 — узел режима не читается (проверить применение нечем).
 *   6 — не найден droidlogic-res.apk (код возврата обёртки).
 *
 * Проверено живьём на Amlogic X4, Android 11 (ядро 5.4.125), стенд .45:
 *   AfrdDispMode 1080p24hz -> vinfo=1080p24hz, SF refresh-rate=23.976 fps, rc=0
 *   AfrdDispMode 1080p60hz -> vinfo=1080p60hz, SF refresh-rate=59.940 fps, rc=0
 *   AfrdDispMode 999p999hz -> rc=4 (режим не изменился)
 *   AfrdDispMode (без аргумента) / битый dex -> rc=2 / rc=3
 */

import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.io.BufferedReader;
import java.lang.reflect.Method;

public class AfrdDispMode
{
    /* Узел ядра с именем текущего режима: им же подтверждаем применение. */
    private static final String MODE_NODE = "/sys/class/display/mode";

    static String readMode ()
    {
        try
        {
            BufferedReader r = new BufferedReader (new InputStreamReader (new FileInputStream (MODE_NODE)));
            try
            {
                String line = r.readLine ();
                return (line == null) ? null : line.trim ();
            }
            finally { r.close (); }
        }
        catch (Exception e)
        {
            return null;
        }
    }

    public static void main (String[] args) throws Exception
    {
        if ((args.length != 1) || (args [0] == null) || args [0].isEmpty ())
        {
            System.err.println ("usage: AfrdDispMode <mode>");
            System.exit (2);
        }

        String want = args [0];

        /* Версия интерфейса зависит от сборки: на проверенной (Android 11) есть
         * V1_1, но вендор мог оставить на 10 только V1_0 — тогда прежний
         * Class.forName по одному имени падал бы. Пробуем по порядку. */
        Class<?> c = null;
        Object svc = null;
        for (String ver : new String [] { "V1_1", "V1_0" })
        {
            try
            {
                Class<?> cand = Class.forName ("vendor.amlogic.hardware.systemcontrol." + ver + ".ISystemControl");
                Object candSvc = cand.getMethod ("getService").invoke (null);
                if (candSvc != null)
                {
                    c = cand;
                    svc = candSvc;
                    break;
                }
            }
            catch (Exception e)
            {
                /* этой версии нет (или нет её классов) — пробуем следующую */
            }
        }
        if (svc == null)
        {
            System.err.println ("systemcontrol service not found (tried V1_1, V1_0)");
            System.exit (3);
        }

        String before = readMode ();
        if (before == null)
        {
            System.err.println ("cannot read " + MODE_NODE);
            System.exit (5);
        }

        c.getMethod ("setActiveDispMode", String.class).invoke (svc, want);

        /* HAL молча принимает несуществующее имя режима (проверено на «999p999hz»),
         * поэтому успех подтверждаем чтением узла: демон должен знать, что режим
         * не применился, а не считать это успехом. */
        String after = readMode ();
        if (after == null)
        {
            System.err.println ("cannot read " + MODE_NODE + " after set");
            System.exit (5);
        }

        if (!want.equals (after))
        {
            System.err.println ("mode not applied: asked " + want + ", now " + after);
            System.exit (4);
        }

        System.exit (0);
    }
}
