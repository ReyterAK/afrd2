/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

import java.util.List;

/**
 * Какую частоту режим отдаёт на самом деле — подписи для списка
 * «Запрещённые частоты» и правило разбора результата пробы.
 *
 * Зачем это нужно. На Android 6–11 имена режимов — номиналы класса:
 * `1080p24hz` — это и 24.000, и 23.976, а фактическую частоту задаёт узел
 * `frac_rate_policy` в момент применения режима. Поведение узла — свойство
 * прошивки: часть боксов держит запрошенное значение, часть возвращает своё
 * (живой `.42`: писали 0, после применения снова 1, фактическая частота
 * 23.98 — `vinfo`, `sync_duration 2997/125`). Пассивно этого не узнать:
 * `disp_cap`/`cea_cap` отдают только имена, `vinfo` — только текущий тайминг.
 * Поэтому список подписывается результатом одной пробы (см. RatesProbe), а до
 * неё — номиналом.
 *
 * Класс без Android-зависимостей: его проверяет tests/FracProbeTest.java.
 */
public class FracProbe
{
    /** Не измеряли: показываем номинал. */
    public static final int UNKNOWN = -1;

    /** Драйвер держит политику 0 — доступны и целые частоты (24.000 и т. д.). */
    public static final int HELD = 0;

    /** Драйвер возвращает своё значение — на деле только дробные (23.976). */
    public static final int FRACT = 1;

    private static final String [] TWINS = { "24", "30", "60" };
    private static final String [] FRACTS = { "23.976", "29.97", "59.94" };

    /** Есть ли у частоты дробный представитель (24/30/60 — да, 25/50 — нет). */
    public static boolean hasTwin (String token)
    {
        for (String t : TWINS)
            if (t.equals (token))
                return true;
        return false;
    }

    /** Дробный представитель пары; null — пары нет. */
    public static String twinName (String token)
    {
        for (int i = 0; i < TWINS.length; i++)
            if (TWINS [i].equals (token))
                return FRACTS [i];
        return null;
    }

    /**
     * Результат пробы: писали политику 0 и смотрели, что вышло.
     *
     * @param policyStuckZero узел после применения режима остался 0
     * @param vinfoFracZero   фактическая частота стала целой (`vinfo: frac: 0`)
     *
     * Целая доступна, только если верно и то, и другое: узел может остаться 0,
     * а тайминги всё равно прийти дробные (HAL собрал режим по-своему).
     */
    public static int verdict (boolean policyStuckZero, boolean vinfoFracZero)
    {
        return (policyStuckZero && vinfoFracZero) ? HELD : FRACT;
    }

    /**
     * Подпись строки списка частот.
     *
     * Измерено «только дробные» — показываем фактическую частоту: именно она
     * будет на экране, и именно её пользователь хочет запретить.
     * Измерено «доступны и целые» — показываем обе: какую из двух возьмут,
     * решает контент (23.976 или 24.000).
     * Не измерено — номинал, без выдумок.
     */
    public static String label (String token, int verdict)
    {
        String frac = twinName (token);
        if (frac == null)
            return token;

        if (verdict == FRACT)
            return frac;
        if (verdict == HELD)
            return frac + " / " + token;
        return token;
    }

    /**
     * Имя режима для пробы: класс 24/30/60, отличный от текущего (повтор того же
     * имени вендорный HAL считает «режим не сменился» и возвращает ошибку 4).
     * Предпочитаем 1080p — он есть почти на любом ТВ.
     */
    public static String targetMode (List<String> names, String current)
    {
        if (names == null)
            return null;

        String best = null;
        for (String name : names)
        {
            if (name == null)
                continue;
            if (name.equals (current))
                continue;
            if (!isTwinModeName (name))
                continue;
            if (name.startsWith ("1080p"))
                return name;
            if (best == null)
                best = name;
        }

        return best;
    }

    /** Имя режима класса 24/30/60 (`1080p24hz`, `2160p60hz`, `1080p30hz`). */
    private static boolean isTwinModeName (String name)
    {
        String base = name.endsWith ("hz") ? name.substring (0, name.length () - 2) : name;
        int p = base.lastIndexOf ('p');
        if (p < 0)
            return false;

        try
        {
            int hz = Integer.parseInt (base.substring (p + 1));
            return (hz == 24) || (hz == 30) || (hz == 60);
        }
        catch (NumberFormatException e)
        {
            return false;
        }
    }

    /** Текст вердикта для сводки настройки. */
    public static String verdictText (int verdict, String fractList, String heldList)
    {
        if (verdict == FRACT)
            return fractList;
        if (verdict == HELD)
            return heldList;
        return null;
    }

    /* ---- Разбор вывода пробы (без Android: проверяется host-тестом) ---- */

    /** Значение помеченной строки вывода (`M=1080p60hz` → `1080p60hz`). */
    public static String tagValue (List<String> lines, String tag)
    {
        if (lines == null)
            return "";

        for (String line : lines)
        {
            String s = line.trim ();
            int p = s.indexOf (tag);
            if (p >= 0)
                return s.substring (p + tag.length ()).trim ();
        }

        return "";
    }

    /** `vinfo` сообщает целую частоту: строка вида `    frac:                  0`. */
    public static boolean fracZero (String vinfoLine)
    {
        return (vinfoLine != null) && vinfoLine.matches (".*frac:\\s*0\\b.*");
    }

    /** Имена режимов из вывода `cat disp_cap` (звёздочка — «поддерживается ТВ»). */
    public static List<String> modeNamesFromLines (List<String> lines)
    {
        List<String> names = new java.util.ArrayList<> ();
        if (lines == null)
            return names;

        for (String line : lines)
        {
            String s = line.trim ();
            if (s.endsWith ("*"))
                s = s.substring (0, s.length () - 1);
            if (s.matches ("[0-9]+[pi][0-9]+hz"))
                names.add (s);
        }

        return names;
    }
}
