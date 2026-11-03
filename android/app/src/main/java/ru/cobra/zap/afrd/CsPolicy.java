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
 * Все политики, кроме явного «Не изменять формат», содержат правило защиты
 * полосы 4K: на 3840x2160 при 50/60 Гц сигнал 4:4:4 (и тем более RGB) физически
 * не пролезает в полосу HDMI, поэтому «Авто» — это не «ничего не делать», а
 * ровно защита 4K плюс невмешательство в остальные режимы (их формат выбирают
 * система и плеер). «Не изменять» — это пустое значение cs.select, и оно
 * означает ровно то, что написано: демон не пишет формат ни разу. Инварианты
 * проверяет host-тест tests/CsPolicyTest.java.
 *
 * Класс не зависит от Android: подписи пунктов живут в строках ресурсов
 * (SettingsFragment), здесь — только значения.
 */
public class CsPolicy
{
    /** Правило защиты полосы 4K (3840x2160 при 40 Гц и выше). */
    public static final String BASE_4K = "2160p[4-9].*=420";

    /** «Не изменять формат»: пустое значение cs.select. Демон с пустым набором
     * правил формат не трогает вообще — ни правила, ни default (см.
     * colorspace_apply). Пункт стоит первым, чтобы «ничего не делать» было
     * видимым выбором, а не отсутствием настройки. */
    public static final String NONE_VALUE = "";

    /** Статический набор: не изменять, авто, RGB, YUV 4:4:4 (умолчание — «авто»,
     * это второй пункт; откат при нечитаемом dc_cap — весь набор). */
    public static final String [] VALUES =
    {
        NONE_VALUE,
        BASE_4K,
        BASE_4K + " .*=rgb",
        BASE_4K + " .*=444",
    };

    /**
     * Пункты выбора по списку форматов ТВ (CsFormats): «Не изменять», «Авто»
     * плюс по одному пункту на каждый поддерживаемый формат. Формат идёт без
     * глубины и диапазона — их оставляем системе; правило 4K стоит первым в
     * каждом пункте, кроме «Не изменять». Пустой список форматов — статический
     * набор VALUES.
     */
    public static List<String> valuesFor (List<String> formats)
    {
        if ((formats == null) || formats.isEmpty ())
            return Arrays.asList (VALUES);

        List<String> out = new ArrayList<> ();
        out.add (NONE_VALUE);
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

    /** Номер пункта по произвольному списку пунктов (он строится из
     * возможностей ТВ); -1 — значение не из списка (свой текст). */
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

    private static String normalize (String value)
    {
        return value.trim ().replaceAll ("\\s+", " ");
    }
}
