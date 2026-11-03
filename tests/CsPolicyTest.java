package ru.cobra.zap.afrd;

/**
 * Host-тест политик цветового пространства (настройка «Формат HDMI»,
 * см. tests/run_tests.sh): выбранный пункт обязан давать значение cs.select,
 * которое демон разберёт (colorspace.c: правила «режим=формат»), а правило
 * защиты полосы 4K обязано оставаться во ВСЕХ пунктах — иначе на 3840x2160
 * при 50/60 Гц пользователь получит 4:4:4 (или RGB), который в полосу HDMI
 * не пролезает, и настройка превратится в источник «зелени» и чёрных экранов.
 */
import java.util.List;

public class CsPolicyTest
{
    private static void eq (String what, Object got, Object want)
    {
        if (!String.valueOf (got).equals (String.valueOf (want)))
        {
            System.err.println ("CsPolicy: " + what + ": ожидалось [" + want
                + "], получено [" + got + "]");
            System.exit (1);
        }
    }

    public static void main (String [] args)
    {
        /* Каждый пункт — своё значение, и оно распознаётся обратно. */
        java.util.List<String> fixed = java.util.Arrays.asList (CsPolicy.VALUES);
        for (int i = 0; i < CsPolicy.VALUES.length; i++)
            eq ("пункт " + i, CsPolicy.indexOf (fixed.get (i), fixed), i);

        /* Первый пункт — «Не изменять формат»: пустое значение. Только он, все
         * остальные пункты обязаны нести защиту полосы 4K (иначе на 3840x2160
         * при 50/60 Гц пользователь получит 4:4:4 или RGB, который в полосу
         * HDMI не пролезает). */
        eq ("не изменять", fixed.get (0), "");
        for (String v : CsPolicy.VALUES)
            eq ("защита 4K в [" + v + "]",
                v.isEmpty () || v.startsWith (CsPolicy.BASE_4K), true);

        /* Пункты различаются только выбором формата для остальных режимов. */
        eq ("авто", fixed.get (1), CsPolicy.BASE_4K);
        eq ("rgb", fixed.get (2), CsPolicy.BASE_4K + " .*=rgb");
        eq ("444", fixed.get (3), CsPolicy.BASE_4K + " .*=444");

        /* Профиль ATV14 (config/afrd2.ini) — это «Авто». */
        eq ("профиль 11-14", CsPolicy.indexOf ("2160p[4-9].*=420", fixed), 1);
        /* Профиль Android 6-9 (config/afrd-android8+.ini) явно ставит 444 всем
         * остальным режимам — настройка показывает это честно, а не «Авто». */
        eq ("профиль 6-9", CsPolicy.indexOf ("2160p[4-9].*=420 .*=444", fixed), 3);

        /* Лишние пробелы и порядок «как написал пользователь» не мешают:
         * значение могло остаться от текстового поля прежних версий. */
        eq ("пробелы", CsPolicy.indexOf ("  2160p[4-9].*=420    ", fixed), 1);
        eq ("переносы", CsPolicy.indexOf ("2160p[4-9].*=420\t.*=rgb", fixed), 2);

        /* Своё значение не подменяется: показываем его как есть. */
        eq ("свой текст", CsPolicy.indexOf ("1080p.*=rgb", fixed), -1);
        eq ("пусто", CsPolicy.indexOf ("", fixed), 0);
        eq ("null", CsPolicy.indexOf (null, fixed), -1);

        /* Список по возможностям ТВ (dc_cap): сворачиваем комбинации
         * «формат,глубина» в форматы, сохраняя порядок узла, и строим по
         * каждому пункт с защитой 4K. */
        List<String> caps = CsFormats.parse ("444,12bit\n444,10bit\n"
            + "422,12bit\nrgb,12bit\nrgb,8bit\n");
        eq ("dc_cap: форматы", caps.toString (), "[444, 422, rgb]");

        List<String> dyn = CsPolicy.valuesFor (caps);
        eq ("динамических пунктов", dyn.size (), 5);
        eq ("динамический 0", dyn.get (0), CsPolicy.NONE_VALUE);
        eq ("динамический авто", dyn.get (1), CsPolicy.BASE_4K);
        eq ("динамический 444", dyn.get (2), CsPolicy.BASE_4K + " .*=444");
        eq ("динамический 422", dyn.get (3), CsPolicy.BASE_4K + " .*=422");
        eq ("динамический rgb", dyn.get (4), CsPolicy.BASE_4K + " .*=rgb");
        for (String v : dyn)
            eq ("4K-защита в [" + v + "]",
                v.isEmpty () || v.startsWith (CsPolicy.BASE_4K), true);

        /* Индекс считается по тому же списку, который показан. */
        eq ("индекс 422", CsPolicy.indexOf (CsPolicy.BASE_4K + " .*=422", dyn), 3);
        eq ("индекс чужого", CsPolicy.indexOf ("1080p.*=rgb", dyn), -1);

        /* Без узла — прежний статический набор. */
        eq ("пустой dc_cap", CsPolicy.valuesFor (new java.util.ArrayList<String> ()),
            java.util.Arrays.asList (CsPolicy.VALUES));

        /* Незнакомый демону формат не показываем и не путаем с известным. */
        eq ("незнакомый формат", CsFormats.parse ("yuv420,8bit\n444,8bit").toString (),
            "[444]");

        /* Подписи и значения пунктов списка живут в arrays.xml, а логика — здесь:
         * разойдись они, строка настроек показывала бы один формат, а в конфиг
         * уходил другой. Списки сверяются как множества. */
        List<String> xml = xmlArray ("cs_policy_value");
        eq ("arrays.xml: пунктов", xml.size (), CsPolicy.VALUES.length);
        for (String v : CsPolicy.VALUES)
            eq ("arrays.xml содержит [" + v + "]", xml.contains (v), true);

        System.out.println ("CsPolicy: OK");
    }

    /** Значения <string-array name="..."> из arrays.xml (как есть, без пробелов по краям). */
    private static List<String> xmlArray (String name)
    {
        List<String> out = new java.util.ArrayList<> ();
        try
        {
            java.io.BufferedReader r = new java.io.BufferedReader (new java.io.FileReader (
                "android/app/src/main/res/values/arrays.xml"));
            String line;
            boolean in = false;
            while ((line = r.readLine ()) != null)
            {
                if (line.contains ("<string-array name=\"" + name + "\""))
                {
                    in = true;
                    continue;
                }
                if (!in)
                    continue;
                if (line.contains ("</string-array>"))
                    break;

                int a = line.indexOf ("<item>"), b = line.indexOf ("</item>");
                if ((a >= 0) && (b > a))
                    out.add (line.substring (a + "<item>".length (), b).trim ());
            }
            r.close ();
        }
        catch (Exception e)
        {
            System.err.println ("CsPolicy: не читается arrays.xml: " + e);
            System.exit (1);
        }

        return out;
    }
}
