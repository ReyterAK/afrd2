package ru.cobra.zap.afrd;

/**
 * Host-тест разбора значений из ini (IniValue): правила должны совпадать с
 * демоном (cfg_parse.c режет строку по «#»), а нечисловое значение целого ключа
 * не должно ронять приложение.
 *
 * Живой случай 03.11: `switch.timeout=3000 # мс` — законная строка для демона,
 * а GUI падал с NumberFormatException в MainActivity.onCreate, и кнопка сброса
 * настроек (в том же экране) была недоступна.
 */
public class IniValueTest
{
    private static int fails;

    private static void eq (String what, Object got, Object want)
    {
        if (!String.valueOf (got).equals (String.valueOf (want)))
        {
            System.err.println ("IniValue: " + what + ": ожидалось [" + want
                + "], получено [" + got + "]");
            fails++;
        }
    }

    public static void main (String [] args)
    {
        /* Комментарий отрезается так же, как в cfg_parse.c — и у строк, и у
         * чисел. Иначе значение с комментарием уехало бы в prefs и обратно в
         * ini вместе с текстом комментария. */
        eq ("число с комментарием", IniValue.parse (IniValue.TYPE_INT, "3000 # мс"), 3000);
        eq ("комментарий без пробела", IniValue.parse (IniValue.TYPE_INT, "250#мс"), 250);
        eq ("строка с комментарием", IniValue.parse (IniValue.TYPE_STR, "2160p[4-9].*=420 # правило"),
            "2160p[4-9].*=420");
        eq ("строка как есть", IniValue.parse (IniValue.TYPE_STR, "2160p[4-9].*=420"),
            "2160p[4-9].*=420");
        eq ("пробелы по краям", IniValue.parse (IniValue.TYPE_STR, "  abc  "), "abc");

        /* Нечисловое значение целого ключа не бросает исключение и не
         * подменяется нулём: возвращается строкой, демон прочитает её сам. */
        eq ("пустое INT", IniValue.parse (IniValue.TYPE_INT, ""), "");
        eq ("текст в INT", IniValue.parse (IniValue.TYPE_INT, "abc"), "abc");
        eq ("дробное в INT", IniValue.parse (IniValue.TYPE_INT, "1.5"), "1.5");
        eq ("отрицательное", IniValue.parse (IniValue.TYPE_INT, "-100"), -100);
        eq ("ноль", IniValue.parse (IniValue.TYPE_INT, "0"), 0);

        /* BOOL — прежнее правило демона и прежнее правило Config: ложь только
         * 0/false/no. */
        eq ("bool 1", IniValue.parse (IniValue.TYPE_BOOL, "1"), true);
        eq ("bool 0", IniValue.parse (IniValue.TYPE_BOOL, "0"), false);
        eq ("bool false", IniValue.parse (IniValue.TYPE_BOOL, "false"), false);
        eq ("bool no", IniValue.parse (IniValue.TYPE_BOOL, " no "), false);
        eq ("bool true", IniValue.parse (IniValue.TYPE_BOOL, "true"), true);
        eq ("bool пусто", IniValue.parse (IniValue.TYPE_BOOL, ""), true);
        eq ("bool с комментарием", IniValue.parse (IniValue.TYPE_BOOL, "0 # выкл"), false);

        /* strip сам по себе: null не должен превращаться в строку. */
        eq ("strip null", IniValue.strip (null), null);
        eq ("strip только комментарий", IniValue.strip ("# всё"), "");

        if (fails != 0)
        {
            System.err.println ("IniValue: " + fails + " проверок не прошло");
            System.exit (1);
        }

        System.out.println ("IniValue: OK");
    }
}
