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

    private static String roundPct (String value)
    {
        return PkgValues.format (PkgValues.parsePercent (value));
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

        /* Числа демон разбирает через sscanf("%f"): берётся самый длинный
         * числовой префикс токена, хвост игнорируется (проверено повтором цикла
         * разбора демона на хосте: «50d» → 50, «50,» → 50, «0x1p3» → 8).
         * Такие записи обязаны сохраняться, а не исчезать: иначе порог,
         * который демон применяет, silently пропадает при первом же сохранении
         * настроек. Форма приводится к канонической — строка настроек показывает
         * ровно то число, которое использует демон. */
        eq ("префикс", round ("a=50d b=50, c=50Hz e=6e1"), "a=50 b=50 c=50 e=60");
        eq ("hex-float", round ("a=0x1p3"), "a=8");

        /* Нечисловое и то, что фильтр демона отбрасывает: NaN/бесконечность
         * дают ложное сравнение с диапазоном, 1e4 выходит за 1000, 0.9 меньше 1 */
        eq ("мусор", round ("a=abc b=NaN c=Infinity d=1e4 f=0.9 g=12"), "g=12");

        /* диапазон 1…1000: границы включаются, выход за них отбрасывается */
        eq ("диапазон", round ("a=0 b=1 c=1000 d=1001"), "b=1 c=1000");

        /* разделители — любые пробельные (strspn(spaces) в демоне) */
        eq ("разделители", round ("  a=50\t b=60\n c=70  "), "a=50 b=60 c=70");

        /* дробные принимаются демоном как есть */
        eq ("дробное", round ("a=23.976 b=59.94"), "a=23.976 b=59.94");

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

        /* слоты демона занимают только записи с числом: запись без числа он
         * игнорирует (afrd.c: if (eq && cur[0])), поэтому лимит 8 GUI обязан
         * считать по заполненным, иначе откажет при свободном слоте */
        if (PkgValues.filledCount (PkgValues.parse ("a b=1 c=2 d", MIN, MAX)) != 2)
        {
            System.err.println ("PkgValues: filledCount должен считать только записи с числом");
            System.exit (1);
        }
        if (PkgValues.filledCount (new LinkedHashMap<String, String> ()) != 0)
        {
            System.err.println ("PkgValues: filledCount пустого значения должен быть 0");
            System.exit (1);
        }

        /* Обрезка до предела демона: порядок сохраняется, лишние отбрасываются,
         * записи без числа (слотов не занимают) остаются. */
        eq ("обрезка", PkgValues.format (PkgValues.trim (
            PkgValues.parse ("a=1 b=2 c=3 d=4 e=5 f=6 g=7 h=8 i=9 j=10", MIN, MAX),
            PkgValues.MAX_ENTRIES)), "a=1 b=2 c=3 d=4 e=5 f=6 g=7 h=8");
        eq ("обрезка с пустой", PkgValues.format (PkgValues.trim (
            PkgValues.parse ("z a=1 b=2 c=3", MIN, MAX), 2)), "z a=1 b=2");
        eq ("обрезка не нужна", PkgValues.format (PkgValues.trim (
            PkgValues.parse ("a=1 b=2", MIN, MAX), PkgValues.MAX_ENTRIES)), "a=1 b=2");
        eq ("обрезка в ноль", PkgValues.format (PkgValues.trim (
            PkgValues.parse ("a=1 z", MIN, MAX), 0)), "z");

        /* Предел длины имени пакета: Java-константа обязана совпадать с
         * PKG_NAME_LEN в pkgdet.h — иначе GUI пропустит имя, которое демон
         * обрежет (или отбросит), и порог для такого плеера молча не сработает. */
        if (PkgValues.MAX_PKG_LEN != 255)
        {
            System.err.println ("PkgValues: MAX_PKG_LEN ожидалось 255, получено " + PkgValues.MAX_PKG_LEN);
            System.exit (1);
        }

        try
        {
            String hdr = new String (java.nio.file.Files.readAllBytes (
                java.nio.file.Paths.get ("pkgdet.h")), java.nio.charset.StandardCharsets.UTF_8);
            if (!hdr.contains ("PKG_NAME_LEN\t256"))
            {
                System.err.println ("PkgValues: в pkgdet.h нет «PKG_NAME_LEN 256» — сверь MAX_PKG_LEN");
                System.exit (1);
            }
        }
        catch (java.io.IOException exc)
        {
            System.err.println ("PkgValues: pkgdet.h не читается: " + exc);
            System.exit (1);
        }

        /* Проценты (fps.correct.list): демон принимает строго |pct| < 50 и не
         * принимает 0 — правило одно и для разбора значения, и для диалога, иначе
         * запись, которую демон применяет, GUI считал бы недопустимой и терял. */
        eq ("проценты", roundPct ("a=-2.5 b=5 c=0.1"), "a=-2.5 b=5 c=0.1");

        /* ноль демон отбрасывает (fps_correct_load: pct != 0.0): запись не должна
         * ни показываться, ни занимать слот лимита */
        eq ("ноль", roundPct ("a=0 b=5"), "b=5");
        if (PkgValues.filledCount (PkgValues.parsePercent ("a=0 b=5")) != 1)
        {
            System.err.println ("PkgValues: запись с нулём не должна занимать слот");
            System.exit (1);
        }

        /* границы: 49.95 демон применяет, 50 и больше — нет */
        eq ("границы процентов", roundPct ("a=49.95 b=-49.95 c=50 d=-50 e=0.1"), "a=49.95 b=-49.95 e=0.1");

        /* повтор: применяется первая запись, у которой число принято, — поэтому
         * запись без числа вытесняется более поздней с числом (как у демона) */
        eq ("повтор без числа", roundPct ("a= a=5"), "a=5");
        eq ("повтор с числами", roundPct ("a=5 a=10"), "a=5");
        eq ("запись без числа сохраняется", roundPct ("a="), "a");

        /* диалог: ноль и пустое не принимаются, значения около нуля — принимаются */
        if (PkgValues.percentInRange ("0") || PkgValues.percentInRange ("") ||
            PkgValues.percentInRange ("50") || PkgValues.percentInRange ("-50"))
        {
            System.err.println ("PkgValues: 0, пустое и |pct| >= 50 демон не принимает");
            System.exit (1);
        }
        if (!PkgValues.percentInRange ("0.1") || !PkgValues.percentInRange ("-0.1") ||
            !PkgValues.percentInRange ("49.95"))
        {
            System.err.println ("PkgValues: допустимые проценты должны приниматься");
            System.exit (1);
        }

        /* в герцах ноль — обычное значение (другой ключ, другой диапазон) */
        if (!PkgValues.inRange ("1", PkgValues.MIN_HZ, PkgValues.MAX_HZ))
        {
            System.err.println ("PkgValues: герцы должны принимать значение в диапазоне");
            System.exit (1);
        }

        System.out.println ("PkgValues: OK");
    }
}
