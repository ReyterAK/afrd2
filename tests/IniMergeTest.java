package ru.cobra.zap.afrd;

/**
 * Host-тест слияния профиля при обновлении APK (IniMerge): активные ключи
 * профиля обновляются версией, а включённые пользователем руками ключи, которых
 * в профиле нет активными, должны пережить обновление.
 */
public class IniMergeTest
{
    private static int fails;

    private static void eq (String what, Object got, Object want)
    {
        if (!String.valueOf (got).equals (String.valueOf (want)))
        {
            System.err.println ("IniMerge: " + what + ": ожидалось [" + want
                + "], получено [" + got + "]");
            fails++;
        }
    }

    public static void main (String [] args)
    {
        /* Пользователь включил ключ, который в профиле закомментирован —
         * значение сохраняется, а строка раскомментируется на месте (копия в
         * конце дала бы «дубль ключа» в журнале демона). */
        String old = "switch.ceiling.method=codec\n";
        String fresh = "enable=1\n#switch.ceiling.method=auto\n";
        String merged = IniMerge.appendMissing (old, fresh);
        eq ("комментированный в профиле",
            merged, "enable=1\nswitch.ceiling.method=codec\n");

        /* Активный ключ профиля главнее: новое умолчание версии не затирается
         * пользовательским значением. */
        old = "switch.timeout=5000\n";
        fresh = "switch.timeout=3000\n";
        eq ("активный в профиле", IniMerge.appendMissing (old, fresh), fresh);

        /* Ключа в профиле нет вовсе — тоже сохраняется (burst.ms и подобные). */
        old = "burst.ms=1500\nenable=1\n";
        fresh = "enable=1\n";
        eq ("ключа нет в профиле",
            IniMerge.appendMissing (old, fresh), "enable=1\nburst.ms=1500\n");

        /* Ничего дописывать не нужно — файл не трогаем. */
        old = "enable=1\nlog.file=/data/local/afrd.log\n";
        fresh = "enable=1\nlog.file=/data/local/x.log\n";
        eq ("всё есть", IniMerge.appendMissing (old, fresh), fresh);

        /* Дубли ключей: в силу идёт последнее вхождение, как у демона. */
        old = "a=1\na=2\n";
        fresh = "b=1\n";
        eq ("дубль в старом", IniMerge.appendMissing (old, fresh), "b=1\na=2\n");

        /* Края: пустой старый файл, null, отсутствие перевода строки. */
        eq ("пустой старый", IniMerge.appendMissing ("", "a=1\n"), "a=1\n");
        eq ("null старый", IniMerge.appendMissing (null, "a=1\n"), "a=1\n");
        eq ("без \\n в конце",
            IniMerge.appendMissing ("x=1\n", "a=1"), "a=1\nx=1\n");

        /* Комментарии и мусор старого файла не переносятся. */
        old = "# комментарий\n\nневерная строка\nkey=value\n";
        eq ("только активные пары",
            IniMerge.appendMissing (old, "a=1\n"), "a=1\nkey=value\n");

        if (fails != 0)
        {
            System.err.println ("IniMerge: " + fails + " проверок не прошло");
            System.exit (1);
        }

        System.out.println ("IniMerge: OK");
    }
}
