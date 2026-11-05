package ru.cobra.zap.afrd;

import java.util.Arrays;
import java.util.List;

/** Подписи частот и правило пробы (см. FracProbe). */
public class FracProbeTest
{
    private static void eq (String what, Object got, Object want)
    {
        boolean same = (want == null) ? (got == null) : want.equals (got);
        if (!same)
        {
            System.err.println ("FracProbe: " + what + ": ожидалось [" + want + "], получено [" + got + "]");
            System.exit (1);
        }
    }

    public static void main (String [] args)
    {
        /* Пары «номинал — дробный представитель»: у 25/50 пары нет. */
        eq ("24 пара", FracProbe.twinName ("24"), "23.976");
        eq ("30 пара", FracProbe.twinName ("30"), "29.97");
        eq ("60 пара", FracProbe.twinName ("60"), "59.94");
        eq ("25 без пары", FracProbe.twinName ("25"), null);
        eq ("hasTwin 50", FracProbe.hasTwin ("50"), false);
        eq ("hasTwin 24", FracProbe.hasTwin ("24"), true);

        /* Правило пробы: целая доступна, только если узел остался 0 И тайминг
         * пришёл целый. Узел 0 при дробном тайминге — HAL собрал режим по-своему. */
        eq ("держит 0", FracProbe.verdict (true, true), FracProbe.HELD);
        eq ("узел вернулся", FracProbe.verdict (false, false), FracProbe.FRACT);
        eq ("узел 0, тайминг дробный", FracProbe.verdict (true, false), FracProbe.FRACT);
        eq ("узел 1, тайминг целый", FracProbe.verdict (false, true), FracProbe.FRACT);

        /* Подписи: до пробы — номинал (не выдумываем), после — фактическая
         * частота; если доступны обе, показываем обе. */
        eq ("не измерено 24", FracProbe.label ("24", FracProbe.UNKNOWN), "24");
        eq ("дробные 24", FracProbe.label ("24", FracProbe.FRACT), "23.976");
        eq ("обе 24", FracProbe.label ("24", FracProbe.HELD), "23.976 / 24");
        eq ("дробные 60", FracProbe.label ("60", FracProbe.FRACT), "59.94");
        eq ("без пары 25", FracProbe.label ("25", FracProbe.FRACT), "25");
        eq ("чужое значение", FracProbe.label ("45", FracProbe.HELD), "45");

        /* Режим для пробы: класс 24/30/60, не текущий (повтор имени вендорный
         * HAL считает «режим не сменился»), предпочтительно 1080p. */
        List<String> names = Arrays.asList ("1080p60hz", "1080p50hz", "1080p24hz", "720p30hz");
        eq ("цель при текущем 24", FracProbe.targetMode (names, "1080p24hz"), "1080p60hz");
        eq ("цель при текущем 60", FracProbe.targetMode (names, "1080p60hz"), "1080p24hz");
        eq ("нет цели", FracProbe.targetMode (Arrays.asList ("1080p25hz", "1080p50hz"), "1080p25hz"),
            null);
        eq ("null-список", FracProbe.targetMode (null, "1080p60hz"), null);

        /* Разбор вывода пробы — на реальных строках с .42 (05.11). */
        List<String> out = Arrays.asList ("M=1080p60hz", "P=1", "N=24");
        eq ("tag M", FracProbe.tagValue (out, "M="), "1080p60hz");
        eq ("tag P", FracProbe.tagValue (out, "P="), "1");
        eq ("тег не найден", FracProbe.tagValue (out, "X="), "");
        eq ("P2 не путается с P", FracProbe.tagValue (Arrays.asList ("P2=0"), "P="), "");

        eq ("frac 0", FracProbe.vinfoInteger ("    frac:                  0", "", ""), true);
        eq ("frac 1", FracProbe.vinfoInteger ("    frac:                  1", "", ""), false);
        /* старые ядра (`.48`, ATV9) поля frac не имеют — решает тайминг */
        eq ("24/1 целая", FracProbe.vinfoInteger ("", "  sync_duration_num: 24",
                                                  "  sync_duration_den: 1"), true);
        eq ("2997/125 дробная", FracProbe.vinfoInteger ("", "  sync_duration_num: 2997",
                                                        "  sync_duration_den: 125"), false);
        eq ("50/1 целая", FracProbe.vinfoInteger ("", "  sync_duration_num: 50",
                                                  "  sync_duration_den: 1"), true);
        eq ("пусто", FracProbe.vinfoInteger ("", "", ""), false);

        eq ("имена режимов", String.join (",", FracProbe.modeNamesFromLines (Arrays.asList (
                "480p60hz", "1080p60hz*", "640x480p60hz", "", "1080p24hz"))),
            "480p60hz,1080p60hz,1080p24hz");

        System.out.println ("FracProbe: OK");
    }
}
