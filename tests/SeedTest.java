package ru.cobra.zap.afrd;

/** Правило посева настроек из профиля (см. MainActivity.seedOptions). */
public class SeedTest
{
    private static void expect (String what, boolean got, boolean want)
    {
        if (got != want)
        {
            System.err.println ("Seed: " + what + ": ожидалось " + want + ", получено " + got);
            System.exit (1);
        }
    }

    private static void check (String what, String got, String want)
    {
        if (!want.equals (got))
        {
            System.err.println ("Seed: " + what + ": ожидалось \"" + want + "\", получено \"" + got + "\"");
            System.exit (1);
        }
    }

    public static void main (String [] args)
    {
        /* пустое значение профиля не сеем: иначе закомментированная строка
         * профиля становится активной пустой и затирает ручную правку */
        expect ("пусто", Seed.shouldSeed ("", false), false);
        expect ("null", Seed.shouldSeed (null, false), false);
        expect ("пробелы", Seed.shouldSeed ("  ", false), false);

        /* непустое сеем, чтобы первая же правка настроек не потеряла профиль */
        expect ("значение", Seed.shouldSeed ("com.foo=50", false), true);

        /* уже имеющаяся настройка не перетирается */
        expect ("уже есть", Seed.shouldSeed ("com.foo=50", true), false);
        expect ("уже есть, пусто", Seed.shouldSeed ("", true), false);

        if (!"com.foo".equals (Seed.value (" com.foo ")))
        {
            System.err.println ("Seed: value должно обрезать пробелы");
            System.exit (1);
        }

        /* Перенос при обновлении: у ключа, который раньше не попадал в конфиг,
         * в настройках лежит старое умолчание XML — такую запись убираем, иначе
         * она молча подменила бы проверенную настройку профиля (12000 вместо 14000). */
        expect ("старое умолчание убираем", Seed.shouldReset ("12000", "12000"), true);
        expect ("правку пользователя не трогаем", Seed.shouldReset ("9000", "12000"), false);
        expect ("пусто не трогаем", Seed.shouldReset ("", "12000"), false);

        /* Обновление умолчания switch.skip.pkg на Android 12-14: у уже
         * установленного приложения в настройках лежит старое умолчание
         * (штатный YouTube). Убираем только его, свои записи пользователя
         * сохраняем; пустой список — законное значение. */
        String yt = "com.google.android.youtube.tv";
        check ("убрать единственный", Seed.withoutPkg (yt, yt), "");
        check ("убрать из списка", Seed.withoutPkg ("com.foo " + yt, yt), "com.foo");
        check ("убрать в середине", Seed.withoutPkg ("a.test " + yt + " b.test", yt), "a.test b.test");
        check ("чужой пакет не трогаем", Seed.withoutPkg ("com.foo", yt), "com.foo");
        check ("пробелы", Seed.withoutPkg ("  " + yt + "  ", yt), "");
        check ("пусто", Seed.withoutPkg ("", yt), "");

        System.out.println ("Seed: OK");
    }
}
