/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

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
     * Проба. Возврат: FracProbe.UNKNOWN — не получилось (нет root, узлы не
     * читаются, режим не сменился); иначе HELD или FRACT.
     *
     * @param cacheDir каталог приложения: рядом с ним лежит вендорный помощник
     *                 afrd-disp.sh (Android 10/11), на старом пути его нет
     */
    public static int measure (File cacheDir)
    {
        if (!supported ())
            return FracProbe.UNKNOWN;

        List<String> out = new ArrayList<> ();
        if (RootShell.run (new String [] {
                "echo M=$(cat " + MODE_NODE + ")",
                "echo P=$(cat " + FRAC_NODE + ")",
                "echo N=$(grep -c hz " + DISP_CAP + " 2>/dev/null)",
            }, out) != 0)
            return FracProbe.UNKNOWN;

        String mode = FracProbe.tagValue (out, "M=");
        String policy = FracProbe.tagValue (out, "P=");
        if (mode.isEmpty () || policy.isEmpty ())
            return FracProbe.UNKNOWN;

        String target = FracProbe.targetMode (modeNames (), mode);
        if (target == null)
            return FracProbe.UNKNOWN;

        boolean viaHelper = new File (cacheDir, "afrd-disp.sh").exists ();
        String applyTarget = applyCmd (viaHelper, cacheDir, target);

        List<String> probe = new ArrayList<> ();
        RootShell.run (new String [] {
                "echo 0 > " + FRAC_NODE,
                applyTarget,
                "sleep 3",
                "echo P2=$(cat " + FRAC_NODE + ")",
                "echo F2=$(grep -m1 'frac:' " + VINFO_NODE + ")",
                "echo M2=$(cat " + MODE_NODE + ")",
            }, probe);

        String policyAfter = FracProbe.tagValue (probe, "P2=");
        String fracAfter = FracProbe.tagValue (probe, "F2=");
        String modeAfter = FracProbe.tagValue (probe, "M2=");

        /* Возврат в исходное состояние — в любом случае, даже если проба
         * не удалась: режим и политику пользователь не заказывал. */
        RootShell.run (new String [] {
                "echo " + policy + " > " + FRAC_NODE,
                applyCmd (viaHelper, cacheDir, mode),
                "sleep 2",
            }, new ArrayList<String> ());

        /* Режим не сменился — значит и политика не применялась, мерить нечего. */
        if (!target.equals (modeAfter))
            return FracProbe.UNKNOWN;

        boolean stuckZero = "0".equals (policyAfter.trim ());
        boolean fracZero = FracProbe.fracZero (fracAfter);

        return FracProbe.verdict (stuckZero, fracZero);
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
