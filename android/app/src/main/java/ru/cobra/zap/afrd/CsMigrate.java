/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

/**
 * Миграция настройки «Формат HDMI» после удаления пункта «Авто» (§237).
 *
 * Пункт «Авто» записывал в cs.select ровно {@link CsPolicy#BASE_4K} —
 * {@code 2160p[4-9].*=420}. Демон с этим правилом не только ставил 420 на 4K,
 * но и при несовпадении любого правила записывал стартовый профиль на всех
 * остальных режимах (colorspace.c: {@code cur_cs = def_cs}), то есть «Авто»
 * не значило «пусть решает система». Пункт удалён, умолчание — пустое
 * значение («Не изменять формат»): демон не пишет формат ни разу.
 *
 * У уже установленного приложения в настройках лежит старое «Авто», а
 * умолчания XML не пересеиваются. Без переноса демон продолжал бы навязывать
 * 444,12bit. Правило: переносим ровно старое «Авто» (пробелы не важны), любое
 * другое значение — выбор пользователя. Класс без Android-зависимостей: его
 * проверяет {@code tests/CsMigrateTest.java}.
 */
public class CsMigrate
{
    /** Значение «Не изменять формат», в которое переносится старое «Авто». */
    public static final String NONE_VALUE = CsPolicy.NONE_VALUE;

    /** Старое значение «Авто» с нормализованными пробелами. */
    private static final String AUTO = normalize (CsPolicy.BASE_4K);

    /** Старое ли значение: ровно правило 4K, пробелы и табы не важны.
     * Регистр значим — правила демона сравниваются как есть. */
    public static boolean isAuto (String cs)
    {
        return AUTO.equals (normalize (cs));
    }

    /** Новое значение для сохранённой настройки или {@code null}, если миграция
     * не нужна (значение чужое/пустое/отсутствует). */
    public static String noneFor (String cs)
    {
        return isAuto (cs) ? NONE_VALUE : null;
    }

    /** Пробелы по краям и повторные пробелы/табы внутри — как у
     * {@code CsPolicy.indexOf}: значение могло быть записано руками. */
    private static String normalize (String value)
    {
        if (value == null)
            return "";

        return value.trim ().replaceAll ("\\s+", " ");
    }
}
