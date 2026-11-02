/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Форматы цветового пространства, которые реально поддерживает ТВ: разбор
 * узла dc_cap (тот же путь, что cs.list.path в профилях демона).
 *
 * Настройка «Формат HDMI» должна показывать только те форматы, которые демон
 * сможет применить: colorspace_apply сверяет правило с этим же узлом
 * (colorspace_supported) и молча пропускает формат, которого там нет. На
 * разных ТВ набор разный: например, YUV 4:2:2 может отсутствовать вовсе или
 * быть только в 12 битах.
 *
 * Строки узла — комбинации «формат,глубина[,диапазон]», по одной на строку.
 * Настройка выбирает только формат; глубину и диапазон оставляем системе,
 * поэтому комбинации сворачиваются по формату с сохранением порядка узла.
 * Пустой результат (узла нет, нет прав, незнакомый формат) — сигнал настройке
 * откатиться к статическому набору CsPolicy.VALUES.
 *
 * Класс не зависит от Android: host-тест tests/CsPolicyTest.java.
 */
public class CsFormats
{
    /** Узел EDID со списком поддерживаемых форматов/глубин. */
    public static final String NODE = "/sys/class/amhdmitx/amhdmitx0/dc_cap";

    /** Имена форматов — ровно те, что разбирает демон (colorspace.c: parse_cs). */
    private static final String [] KNOWN = { "rgb", "422", "444", "420" };

    /** Содержимое узла; пустая строка — не прочитался. */
    public static String read ()
    {
        StringBuilder sb = new StringBuilder ();
        try
        {
            BufferedReader r = new BufferedReader (new InputStreamReader (
                new FileInputStream (NODE), StandardCharsets.UTF_8));
            String line;
            while ((line = r.readLine ()) != null)
                sb.append (line).append ('\n');
            r.close ();
        }
        catch (Exception ignored)
        {
        }

        return sb.toString ();
    }

    /** Уникальные поддерживаемые форматы в порядке узла. */
    public static List<String> parse (String text)
    {
        List<String> out = new ArrayList<> ();
        if (text == null)
            return out;

        for (String line : text.split ("\\r?\\n"))
        {
            String s = line.trim ();
            if (s.isEmpty ())
                continue;

            int comma = s.indexOf (',');
            String fmt = ((comma >= 0) ? s.substring (0, comma) : s).trim ()
                .toLowerCase (Locale.US);

            if (!isKnown (fmt) || out.contains (fmt))
                continue;

            out.add (fmt);
        }

        return out;
    }

    private static boolean isKnown (String fmt)
    {
        for (String k : KNOWN)
            if (k.equals (fmt))
                return true;

        return false;
    }
}
