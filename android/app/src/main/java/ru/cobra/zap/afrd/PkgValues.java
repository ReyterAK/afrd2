/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Значение настройки вида «пакет=число» (mode.min.hz.list): разбор и сборка
 * ровно в том виде, в каком значение читает демон (afrd.c, mode_min_hz_load).
 * Разница в поведении здесь стоила бы пользователю молча потерянной
 * настройки, поэтому правила повторяют демон: записи через пробел, число
 * разбирается как sscanf("%f") (самый длинный числовой префикс токена),
 * допустимы 1…1000 Гц, записей с числом — не более восьми.
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

    /**
     * Разбор значения. Порядок записей сохраняется; повтор пакета оставляет
     * первую запись (как mode_min_hz_for в демоне). Записи, которые демон
     * игнорирует (числа нет, оно вне диапазона, пустое имя пакета),
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

            if (!num.isEmpty ())
            {
                String parsed = number (num, min, max);
                if (parsed == null)
                    continue;

                /* Форма приводится к канонической: «50d» демон читает как 50, и
                 * строка настроек должна показывать то же самое число. */
                num = parsed;
            }

            out.put (pkg, num);
        }

        return out;
    }

    /**
     * Сколько записей занимают слоты демона: он хранит только записи с числом
     * (afrd.c: if (eq && cur [0])), поэтому лимит MODE_MIN_HZ_MAX считается по ним.
     */
    public static int filledCount (Map<String, String> values)
    {
        int n = 0;
        for (String v : values.values ())
            if ((v != null) && !v.isEmpty ())
                n++;

        return n;
    }

    /**
     * Обрезка до предела записей, которые демон хранит: лишние (сверх max
     * заполненных) отбрасываются, порядок сохраняется, записи без числа
     * остаются — слотов у демона они не занимают.
     */
    public static LinkedHashMap<String, String> trim (Map<String, String> values, int max)
    {
        LinkedHashMap<String, String> out = new LinkedHashMap<> ();
        int filled = 0;

        for (Map.Entry<String, String> e : values.entrySet ())
        {
            boolean hasValue = (e.getValue () != null) && !e.getValue ().isEmpty ();
            if (hasValue)
            {
                if (filled >= max)
                    continue;

                filled++;
            }

            out.put (e.getKey (), e.getValue ());
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

    /** Есть ли в тексте число, которое демон примет в этом диапазоне. */
    public static boolean inRange (String text, double min, double max)
    {
        return number (text, min, max) != null;
    }

    /**
     * Число из текста так, как его разберёт sscanf ("%f") в демоне: берётся
     * самый длинный числовой префикс токена, хвост игнорируется («50d» → 50,
     * «50,» → 50, «0x1p3» → 8). Возврат — каноническая запись числа, или null,
     * если числа нет либо оно вне диапазона min…max (NaN и бесконечность демон
     * отбрасывает тем же сравнением с диапазоном).
     */
    public static String number (String text, double min, double max)
    {
        if (text == null)
            return null;

        String s = text.trim ();
        for (int len = s.length (); len > 0; len--)
        {
            Double v = parsePrefix (s.substring (0, len));
            if (v == null)
                continue;

            if ((v >= min) && (v <= max))
                return canonical (v);

            /* Префикс разобран, но вне диапазона: любой более короткий префикс
             * даст то же число (или часть его) — подходящего не будет. */
            return null;
        }

        return null;
    }

    /* Разбор строки целиком как числа: обычная или экспоненциальная запись,
     * а также hex-float (Double.parseDouble понимает «0x1p3» — как и strtod
     * на bionic, которым пользуется sscanf в демоне). «Infinity»/«NaN»
     * отсекаются по первому символу: демон их не принимает. */
    private static Double parsePrefix (String s)
    {
        if (s.isEmpty ())
            return null;

        char c = s.charAt (0);
        if (!(((c >= '0') && (c <= '9')) || (c == '+') || (c == '-') || (c == '.')))
            return null;

        try
        {
            double v = Double.parseDouble (s);
            if (Double.isNaN (v) || Double.isInfinite (v))
                return null;

            return v;
        }
        catch (NumberFormatException exc)
        {
            return null;
        }
    }

    /* Каноническая запись: целые без «.0», чтобы строка настроек показывала
     * «50 Гц», а не «50.0 Гц». */
    private static String canonical (double v)
    {
        if ((v == Math.floor (v)) && (Math.abs (v) < 1e15))
            return Long.toString ((long) v);

        return Double.toString (v);
    }
}
