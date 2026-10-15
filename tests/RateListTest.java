package ru.cobra.zap.afrd;

import java.io.BufferedReader;
import java.io.FileReader;
import java.util.Arrays;
import java.util.List;

/**
 * Host-тест частот для настройки «Список запрещённых частот» (mode.blacklist.rates,
 * см. tests/run_tests.sh). Проверяет, что список в настройках — это ровно те
 * частоты, которые демон сможет отсечь (afrd.c: rate_is_blacklisted, допуск
 * ±1 в 1/256 Гц), и что значение настройки не теряет написанное руками.
 *
 * Источники берутся настоящие: список режимов ТВ снимался с живого бокса,
 * дампы SurfaceFlinger — те же фикстуры, что у host-теста демона
 * (tests/test_sfmodes.c), поэтому расхождение «GUI видит одно, демон другое»
 * здесь и ловится.
 */
public class RateListTest
{
    private static void eq (String what, Object got, Object want)
    {
        if (!String.valueOf (got).equals (String.valueOf (want)))
        {
            System.err.println ("RateList: " + what + ": ожидалось [" + want
                + "], получено [" + got + "]");
            System.exit (1);
        }
    }

    private static void ok (String what, boolean cond)
    {
        if (!cond)
        {
            System.err.println ("RateList: " + what);
            System.exit (1);
        }
    }

    private static String list (List<String> l)
    {
        return String.join (",", l);
    }

    private static String readFile (String path)
    {
        StringBuilder sb = new StringBuilder ();
        try
        {
            BufferedReader r = new BufferedReader (new FileReader (path));
            String line;
            while ((line = r.readLine ()) != null)
                sb.append (line).append ('\n');
            r.close ();
        }
        catch (Exception e)
        {
            System.err.println ("RateList: не читается " + path + ": " + e);
            System.exit (1);
        }

        return sb.toString ();
    }

    /* Снимок с живого стенда (Android 9, UGOOS-AM6/X88 King, ТВ LG):
     * 480p60, 576p50, 720p60, 1080i60, 1080p60*, 720p50, 1080i50, 1080p30,
     * 1080p50, 1080p25, 1080p24. Ожидаемые частоты — только целые, как их и
     * видит демон (modes.c: display_mode_add оставляет неразрывные варианты). */
    private static final String DISP_CAP_LIVE =
        "480p60hz\n576p50hz\n720p60hz\n1080i60hz\n1080p60hz*\n720p50hz\n1080i50hz\n"
        + "1080p30hz\n1080p50hz\n1080p25hz\n1080p24hz\n";

    public static void main (String [] args)
    {
        /* Канонизация: 23.98 (как в дампе SurfaceFlinger) — это 23.976, а не 24;
         * 24.00 — именно 24. Допуск тот же, что у демона (±1 в 1/256 Гц). */
        eq ("23.98 -> 23.976", RateList.canonical (23.98), "23.976");
        eq ("23.976 -> 23.976", RateList.canonical (23.976), "23.976");
        eq ("24.00 -> 24", RateList.canonical (24.00), "24");
        eq ("59.94 -> 59.94", RateList.canonical (59.94), "59.94");
        eq ("60.00 -> 60", RateList.canonical (60.00), "60");
        eq ("29.97 -> 29.97", RateList.canonical (29.97), "29.97");
        eq ("50.00 -> 50", RateList.canonical (50.00), "50");

        /* Чужая частота канонического имени не получает: подмена «47.95 → 50»
         * дала бы настройку, которая выглядит рабочей, но режим не отсекает. */
        eq ("47.95 -> нет", RateList.canonical (47.95), null);
        eq ("120 -> нет", RateList.canonical (120.0), null);
        eq ("токен 47.95", RateList.token (47.95), "47.95");
        eq ("токен 120", RateList.token (120.0), "120");
        eq ("токен 12.5", RateList.token (12.5), "12.5");

        /* Список режимов ТВ (живой снимок): частоты режимов и их дробные
         * варианты — имя режима одно на оба («1080p24hz» — это и 24.000, и
         * 23.976, дробность задаёт frac_rate_policy), а записи настройки разные,
         * и запрет по одной не отсекает другую. */
        eq ("список ТВ", list (RateList.fromSources (DISP_CAP_LIVE)),
            "23.976,24,25,29.97,30,50,59.94,60");

        /* Дамп SurfaceFlinger (фикстура демона): у 1080p есть И 23.98, И 24.00 —
         * это разные записи настройки, обе нужны. */
        String sf = readFile ("tests/fixtures/sf_modes.txt");
        eq ("дамп SF", list (RateList.fromSources (sf)), "23.976,24,25,29.97,30,50,59.94,60");

        /* Android 10/11: таблицы режимов в дампе нет, есть частота активного режима. */
        eq ("дамп ATV11", list (RateList.fromSources (readFile ("tests/fixtures/sf_modes_android11.txt"))),
            "59.94");

        /* Значение настройки: порядок приводится к возрастанию, дубли убираются,
         * мусор (вне 1…1000) отбрасывается — как blacklist_rates_load. */
        eq ("значение", list (RateList.parse ("25 23.976 25")), "23.976,25");
        eq ("23.98 в файле", list (RateList.parse ("23.98")), "23.976");
        eq ("мусор", list (RateList.parse ("abc 0.5 2000 -5")), "");
        /* sscanf("%f") у демона: «30d» → 30 и «6e1» → 60 — настройка не теряется. */
        eq ("префикс", list (RateList.parse ("30d 6e1")), "30,60");

        eq ("сборка", RateList.format (RateList.parse ("25 30")), "25 30");
        eq ("сборка пустая", RateList.format (RateList.parse ("")), "");

        /* Список для диалога: частоты ТВ + уже выбранные (в том числе чужие,
         * которых на этом ТВ нет — их нельзя потерять при сохранении). */
        eq ("диалог", list (RateList.displayList (RateList.fromSources (DISP_CAP_LIVE), "30 45")),
            "23.976,24,25,29.97,30,45,50,59.94,60");

        /* Демон хранит десять записей (g_mode_blacklist_rates [10]): лишние
         * он выбрасывает молча, поэтому хвост отрезает GUI. */
        List<String> many = RateList.parse ("1 2 3 4 5 6 7 8 9 10 11 12");
        eq ("предел записей", RateList.trim (many).size (), RateList.MAX_ENTRIES);
        eq ("хвост отрезан", list (RateList.trim (many)).endsWith ("10") &&
            !list (RateList.trim (many)).contains ("11"), true);

        /* Канонические имена совпадают с таблицей демона (fps_class.c k_rates) —
         * иначе строка настроек покажет частоту, которой демон не знает. */
        eq ("канонические имена", String.join (",", Arrays.asList (RateList.CANON)),
            "23.976,24,25,29.97,30,50,59.94,60");
        for (String name : RateList.CANON)
            ok ("канонизация «" + name + "»", RateList.canonical (RateList.value (name)).equals (name));

        System.out.println ("RateList: OK");
    }
}
