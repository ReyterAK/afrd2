/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

import java.util.ArrayList;
import java.util.Collection;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Частоты вертикальной развёртки для настройки mode.blacklist.rates: список
 * для выбора, разбор источников (список режимов ТВ и дамп SurfaceFlinger) и
 * сборка значения в том виде, в каком его читает демон (afrd.c,
 * blacklist_rates_load: числа через пробел, 1…1000 Гц, не больше десяти
 * записей).
 *
 * Почему выбор, а не ввод текста: демон сравнивает частоту запрета с частотой
 * режима численно с допуском ±1 в 1/256 Гц (afrd.c: rate_is_blacklisted),
 * поэтому 23.976 и 24 — РАЗНЫЕ записи (6138 и 6144). Угадать, какую именно
 * частоту сообщает прошивка, по подсказке «23.976 25» нельзя, а ошибка
 * выглядит как «настройка не действует».
 *
 * Имена канонических частот — зеркало таблицы k_rates (fps_class.c): если
 * списки разойдутся, строка настроек покажет частоту, которой демон не знает.
 * Класс не зависит от Android: его проверяет host-тест tests/RateListTest.java
 * тем же javac, которым собирается APK.
 */
public class RateList
{
    /** Канонические частоты — имена как в k_rates (fps_class.c). */
    public static final String [] CANON =
    {
        "23.976", "24", "25", "29.97", "30", "50", "59.94", "60",
    };

    private static final double [] HZ =
    {
        24000.0 / 1001.0, 24.0, 25.0, 30000.0 / 1001.0, 30.0, 50.0, 60000.0 / 1001.0, 60.0,
    };

    /** Предел числа записей у демона (afrd.c: g_mode_blacklist_rates [10]). */
    public static final int MAX_ENTRIES = 10;

    /** Границы частоты, которые принимает демон (afrd.c: rate >= 1 && rate <= 1000). */
    public static final double MIN_HZ = 1.0, MAX_HZ = 1000.0;

    /** Частота в имени режима ТВ: «1080p24hz», «1080i60hz», «2160p23.976hz*». */
    private static final Pattern MODE_NAME =
        Pattern.compile ("(?im)^[ \\t]*[0-9]+[pi]([0-9]+(?:\\.[0-9]+)?)hz");

    /**
     * Пары «целая частота в имени режима — её дробный вариант». Имя режима на
     * старом и вендорном путях одно на оба варианта («1080p24hz» — это и 24.000,
     * и 23.976): дробность задаётся узлом frac_rate_policy (modes.c:
     * display_mode_add оставляет недробные, display_mode_get_current выставляет
     * fractional по этому узлу). Демон сравнивает частоту запрета с частотой
     * режима численно с допуском ±1 в 1/256 Гц, поэтому 23.976 (6138) и 24 (6144)
     * — РАЗНЫЕ записи: без дробного варианта в списке запрет «24» не отсекал
     * режим 23.976, а выбрать 23.976 из списка было нельзя (живая приёмка
     * 2026-10-15 на стенде Android 9: при mode.blacklist.rates=24 демон всё равно
     * встал на 1080p24hz как 23.98 Гц).
     */
    private static final String [][] FRACT_TWINS =
    {
        { "24", "23.976" }, { "30", "29.97" }, { "60", "59.94" },
    };

    /** Частота режима в дампе SurfaceFlinger: «refreshRate=23.98 Hz» (Android 12-14). */
    private static final Pattern SF_MODE =
        Pattern.compile ("refreshRate=([0-9]+(?:\\.[0-9]+)?)");

    /** Частота активного режима в дампе Android 10/11: «refresh-rate : 59.940061 fps». */
    private static final Pattern SF_ACTIVE =
        Pattern.compile ("refresh-rate\\s*:\\s*([0-9]+(?:\\.[0-9]+)?)");

    /**
     * Каноническое имя частоты, если она совпадает с ним в пределах округления
     * до 1/256 Гц — то есть ровно с тем допуском, с каким демон сравнивает
     * частоту запрета с частотой режима. Иначе null: чужую частоту показываем
     * как есть (и запрет по ней тоже сработает), а не подменяем ближайшей
     * канонической — подмена дала бы настройку, которая выглядит рабочей, но
     * режим не отсекает (23.98 вместо 24.00 при допуске ±1/256).
     */
    public static String canonical (double hz)
    {
        int r = (int) Math.round (hz * 256.0);

        for (int i = 0; i < HZ.length; i++)
            if (Math.abs ((int) Math.round (HZ [i] * 256.0) - r) <= 1)
                return CANON [i];

        return null;
    }

    /**
     * Запись настройки для частоты: каноническое имя, если подходит по допуску,
     * иначе само число в исходном виде (демон принимает любую частоту 1…1000).
     */
    public static String token (double hz)
    {
        String canon = canonical (hz);
        if (canon != null)
            return canon;

        /* Как и в PkgValues: число приводим к каноническому виду, но значение
         * не теряем — иначе правка, сделанная руками, исчезла бы при первом
         * сохранении настроек. */
        if (hz == Math.floor (hz) && !Double.isInfinite (hz))
            return Long.toString ((long) hz);

        return trimZeros (String.format (java.util.Locale.US, "%.3f", hz));
    }

    private static String trimZeros (String s)
    {
        while (s.endsWith ("0"))
            s = s.substring (0, s.length () - 1);

        if (s.endsWith ("."))
            s = s.substring (0, s.length () - 1);

        return s;
    }

    /** Все частоты, упомянутые в тексте источника (список режимов ТВ или дамп SF). */
    public static List<String> fromSources (String text)
    {
        LinkedHashSet<String> out = new LinkedHashSet<> ();

        if (text != null)
        {
            /* Частоты из имён режимов (disp_cap) дополняются дробными вариантами:
             * имя одно на 24.000 и 23.976 (см. FRACT_TWINS), а записи настройки
             * разные. В дампе SurfaceFlinger нового пути оба варианта приходят
             * отдельными строками, поэтому там ничего не досочиняем. */
            LinkedHashSet<String> byName = new LinkedHashSet<> ();
            addMatches (byName, MODE_NAME, text);
            out.addAll (byName);

            for (String name : byName)
                for (String [] pair : FRACT_TWINS)
                    if (pair [0].equals (name))
                        out.add (pair [1]);

            addMatches (out, SF_MODE, text);
            addMatches (out, SF_ACTIVE, text);
        }

        return sort (out);
    }

    private static void addMatches (Collection<String> out, Pattern p, String text)
    {
        Matcher m = p.matcher (text);
        while (m.find ())
        {
            double hz = parseDouble (m.group (1));
            if (inRange (hz))
                out.add (token (hz));
        }
    }

    /**
     * Записи из значения настройки: то, что демон примет (1…1000 Гц), в
     * каноническом виде; мусор не возвращается, но и не мешает — как
     * blacklist_rates_load.
     */
    public static List<String> parse (String value)
    {
        LinkedHashSet<String> out = new LinkedHashSet<> ();

        if (value != null)
            for (String tok : value.trim ().split ("\\s+"))
            {
                double hz = parseDouble (tok);
                if (inRange (hz))
                    out.add (token (hz));
            }

        return sort (out);
    }

    /** Значение настройки из выбранных частот (демон читает его через пробел). */
    public static String format (Collection<String> rates)
    {
        StringBuilder sb = new StringBuilder ();
        for (String r : rates)
        {
            if (r == null || r.isEmpty ())
                continue;
            if (sb.length () > 0)
                sb.append (' ');
            sb.append (r);
        }

        return sb.toString ();
    }

    /**
     * Список строк для диалога: частоты источника (список режимов ТВ), уже
     * выбранные записи из настройки (в том числе чужие — их нельзя терять
     * молча) — по возрастанию.
     */
    public static List<String> displayList (List<String> source, String value)
    {
        LinkedHashSet<String> out = new LinkedHashSet<> ();
        if (source != null)
            out.addAll (source);
        out.addAll (parse (value));

        return sort (out);
    }

    /**
     * Оставить не больше MAX_ENTRIES записей (столько хранит демон; лишние он
     * молча выбрасывает). Порядок сохраняется — как в списке для диалога.
     */
    public static List<String> trim (Collection<String> rates)
    {
        List<String> out = new ArrayList<> ();
        for (String r : rates)
        {
            if (out.size () >= MAX_ENTRIES)
                break;
            out.add (r);
        }

        return out;
    }

    /** Числовое значение записи (для порядка в списке); NaN — не число. */
    public static double value (String token)
    {
        return parseDouble (token);
    }

    private static List<String> sort (Collection<String> rates)
    {
        List<String> out = new ArrayList<> (rates);
        java.util.Collections.sort (out, new java.util.Comparator<String> ()
        {
            @Override
            public int compare (String a, String b)
            {
                double da = parseDouble (a), db = parseDouble (b);
                if (da < db)
                    return -1;
                if (da > db)
                    return 1;
                return a.compareTo (b);
            }
        });

        return out;
    }

    private static boolean inRange (double hz)
    {
        return !Double.isNaN (hz) && (hz >= MIN_HZ) && (hz <= MAX_HZ);
    }

    /* Разбор числа как sscanf("%f") у демона: берётся числовой префикс токена,
     * хвост игнорируется («50d» → 50, «6e1» → 60). Значение, которое демон
     * примет, нельзя терять — иначе настройка молча исчезнет при сохранении. */
    private static double parseDouble (String tok)
    {
        if (tok == null)
            return Double.NaN;

        Matcher m = Pattern.compile ("^[+-]?(?:[0-9]+\\.?[0-9]*|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?")
            .matcher (tok.trim ());
        if (!m.find () || (m.start () != 0))
            return Double.NaN;

        try
        {
            return Double.parseDouble (m.group ());
        }
        catch (NumberFormatException e)
        {
            return Double.NaN;
        }
    }
}
