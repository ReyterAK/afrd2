/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

/**
 * Правило переноса настроек из профиля в настройки приложения (ini_options).
 *
 * Сеем только непустое значение: пустое — это не умолчание, а отсутствие
 * настройки. Посев пустоты превратил бы закомментированную строку профиля в
 * активную пустую и затёр ручную правку ключа при следующем сохранении
 * настроек. Класс без Android-зависимостей — под host-тест tests/SeedTest.java.
 */
public class Seed
{
    /** Значение для посева или null, если сеять нечего. */
    public static String value (String profileValue)
    {
        if (profileValue == null)
            return null;

        String v = profileValue.trim ();
        return v.isEmpty () ? null : v;
    }

    /** Сеять ли настройку: её нет в настройках приложения, а в профиле есть значение. */
    public static boolean shouldSeed (String profileValue, boolean alreadyInPrefs)
    {
        return !alreadyInPrefs && (value (profileValue) != null);
    }

    /**
     * Убрать ли из настроек приложения значение, оставшееся от версии, где ключ
     * не переносился в конфиг (его не было в Config.itemType): там лежало
     * умолчание XML, которое ни на что не влияло, а демон читал профиль. Если
     * такое умолчание оставить, оно станет действующим и молча подменит
     * проверенную настройку профиля (12000 вместо 14000). Удаление записи
     * возвращает строку к умолчанию XML (совпадает с профилем) и не мешает
     * демону читать профильное значение.
     *
     * Своё значение пользователя (отличное от старого умолчания) не трогаем.
     */
    public static boolean shouldReset (String prefValue, String oldDefault)
    {
        return oldDefault.equals (prefValue);
    }
}
