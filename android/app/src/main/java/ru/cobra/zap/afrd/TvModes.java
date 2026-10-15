/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;


import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * Список видеорежимов, которые сейчас доступны на боксе: узел EDID телевизора
 * (disp_cap) и таблица режимов SurfaceFlinger. Нужен настройкам, где
 * пользователь выбирает частоту («Список запрещённых частот»): частоты берём
 * ровно те, которые может выбрать демон, а не «стандартный набор» — иначе
 * настройка выглядела бы рабочей, но не отсекала ни одного режима.
 *
 * Источников два, потому что пути демона разные: на Android 6-11 решает
 * disp_cap (таблица режимов в узле), на 12-14 — таблица SurfaceFlinger, а узел
 * может отдавать и список с другим набором частот. Дамп SurfaceFlinger
 * доступен только под root, disp_cap читается без него, поэтому при отсутствии
 * root остаётся первый источник.
 *
 * Класс нужен только GUI: демон читает те же узлы сам (platform.c, afrd.c).
 */
public class TvModes
{
    private static final String DISP_CAP = "/sys/class/amhdmitx/amhdmitx0/disp_cap";

    /** Текст обоих источников (строки как есть, разбор — RateList). */
    public static String read ()
    {
        StringBuilder sb = new StringBuilder ();

        List<String> out = new ArrayList<> ();
        RootShell.run (new String [] {
            "cat " + DISP_CAP,
            /* Фильтр как у демона (platform.c): нужны строки таблицы режимов и
             * частота активного режима (на Android 10/11 таблицы нет). */
            "dumpsys SurfaceFlinger | grep -E 'refreshRate=|refresh-rate' | head -40",
        }, out);

        for (String line : out)
            sb.append (line).append ('\n');

        /* Без root (или если su не ответил) дамп не получится — узел EDID
         * читается обычным чтением файла. */
        if (sb.length () == 0)
            sb.append (readNode (DISP_CAP));

        return sb.toString ();
    }

    /** Содержимое sysfs-узла; пустая строка — не прочитался. */
    private static String readNode (String path)
    {
        StringBuilder sb = new StringBuilder ();
        try
        {
            BufferedReader r = new BufferedReader (new InputStreamReader (
                new FileInputStream (path), StandardCharsets.UTF_8));
            String line;
            while ((line = r.readLine ()) != null)
                sb.append (line).append ('\n');
            r.close ();
        }
        catch (Exception ignored)
        {
        }

        return sb.toString ();
    }
}
