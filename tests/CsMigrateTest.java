package ru.cobra.zap.afrd;

/**
 * Host-тест разового переноса настройки «Формат HDMI» (§237).
 *
 * В 1.2.0 и раньше пункт «Авто» записывал в cs.select ровно правило
 * {@link CsPolicy#BASE_4K}. Пункт удалён, умолчание — «Не изменять формат»
 * (пустое значение). У уже установленного приложения в настройках остаётся
 * старое «Авто»; без переноса демон продолжал бы писать формат, хотя строка
 * настроек уже показывала бы «свои правила».
 *
 * Правило проверки: переносится ровно старое «Авто» (пробелы и табы не важны),
 * любое другое значение — выбор пользователя, его не трогаем. Перенос
 * одноразовый (флаг в requisites), это проверяет MainActivity, здесь — правило.
 */
public class CsMigrateTest
{
    private static int fails;

    private static void eq (String what, Object got, Object want)
    {
        if (!String.valueOf (got).equals (String.valueOf (want)))
        {
            System.err.println ("CsMigrate: " + what + ": ожидалось [" + want
                + "], получено [" + got + "]");
            fails++;
        }
    }

    public static void main (String [] args)
    {
        /* Старое «Авто» — ровно правило защиты 4K. */
        eq ("точное авто", CsMigrate.noneFor (CsPolicy.BASE_4K), "");
        eq ("auto isAuto", CsMigrate.isAuto (CsPolicy.BASE_4K), true);
        eq ("пробелы по краям", CsMigrate.noneFor ("  " + CsPolicy.BASE_4K + "\t "), "");
        eq ("двойные пробелы",
            CsMigrate.noneFor ("2160p[4-9].*=420\t\t"), "");

        /* Чужие значения — выбор пользователя, не трогаем. */
        String [][] foreign =
        {
            { "YUV 4:4:4", CsPolicy.BASE_4K + " .*=444" },
            { "RGB", CsPolicy.BASE_4K + " .*=rgb" },
            { "4:2:2", CsPolicy.BASE_4K + " .*=422" },
            { "своё правило", "1080p.*=rgb" },
            { "пусто", "" },
            { "только пробелы", "   " },
            { "null", null },
        };

        for (String [] f : foreign)
        {
            eq (f [0] + ": миграции нет", CsMigrate.noneFor (f [1]), null);
            eq (f [0] + ": не авто", CsMigrate.isAuto (f [1]), false);
        }

        /* Регистр правил для демона значим: «2160P…» — не наше правило. */
        eq ("регистр", CsMigrate.isAuto ("2160P[4-9].*=420"), false);

        if (fails != 0)
        {
            System.err.println ("CsMigrate: " + fails + " проверок не прошло");
            System.exit (1);
        }

        System.out.println ("CsMigrate: OK");
    }
}
