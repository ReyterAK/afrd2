/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

/**
 * Миграция настройки поправки AV1 после того, как «авто» убрали (§185).
 *
 * До 05.11 в настройках жило значение {@code auto}: поправка включалась сама на
 * ATV14 и действовала на встроенный список Cobalt-пакетов. Затем авто убрали
 * (решение владельца, §178): теперь нужен процент плюс список плееров, а
 * {@code auto} демон считает выключенным и пишет в журнал подсказку.
 *
 * Проблема обновления: у всех, кто ставил ранние сборки, в настройках остаётся
 * {@code auto} (умолчания XML не пересеиваются), приложение переносит его в
 * afrd.ini, и поправка AV1 молча перестаёт работать — 24p в Cobalt снова
 * читается как 25 и режим уходит на 50 Гц (живой случай §176).
 *
 * Правило: {@code auto} → процент {@link #PERCENT}; пустой список плееров при
 * этом заполняется прежним встроенным (тот же, что был в демоне до 355081a).
 * Любое другое значение (off, число, свой список) не трогаем — это выбор
 * пользователя. Класс без Android-зависимостей: его проверяет
 * {@code tests/Av1MigrateTest.java}.
 */
public class Av1Migrate
{
    /** Процент, который демон сам подсказывает в журнале для тракта Cobalt. */
    public static final String PERCENT = "5.7";

    /** Прежний встроенный список из afrd.c (удалён в 355081a). */
    public static final String LEGACY_PKGS =
            "io.gh.reisxd.tizentube.cobalt com.google.android.youtube.tv com.google.android.youtube";

    /** Устаревшее ли значение: ровно {@code auto}, регистр и пробелы не важны. */
    public static boolean isLegacy (String av1)
    {
        return (av1 != null) && "auto".equalsIgnoreCase (av1.trim ());
    }

    /** Новое значение процента или {@code null}, если миграция не нужна. */
    public static String percentFor (String av1)
    {
        return isLegacy (av1) ? PERCENT : null;
    }

    /**
     * Список плееров после миграции: прежний встроенный, если своего не было,
     * иначе {@code null} — правку пользователя не переписываем.
     */
    public static String pkgsFor (String av1, String pkgs)
    {
        if (!isLegacy (av1))
            return null;

        return ((pkgs == null) || pkgs.trim ().isEmpty ()) ? LEGACY_PKGS : null;
    }
}
