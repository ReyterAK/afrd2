/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

/**
 * Разбор значения из ini так же, как его видит демон (cfg_parse.c:74-82):
 * всё после «#» — комментарий, а нечисловое значение целого ключа не должно
 * ронять приложение.
 *
 * Почему отдельным классом: конфиг правят руками (в README так и написано), и
 * `switch.timeout=3000 # мс` — законная строка для демона. Раньше
 * `Integer.parseInt` на ней бросал NumberFormatException из
 * MainActivity.onCreate, приложение не открывалось, а кнопка сброса настроек
 * живёт в том же экране (живой прогон 03.11). Класс без Android-зависимостей —
 * под host-тест tests/IniValueTest.java.
 */
public class IniValue
{
    /* Типы ключей. Живут здесь, а не в Config: Config тянет Android
     * (ArrayMap, SharedPreferences), и с ним этот разбор нельзя проверить
     * host-тестом. Config.TYPE_* — те же значения (алиасы). */
    public static final int TYPE_MASK = 0x0000000F;
    public static final int TYPE_BOOL = 0x00000001;
    public static final int TYPE_INT  = 0x00000002;
    public static final int TYPE_STR  = 0x00000003;

    /** Значение без комментария и без пробелов по краям — ровно то, что
     * прочитает демон. */
    public static String strip (String val)
    {
        if (val == null)
            return null;

        int hash = val.indexOf ('#');
        if (hash >= 0)
            val = val.substring (0, hash);

        return val.trim ();
    }

    /** Разобрать по типу ключа.
     *
     * BOOL — как раньше (всё, кроме 0/false/no, — истина);
     * INT — число, а нечисловое значение возвращается строкой: так оно переживёт
     * сохранение файла и не подменится нулём молча (демон прочитает его через
     * atoi, но это его дело, а не повод не открывать настройки);
     * STR — строка без комментария. */
    public static Object parse (int type, String val)
    {
        String v = strip (val);

        switch (type & TYPE_MASK)
        {
            case TYPE_BOOL:
                return parseBoolean (v);

            case TYPE_INT:
                try
                {
                    return Integer.parseInt (v);
                }
                catch (NumberFormatException e)
                {
                    return v;
                }

            default:
                return v;
        }
    }

    public static Boolean parseBoolean (String val)
    {
        if (val == null)
            return false;

        val = val.trim ().toLowerCase ();
        return !("0".equals (val) || "false".equals (val) || "no".equals (val));
    }
}
