package ru.cobra.zap.afrd;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.List;

/**
 * Минимальная замена eu.chainfire.libsuperuser (Shell/Shell.SU).
 *
 * Причина: библиотека публиковалась в jcenter, который закрыт, а в mavenCentral
 * её нет — сборка форка без этой замены невозможна. Используется ровно две
 * операции: проверить наличие root и выполнить команду от root.
 *
 * Лицензия: собственная реализация, ничего не скопировано.
 */
public class RootShell
{
    /** Есть ли рабочий root (su отвечает uid=0). */
    public static boolean suAvailable ()
    {
        try
        {
            Process p = new ProcessBuilder ("su", "-c", "id")
                    .redirectErrorStream (true).start ();
            BufferedReader r = new BufferedReader (new InputStreamReader (p.getInputStream ()));
            boolean root = false;
            String line;
            while ((line = r.readLine ()) != null)
                if (line.contains ("uid=0"))
                    root = true;
            p.waitFor ();
            return root;
        }
        catch (Exception e)
        {
            return false;
        }
    }

    /**
     * Выполнить команду от root и дождаться завершения.
     *
     * @param command команда с аргументами (экранирование не требуется: строка
     *                собирается и передаётся su -c целиком)
     * @param output  если не null — построчно складывать stdout+stderr
     * @return код возврата процесса или -1, если запустить не удалось
     */
    public static int run (String[] command, List<String> output)
    {
        StringBuilder sb = new StringBuilder ();
        for (String s : command)
        {
            if (sb.length () > 0)
                sb.append (' ');
            sb.append (s);
        }

        try
        {
            Process p = new ProcessBuilder ("su", "-c", sb.toString ())
                    .redirectErrorStream (true).start ();

            BufferedReader r = new BufferedReader (new InputStreamReader (p.getInputStream ()));
            String line;
            while ((line = r.readLine ()) != null)
                if (output != null)
                    output.add (line);

            return p.waitFor ();
        }
        catch (Exception e)
        {
            return -1;
        }
    }
}
