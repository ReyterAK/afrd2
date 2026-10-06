package ru.cobra.zap.afrd;

import java.util.LinkedHashMap;

/**
 * Host-тест разбора и сборки значения «пакет=число» (см. tests/run_tests.sh).
 * Проверяет, что GUI видит значение ровно так же, как демон (afrd.c,
 * mode_min_hz_load): иначе сохранение из настроек молча потеряет или испортит
 * то, что демон на самом деле использует.
 */
public class PkgValuesTest
{
    private static final double MIN = PkgValues.MIN_HZ;
    private static final double MAX = PkgValues.MAX_HZ;

    private static void eq (String what, String got, String want)
    {
        if (!got.equals (want))
        {
            System.err.println ("PkgValues: " + what + ": ожидалось [" + want
                + "], получено [" + got + "]");
            System.exit (1);
        }
    }

    private static String round (String value)
    {
        return PkgValues.format (PkgValues.parse (value, MIN, MAX));
    }

    public static void main (String [] args)
    {
        /* обычный список: порядок записей сохраняется */
        eq ("порядок", round ("a.b=50 c.d=59.94"), "a.b=50 c.d=59.94");

        /* повтор пакета: первая запись (как mode_min_hz_for в демоне) */
        eq ("повтор", round ("a=50 a=60"), "a=50");

        /* запись без '=' сохраняется как есть: GUI не удаляет текст молча */
        eq ("без =", round ("a.b"), "a.b");
        eq ("пустое =", round ("a.b="), "a.b");

        /* мусор вместо числа и «числа», которые Double.parseDouble съел бы,
         * а sscanf("%f") в демоне — нет */
        eq ("мусор", round ("a=abc b=NaN c=50d d=0x1p3 e=Infinity f=12"), "f=12");

        /* диапазон 1…1000: границы включаются, выход за них отбрасывается */
        eq ("диапазон", round ("a=0 b=1 c=1000 d=1001"), "b=1 c=1000");

        /* разделители — любые пробельные (strspn(spaces) в демоне) */
        eq ("разделители", round ("  a=50\t b=60\n c=70  "), "a=50 b=60 c=70");

        /* дробные и экспоненциальная запись принимаются демоном */
        eq ("дробное", round ("a=23.976 b=6e1"), "a=23.976 b=6e1");

        /* пустое значение настройки — пустой список, а не «одна пустая запись» */
        eq ("пусто", round (""), "");
        eq ("null", round (null), "");
        eq ("сборка пустого", PkgValues.format (new LinkedHashMap<String, String> ()), "");

        /* без числа запись считается незаданной, но остаётся в значении */
        LinkedHashMap<String, String> v = PkgValues.parse ("a.b", MIN, MAX);
        if (!"".equals (v.get ("a.b")) || (v.size () != 1))
        {
            System.err.println ("PkgValues: запись без числа должна сохраняться с пустым значением");
            System.exit (1);
        }

        System.out.println ("PkgValues: OK");
    }
}
