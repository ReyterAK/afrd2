/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

import android.util.Log;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/**
 * Проба «какую частоту режим отдаёт на самом деле» (Android 6–11).
 *
 * Пассивного источника нет: `disp_cap`/`cea_cap` перечисляют только имена
 * режимов (номиналы), `vinfo` знает лишь текущий тайминг. Поэтому проба
 * активная — ровно та же пара действий, что делает демон при переключении:
 *
 *  1. запомнить текущий режим и значение `frac_rate_policy`;
 *  2. поставить политику 0 (просим целую частоту) и применить режим класса
 *     24/30/60, отличный от текущего (повтор того же имени вендорный HAL
 *     считает «режим не сменился» и возвращает ошибку 4);
 *  3. прочитать узел и `vinfo`: узел остался 0 и `frac: 0` — целая доступна;
 *     узел вернулся в 1 (или `frac: 1`) — на деле только дробная;
 *  4. вернуть прежнюю политику и прежний режим.
 *
 * Цена — два переключения режима, то есть заметное мигание; поэтому проба
 * только по кнопке, а не при открытии настроек. Результат — FracProbe.HELD /
 * FRACT / UNKNOWN.
 */
public class RatesProbe
{
    private static final String TAG = "afrd2";

    public static final String FRAC_NODE = "/sys/class/amhdmitx/amhdmitx0/frac_rate_policy";
    public static final String MODE_NODE = "/sys/class/display/mode";
    public static final String VINFO_NODE = "/sys/class/display/vinfo";
    public static final String DISP_CAP = "/sys/class/amhdmitx/amhdmitx0/disp_cap";

    /** Есть ли на боксе чем мерить (оба узла на месте). */
    public static boolean supported ()
    {
        return new File (FRAC_NODE).exists () && new File (MODE_NODE).exists ();
    }

    /**
     * Режим задаётся таблицей SurfaceFlinger (Android 12-14), а не вендорным
     * SystemControl: проба там не нужна и не работает — список частот уже
     * строится из настоящих режимов таблицы (23.98 и 24.00 приходят
     * отдельными строками), а `afrd-disp.sh` на этом пути режим не меняет.
     * Живой случай `.50`: проба билась в «не удалось», потому что запись в
     * узел режима/помощник на backdoor-пути ничего не делает (журнал §208).
     */
    public static boolean backdoorPath ()
    {
        List<String> out = new ArrayList<> ();
        if (RootShell.run (new String [] {
                "dumpsys SurfaceFlinger | grep -c 'refreshRate='",
            }, out) != 0 && out.isEmpty ())
            return false;

        for (String line : out)
        {
            try
            {
                if (Integer.parseInt (line.trim ()) >= 2)
                    return true;
            }
            catch (NumberFormatException ignored)
            {
            }
        }

        return false;
    }

    /**
     * Проба. Возврат: FracProbe.UNKNOWN — не получилось (нет root, узлы не
     * читаются, режим не сменился); иначе HELD или FRACT.
     *
     * @param cacheDir каталог приложения: рядом с ним лежит вендорный помощник
     *                 afrd-disp.sh (Android 10/11), на старом пути его нет
     */
    public static int measure (File cacheDir)
    {
        if (!supported ())
        {
            Log.i (TAG, "проба: нет узлов " + FRAC_NODE + " / " + MODE_NODE);
            return FracProbe.UNKNOWN;
        }

        /* Шаги нарочно раздельными вызовами: су-сессия короткоживущая, а сон
         * внутри неё увеличивает шанс, что читатель упрётся в срок. */
        List<String> out = new ArrayList<> ();
        int rc = RootShell.run (new String [] {
                "echo M=$(cat " + MODE_NODE + ")",
                "echo P=$(cat " + FRAC_NODE + ")",
            }, out);
        Log.i (TAG, "проба: чтение rc=" + rc + " out=" + out);

        String mode = FracProbe.tagValue (out, "M=");
        String policy = FracProbe.tagValue (out, "P=");
        if (mode.isEmpty () || policy.isEmpty ())
            return FracProbe.UNKNOWN;

        List<String> names = new ArrayList<> ();
        RootShell.run (new String [] { "cat " + DISP_CAP }, names);
        List<String> modes = FracProbe.modeNamesFromLines (names);
        String target = FracProbe.targetMode (modes, mode);
        Log.i (TAG, "проба: режим=" + mode + " политика=" + policy + " режимов=" + modes.size ()
                   + " цель=" + target + " cache=" + cacheDir.getAbsolutePath ()
                   + " helper=" + new File (cacheDir, "afrd-disp.sh").exists ());
        if (target == null)
            return FracProbe.UNKNOWN;

        boolean viaHelper = new File (cacheDir, "afrd-disp.sh").exists ();

        List<String> apply = new ArrayList<> ();
        RootShell.run (new String [] {
                "echo 0 > " + FRAC_NODE,
                applyCmd (viaHelper, cacheDir, target) + "; echo RC=$?",
            }, apply);
        Log.i (TAG, "проба: применение " + target + " out=" + apply);

        sleep (3000);

        List<String> after = new ArrayList<> ();
        RootShell.run (new String [] {
                "echo P2=$(cat " + FRAC_NODE + ")",
                "echo F2=$(grep -m1 'frac:' " + VINFO_NODE + ")",
                "echo M2=$(cat " + MODE_NODE + ")",
            }, after);
        Log.i (TAG, "проба: после применения out=" + after);

        String policyAfter = FracProbe.tagValue (after, "P2=");
        String fracAfter = FracProbe.tagValue (after, "F2=");
        String modeAfter = FracProbe.tagValue (after, "M2=");

        /* Запасной путь: помощник не сменил режим (или его нет, а путь
         * вендорный) — пробуем запись в узел режима. На Android 10/11 это
         * обычно не работает, но проверить дешевле, чем вернуть «не удалось». */
        if (!target.equals (modeAfter))
        {
            List<String> direct = new ArrayList<> ();
            RootShell.run (new String [] {
                    "echo " + target + " > " + MODE_NODE + "; echo RC=$?",
                }, direct);
            sleep (2500);

            after = new ArrayList<> ();
            RootShell.run (new String [] {
                    "echo P3=$(cat " + FRAC_NODE + ")",
                    "echo F3=$(grep -m1 'frac:' " + VINFO_NODE + ")",
                    "echo M3=$(cat " + MODE_NODE + ")",
                }, after);
            Log.i (TAG, "проба: запись в узел out=" + direct + " after=" + after);

            String m3 = FracProbe.tagValue (after, "M3=");
            if (target.equals (m3))
            {
                policyAfter = FracProbe.tagValue (after, "P3=");
                fracAfter = FracProbe.tagValue (after, "F3=");
                modeAfter = m3;
            }
        }

        /* Возврат в исходное состояние — в любом случае: режим и политику
         * пользователь не заказывал. */
        RootShell.run (new String [] {
                "echo " + policy + " > " + FRAC_NODE,
                applyCmd (viaHelper, cacheDir, mode),
            }, new ArrayList<String> ());

        if (!target.equals (modeAfter))
        {
            Log.i (TAG, "проба: режим не сменился (ждали " + target + ", получили " + modeAfter + ")");
            return FracProbe.UNKNOWN;
        }

        boolean stuckZero = "0".equals (policyAfter.trim ());
        boolean fracZero = FracProbe.fracZero (fracAfter);
        Log.i (TAG, "проба: узел=" + policyAfter + " frac=" + fracAfter
                   + " -> " + (FracProbe.verdict (stuckZero, fracZero) == FracProbe.HELD
                               ? "целые доступны" : "только дробные"));

        return FracProbe.verdict (stuckZero, fracZero);
    }

    private static void sleep (long ms)
    {
        try
        {
            Thread.sleep (ms);
        }
        catch (InterruptedException ignored)
        {
        }
    }

    /** Команда применения режима: вендорный помощник или запись в узел. */
    private static String applyCmd (boolean viaHelper, File cacheDir, String mode)
    {
        if (viaHelper)
            return "cd " + cacheDir + " && sh afrd-disp.sh " + mode;

        return "echo " + mode + " > " + MODE_NODE;
    }

    /** Имена режимов из disp_cap (разбор — FracProbe.modeNamesFromLines). */
    private static List<String> modeNames ()
    {
        List<String> out = new ArrayList<> ();
        RootShell.run (new String [] { "cat " + DISP_CAP }, out);
        return FracProbe.modeNamesFromLines (out);
    }
}
