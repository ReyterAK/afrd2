package ru.cobra.zap.afrd;

/**
 * Host-тест записи значений в ini (IniRewrite): на каждый ключ — одна активная
 * строка, подсказки остаются комментариями, порядок файла не рвётся.
 *
 * Живой случай §104: подсказка `#fps.declare.fast=0` «оживала» при сохранении
 * настроек, а `setIniKey` дописывал вторую активную строку — демон читал
 * последнюю, и переключатель оказывался инертным.
 */
public class IniRewriteTest
{
    private static int fails;

    private static void eq (String what, Object got, Object want)
    {
        if (!String.valueOf (got).equals (String.valueOf (want)))
        {
            System.err.println ("IniRewrite: " + what + ": ожидалось [" + want
                + "], получено [" + got + "]");
            fails++;
        }
    }

    private static java.util.Map<String, String> map (String... kv)
    {
        java.util.LinkedHashMap<String, String> m = new java.util.LinkedHashMap<> ();
        for (int i = 0; i + 1 < kv.length; i += 2)
            m.put (kv [i], kv [i + 1]);
        return m;
    }

    public static void main (String [] args)
    {
        /* Подсказка остаётся комментарием, значение пишется активной строкой
         * в конец (ключа активного в файле не было). */
        String text = "enable=1\n#fps.declare.fast=0\n";
        eq ("подсказка не оживает",
            IniRewrite.apply (text, map ("fps.declare.fast", "1")),
            "enable=1\n#fps.declare.fast=0\nfps.declare.fast=1\n");

        /* Активная строка переписывается на своём месте, порядок и
         * комментарии вокруг не трогаются. */
        text = "# шапка\nenable=1\nswitch.timeout=2000\n# хвост\n";
        eq ("значение на месте",
            IniRewrite.apply (text, map ("switch.timeout", "3000")),
            "# шапка\nenable=1\nswitch.timeout=3000\n# хвост\n");

        /* Дубли активных строк схлопываются в одну — последнее значение
         * (так читает демон). */
        text = "a=1\nb=2\na=3\n";
        eq ("дубли", IniRewrite.apply (text, null), "a=3\nb=2\n");

        /* Без изменений файл остаётся байт в байт. */
        text = "enable=1\n# комментарий\nlog.file=/data/local/afrd.log\n";
        eq ("без изменений", IniRewrite.apply (text, map ()), text);

        /* Значения с '=' внутри (правило формата) не режутся. */
        text = "cs.select=2160p[4-9].*=420\n";
        eq ("значение с =",
            IniRewrite.apply (text, map ("cs.select", "2160p[4-9].*=444")),
            "cs.select=2160p[4-9].*=444\n");

        /* Пустой файл: ключ дописывается без лишних переводов строки. */
        eq ("пустой файл", IniRewrite.apply ("", map ("enable", "1")), "enable=1\n");
        eq ("null текст", IniRewrite.apply (null, map ("enable", "1")), "enable=1\n");

        /* Нет перевода строки в конце — новый ключ начинается с новой строки. */
        eq ("без \\n в конце",
            IniRewrite.apply ("enable=1", map ("log.enable", "1")),
            "enable=1\nlog.enable=1\n");

        /* Значение null не пишется (ключ не задан — строку не трогаем). */
        eq ("null значение", IniRewrite.apply ("a=1\n", map ("b", null)), "a=1\n");

        /* Комментарий вида «# ключ=значение» в середине не мешает активной
         * строке ниже и не превращается в две активные. */
        text = "#switch.revert=1\nswitch.revert=0\n";
        eq ("комментарий рядом", IniRewrite.apply (text, map ()),
            "#switch.revert=1\nswitch.revert=0\n");

        if (fails != 0)
        {
            System.err.println ("IniRewrite: " + fails + " проверок не прошло");
            System.exit (1);
        }

        System.out.println ("IniRewrite: OK");
    }
}
