/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

/**
 * Слияние профиля при обновлении APK: профиль из ресурсов заменяет файл
 * целиком, и всё, что пользователь включил руками, терялось молча — а README
 * ровно эти ключи и предлагает править руками
 * (`switch.ceiling.method`, `switch.revert`, `fps.feed.*`,
 * `fps.correct.av1.*`, `burst.ms`, `vendor.disp.persist`).
 *
 * Правило: активные ключи профиля главнее (их значения обновляет версия). Для
 * остальных активных ключей старого файла:
 *   * если в профиле тот же ключ стоит закомментированным — раскомментируем его
 *     на месте и подставляем значение пользователя (комментарий — подсказка, а
 *     не значение; копия в конце файла дала бы «дубль ключа» в журнале);
 *   * если ключа в профиле нет вовсе — дописываем в конец.
 *
 * Класс без Android-зависимостей — под host-тест tests/IniMergeTest.java.
 */
public class IniMerge
{
    /** Слить профиль newText со значениями из oldText. Возвращает newText,
     * если менять нечего. */
    public static String appendMissing (String oldText, String newText)
    {
        if ((oldText == null) || oldText.isEmpty () || (newText == null))
            return newText;

        java.util.LinkedHashMap<String, String> old = active (oldText);
        java.util.Set<String> taken = active (newText).keySet ();

        if (old.isEmpty ())
            return newText;

        StringBuilder tail = new StringBuilder ();
        boolean changed = false;
        String [] lines = newText.split ("\\n", -1);
        StringBuilder out = new StringBuilder ();

        for (int i = 0; i < lines.length; i++)
        {
            String raw = lines [i];
            boolean last = (i == lines.length - 1);
            String line = raw.trim ();
            String key = null;
            String value = null;

            if (line.startsWith ("#"))
            {
                int eq = line.indexOf ('=', 1);
                if (eq > 1)
                    key = line.substring (1, eq).trim ();
            }

            if ((key != null) && !taken.contains (key) && old.containsKey (key))
            {
                value = old.get (key);
                old.remove (key);
                changed = true;
                out.append (key).append ('=').append (value);
                if (!last)
                    out.append ('\n');
                continue;
            }

            out.append (raw);
            if (!last)
                out.append ('\n');
        }

        for (java.util.Map.Entry<String, String> e : old.entrySet ())
        {
            if (taken.contains (e.getKey ()))
                continue;

            tail.append (e.getKey ()).append ('=').append (e.getValue ()).append ('\n');
            changed = true;
        }

        if (!changed)
            return newText;

        String res = out.toString ();
        if ((tail.length () > 0) && !res.endsWith ("\n"))
            res += "\n";

        return res + tail;
    }

    /** Убрать из текста строки (активные и закомментированные) указанных
     * ключей. Нужно «Сбросу настроек»: для ключей, которыми управляет
     * приложение, должен победить профиль, а ручные правки остальных ключей
     * сохраняются (их в наборе нет). */
    public static String dropKeys (String text, java.util.Set<String> keys)
    {
        if ((text == null) || (keys == null) || keys.isEmpty ())
            return text;

        StringBuilder out = new StringBuilder ();
        boolean trailing = text.endsWith ("\n");
        String [] lines = text.isEmpty () ? new String [0] : text.split ("\n", -1);
        int n = lines.length;
        if (trailing && (n > 0))
            n--;

        boolean first = true;
        for (int i = 0; i < n; i++)
        {
            String raw = lines [i];
            String line = raw.trim ();
            String key = null;

            if (line.startsWith ("#"))
            {
                int eq = line.indexOf ('=', 1);
                if (eq > 1)
                    key = line.substring (1, eq).trim ();
            }
            else if (!line.isEmpty ())
            {
                int eq = line.indexOf ('=');
                if (eq > 0)
                    key = line.substring (0, eq).trim ();
            }

            if ((key != null) && keys.contains (key))
                continue;

            if (!first)
                out.append ('\n');
            out.append (raw);
            first = false;
        }

        if (trailing && (out.length () > 0))
            out.append ('\n');

        return out.toString ();
    }

    /** Активные пары key=value файла: последнее вхождение ключа выигрывает
     * (так же читает демон, cfg_parse.c). */
    private static java.util.LinkedHashMap<String, String> active (String text)
    {
        java.util.LinkedHashMap<String, String> map = new java.util.LinkedHashMap<> ();

        for (String raw : text.split ("\\n"))
        {
            String line = raw.trim ();
            if (line.isEmpty () || (line.charAt (0) == '#'))
                continue;

            int eq = line.indexOf ('=');
            if (eq <= 0)
                continue;

            String key = line.substring (0, eq).trim ();
            String val = line.substring (eq + 1).trim ();
            if (key.isEmpty ())
                continue;

            map.remove (key);		/* последнее вхождение — в конец порядка */
            map.put (key, val);
        }

        return map;
    }
}
