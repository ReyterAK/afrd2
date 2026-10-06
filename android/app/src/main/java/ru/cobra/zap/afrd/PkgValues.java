/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.regex.Pattern;

/**
 * Значение настройки вида «пакет=число» (mode.min.hz.list): разбор и сборка
 * ровно в том виде, в каком значение читает демон (afrd.c, mode_min_hz_load).
 * Разница в поведении здесь стоила бы пользователю молча потерянной
 * настройки, поэтому правила повторяют демон: записи через пробел, число
 * в диапазоне 1…1000 (для ЧАСТОТЫ), не более 8 записей.
 *
 * Класс намеренно не зависит от Android: его проверяет host-тест
 * tests/PkgValuesTest.java тем же javac, которым собирается APK.
 */
public class PkgValues
{
    /** Границы частоты для mode.min.hz.list (afrd.c: hz >= 1 && hz <= 1000). */
    public static final double MIN_HZ = 1.0;
    public static final double MAX_HZ = 1000.0;

    /** Предел числа записей у демона (afrd.c: MODE_MIN_HZ_MAX). */
    public static final int MAX_ENTRIES = 8;

    /* Синтаксис числа, который принимает sscanf("%f") в демоне: обычная или
     * экспоненциальная запись. Отсекает то, что принял бы Double.parseDouble,
     * но демон — нет: шестнадцатеричное (0x1p3), хвостовое d/f (50d),
     * NaN, Infinity. */
    private static final Pattern NUMBER =
        Pattern.compile ("[+-]?(\\d+(\\.\\d*)?|\\.\\d+)([eE][+-]?\\d+)?");

    /**
     * Разбор значения. Порядок записей сохраняется; повтор пакета оставляет
     * первую запись (как mode_min_hz_for в демоне). Записи, которые демон
     * игнорирует (число не разбирается, вне диапазона, пустое имя пакета),
     * отбрасываются; запись без числа сохраняется с пустым значением — демон
     * её тоже игнорирует, но GUI не должен молча удалять текст пользователя.
     */
    public static LinkedHashMap<String, String> parse (String value, double min, double max)
    {
        LinkedHashMap<String, String> out = new LinkedHashMap<> ();
        if (value == null)
            return out;

        for (String token : value.trim ().split ("\\s+"))
        {
            if (token.isEmpty ())
                continue;

            int eq = token.indexOf ('=');
            String pkg = (eq < 0) ? token : token.substring (0, eq);
            String num = (eq < 0) ? "" : token.substring (eq + 1);
            if (pkg.isEmpty () || out.containsKey (pkg))
                continue;
            if (!num.isEmpty () && !inRange (num, min, max))
                continue;

            out.put (pkg, num);
        }

        return out;
    }

    /** Сборка значения: записи через пробел, порядок сохраняется. */
    public static String format (Map<String, String> values)
    {
        StringBuilder sb = new StringBuilder ();
        for (Map.Entry<String, String> e : values.entrySet ())
        {
            if (sb.length () > 0)
                sb.append (' ');
            sb.append (e.getKey ());
            if ((e.getValue () != null) && !e.getValue ().isEmpty ())
                sb.append ('=').append (e.getValue ());
        }

        return sb.toString ();
    }

    /** Число в диапазоне min…max, записанное так, как его поймёт демон. */
    public static boolean inRange (String text, double min, double max)
    {
        if (text == null)
            return false;

        String s = text.trim ();
        if (!NUMBER.matcher (s).matches ())
            return false;

        try
        {
            double v = Double.parseDouble (s);
            return !Double.isNaN (v) && !Double.isInfinite (v) && (v >= min) && (v <= max);
        }
        catch (NumberFormatException exc)
        {
            return false;
        }
    }
}
