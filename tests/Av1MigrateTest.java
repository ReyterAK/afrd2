package ru.cobra.zap.afrd;

/** Правило миграции устаревшей поправки AV1 (см. Av1Migrate). */
public class Av1MigrateTest
{
    private static void expect (String what, boolean got, boolean want)
    {
        if (got != want)
        {
            System.err.println ("Av1Migrate: " + what + ": ожидалось " + want + ", получено " + got);
            System.exit (1);
        }
    }

    private static void check (String what, String got, String want)
    {
        boolean same = (want == null) ? (got == null) : want.equals (got);
        if (!same)
        {
            System.err.println ("Av1Migrate: " + what + ": ожидалось \"" + want + "\", получено \"" + got + "\"");
            System.exit (1);
        }
    }

    public static void main (String [] args)
    {
        /* Устаревшее значение — ровно auto; регистр и пробелы не важны, потому
         * что значение приходит из настроек, а не от пользователя вручную. */
        expect ("auto", Av1Migrate.isLegacy ("auto"), true);
        expect ("AUTO", Av1Migrate.isLegacy ("AUTO"), true);
        expect ("пробелы", Av1Migrate.isLegacy (" auto "), true);
        expect ("off", Av1Migrate.isLegacy ("off"), false);
        expect ("число", Av1Migrate.isLegacy ("5.7"), false);
        expect ("пусто", Av1Migrate.isLegacy (""), false);
        expect ("null", Av1Migrate.isLegacy (null), false);

        /* Выбор пользователя (выключено, свой процент) не переписываем. */
        check ("процент для auto", Av1Migrate.percentFor ("auto"), "5.7");
        check ("off не мигрирует", Av1Migrate.percentFor ("off"), null);
        check ("свой процент не мигрирует", Av1Migrate.percentFor ("3.5"), null);

        /* Пустой список заполняем прежним встроенным; заполненный не трогаем. */
        check ("пустой список", Av1Migrate.pkgsFor ("auto", ""), Av1Migrate.LEGACY_PKGS);
        check ("null-список", Av1Migrate.pkgsFor ("auto", null), Av1Migrate.LEGACY_PKGS);
        check ("пробелы вместо списка", Av1Migrate.pkgsFor ("auto", "   "), Av1Migrate.LEGACY_PKGS);
        check ("свой список", Av1Migrate.pkgsFor ("auto", "com.foo"), null);
        check ("не auto — список не трогаем", Av1Migrate.pkgsFor ("off", ""), null);

        /* Список должен совпадать с тем, что демон держал встроенным до 355081a:
         * иначе миграция включит поправку не тем плеерам. */
        String [] legacy = { "io.gh.reisxd.tizentube.cobalt",
                             "com.google.android.youtube.tv",
                             "com.google.android.youtube" };
        for (String pkg : legacy)
            expect ("в списке " + pkg, Av1Migrate.LEGACY_PKGS.contains (pkg), true);

        /* Разделитель — пробел, как пишет GUI (PkgValues); запятая в этом поле
         * означала бы список, который демон тоже разберёт, но GUI показал бы
         * его одной строкой. */
        expect ("без запятых", Av1Migrate.LEGACY_PKGS.contains (","), false);
    }
}
