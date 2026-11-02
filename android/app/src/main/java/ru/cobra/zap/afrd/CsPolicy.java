/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Политики цветового пространства для настройки cs.select: то, что выбирает
 * пользователь в настройках, в значение конфига (список правил «режим=формат»,
 * читает colorspace.c: colorspace_parse_filter).
 *
 * Все политики содержат правило защиты полосы 4K: на 3840x2160 при 50/60 Гц
 * сигнал 4:4:4 (и тем более RGB) физически не пролезает в полосу HDMI, поэтому
 * «Авто» — это не «ничего не делать», а ровно защита 4K плюс невмешательство в
 * остальные режимы (их формат выбирают система и плеер). Инвариант проверяет
 * host-тест tests/CsPolicyTest.java.
 *
 * Класс не зависит от Android: подписи пунктов живут в строках ресурсов
 * (SettingsFragment), здесь — только значения.
 */
public class CsPolicy
{
    /** Правило защиты полосы 4K (3840x2160 при 40 Гц и выше). */
    public static final String BASE_4K = "2160p[4-9].*=420";

    /** Статический набор: авто, RGB, YUV 4:4:4 (умолчание и откат). */
    public static final String [] VALUES =
    {
        BASE_4K,
        BASE_4K + " .*=rgb",
        BASE_4K + " .*=444",
    };

    public static final int AUTO = 0, RGB = 1, YUV444 = 2;

    /**
     * Пункты выбора по списку форматов ТВ (CsFormats): «Авто» плюс по одному
     * пункту на каждый поддерживаемый формат. Формат идёт без глубины и
     * диапазона — их оставляем системе; правило 4K остаётся первым в каждом
     * пункте. Пустой список форматов — статический набор VALUES.
     */
    public static List<String> valuesFor (List<String> formats)
    {
        if ((formats == null) || formats.isEmpty ())
            return Arrays.asList (VALUES);

        List<String> out = new ArrayList<> ();
        out.add (BASE_4K);

        for (String f : formats)
        {
            if ((f == null) || f.isEmpty ())
                continue;

            String v = BASE_4K + " .*=" + f;
            if (!out.contains (v))
                out.add (v);
        }

        return out;
    }

    /** Номер пункта по значению настройки; -1 — значение не из списка (свой текст). */
    public static int indexOf (String value)
    {
        return indexOf (value, Arrays.asList (VALUES));
    }

    /** То же по произвольному списку пунктов (он строится из возможностей ТВ). */
    public static int indexOf (String value, List<String> values)
    {
        if (value == null)
            return -1;

        /* Сравнение без учёта порядка и лишних пробелов: значение могло быть
         * записано руками или раньше, из текстового поля. */
        String norm = normalize (value);
        for (int i = 0; i < values.size (); i++)
            if (normalize (values.get (i)).equals (norm))
                return i;

        return -1;
    }

    public static String valueAt (int index)
    {
        return ((index >= 0) && (index < VALUES.length)) ? VALUES [index] : null;
    }

    private static String normalize (String value)
    {
        return value.trim ().replaceAll ("\\s+", " ");
    }
}
