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
    /*
     * Кандидаты в su, в порядке предпочтения.
     *
     * Почему не просто "su": на стоковых userdebug-прошивках Amlogic (например,
     * ATV14 в боксе .41) в PATH первым стоит старый AOSP-su из
     * /system/xbin/su, который не понимает `-c` и отвечает «invalid uid/gid».
     * Magisk при этом кладёт свой su в /debug_ramdisk/su, и приложение,
     * вызывающее просто "su", получает «root недоступен», хотя root есть.
     * Проверка идёт по факту: кандидат считается рабочим, только если вернул
     * uid=0 (живая находка 2026-10-18).
     */
    private static final String [] kSuCandidates =
    {
        "/debug_ramdisk/su",	/* Magisk на Android 11+ (GKI, init_boot) */
        "/sbin/su",		/* Magisk на старых схемах загрузки */
        "/system/bin/su",	/* MagiskSU на части прошивок */
        "/system/xbin/su",	/* классический AOSP-su */
        "su"			/* что найдётся в PATH */
    };

    /** Найденный рабочий su (null — ещё не найден). */
    private static String sSu = null;

    /**
     * Путь к рабочему su или null.
     *
     * Положительный результат запоминается на процесс; отрицательный — НЕ
     * запоминается: пользователь может выдать Magisk разрешение позже, и тогда
     * следующая проверка должна снова перебрать кандидатов (на стенде .41
     * установка была заблокирована и разрешалась вручную — живая находка).
     */
    private static String findSu ()
    {
        if (sSu != null)
            return sSu;

        for (String cand : kSuCandidates)
        {
            try
            {
                Process p = new ProcessBuilder (cand, "-c", "id")
                        .redirectErrorStream (true).start ();
                BufferedReader r = new BufferedReader (new InputStreamReader (p.getInputStream ()));
                boolean root = false;
                String line;
                while ((line = r.readLine ()) != null)
                    if (line.contains ("uid=0"))
                        root = true;
                p.waitFor ();
                if (root)
                {
                    sSu = cand;
                    return sSu;
                }
            }
            catch (Exception ignored)
            {
            }
        }

        return null;
    }

    /** Сбросить запомненный путь (например, после выдачи root пользователем). */
    public static void resetProbe ()
    {
        sSu = null;
    }

    /** Есть ли рабочий root (su отвечает uid=0). */
    public static boolean suAvailable ()
    {
        return findSu () != null;
    }

    /**
     * Выполнить команды от root и дождаться завершения.
     *
     * Как в libsuperuser: запускается один `su`, каждая строка массива пишется
     * ему в stdin отдельной командой (это важно — Control.extractDaemon
     * передаёт двумя элементами «<path> -k» и «rm -f <path>»; склеивание их
     * пробелом ломало вторую команду).
     *
     * @param commands команды (каждый элемент — отдельная строка для su)
     * @param output   если не null — построчно складывать stdout+stderr
     * @return код возврата su или -1, если запустить не удалось
     */
    public static int run (String[] commands, List<String> output)
    {
        try
        {
            String su = findSu ();
            if (su == null)
                return -1;

            Process p = new ProcessBuilder (su).redirectErrorStream (true).start ();

            java.io.OutputStream os = p.getOutputStream ();
            for (String c : commands)
            {
                os.write (c.getBytes ());
                os.write ('\n');
            }
            os.flush ();
            os.close ();

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
