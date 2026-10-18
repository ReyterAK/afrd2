/*
 * AFRd2: постоянный помощник смены режима (Android 10/11, Amlogic).
 *
 * Зачем. Разовый запуск `app_process` стоит ~490 мс (замер 18.10 на ATV11):
 * столько же, сколько сам HAL при смене режима на быстром ТВ. Помощник держит
 * VM и классы HIDL загруженными и обслуживает запросы построчно (stdin/stdout),
 * поэтому на каждую смену остаётся только работа HAL.
 *
 * Протокол (по строке на запрос):
 *   запрос:  <имя режима>
 *   ответ:   ok | err:<код>:<пояснение>
 *   "quit"  — завершиться.
 *
 * Коды те же, что у AfrdDispMode: 3 — нет сервиса, 4 — HAL не применил,
 * 5 — узел не читается.
 */

import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.io.BufferedReader;
import java.io.PrintStream;
import java.io.InputStream;
import java.lang.reflect.Method;

public class AfrdDispServe
{
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
            catch (Exception e) { }
        }

        PrintStream out = new PrintStream (System.out, true);
        if (svc == null)
        {
            out.println ("err:3:systemcontrol service not found");
            return;
        }

        Method set = c.getMethod ("setActiveDispMode", String.class);
        BufferedReader in = new BufferedReader (new InputStreamReader (System.in));
        String line;

        out.println ("ready");
        while ((line = in.readLine ()) != null)
        {
            line = line.trim ();
            if (line.isEmpty ())
                continue;
            if (line.equals ("quit"))
                return;

            try
            {
                set.invoke (svc, line);
            }
            catch (Exception e)
            {
                out.println ("err:3:" + e.getClass ().getSimpleName ());
                continue;
            }

            String after = readMode ();
            if (after == null)
            {
                out.println ("err:5:cannot read mode node");
                continue;
            }

            if (!line.equals (after))
            {
                out.println ("err:4:asked " + line + ", now " + after);
                continue;
            }

            out.println ("ok");
        }
    }
}
