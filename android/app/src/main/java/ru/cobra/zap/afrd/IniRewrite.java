/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

/**
 * Запись значений в ini без порчи файла.
 *
 * Профиль из ресурсов держит часть ключей закомментированными — это подсказки
 * для ручной правки (`#fps.declare.fast=0`, `#switch.revert=1`). Прежняя
 * запись «оживляла» такую строку и вдобавок могла дописать вторую активную
 * строку того же ключа: демон читает последнюю, поэтому переключатель в GUI
 * оказывался инертным (живой случай журнала §104).
 *
 * Правила здесь:
 *   * на каждый ключ — ровно одна активная строка;
 *   * подсказки-комментарии остаются комментариями;
 *   * активная строка переписывается на своём месте (порядок файла не рвётся);
 *   * ключа в файле нет — дописывается в конец;
 *   * прочие строки (комментарии, пустые, мусор) остаются байт в байт.
 *
 * Класс без Android-зависимостей — под host-тест tests/IniRewriteTest.java.
 */
public class IniRewrite
{
    /** Применить значения к тексту ini. overrides: ключ → значение (null —
     * значение не задано, строка не трогается). Возвращает новый текст. */
    public static String apply (String text, java.util.Map<String, String> overrides)
    {
        if (text == null)
            text = "";

        java.util.LinkedHashMap<String, String> active = active (text);

        if (overrides != null)
            for (java.util.Map.Entry<String, String> e : overrides.entrySet ())
                if ((e.getKey () != null) && !e.getKey ().isEmpty () && (e.getValue () != null))
                    active.put (e.getKey (), e.getValue ());

        java.util.ArrayList<String> out = new java.util.ArrayList<> ();
        java.util.HashSet<String> emitted = new java.util.HashSet<> ();
        boolean trailing = text.endsWith ("\n");
        String [] lines = text.isEmpty () ? new String [0] : text.split ("\n", -1);
        int n = lines.length;
        if (trailing && (n > 0))
            n--;			/* последний пустой элемент — от перевода строки */

        for (int i = 0; i < n; i++)
        {
            String raw = lines [i];
            String line = raw.trim ();
            String key = null;

            if (!line.isEmpty () && (line.charAt (0) != '#'))
            {
                int eq = line.indexOf ('=');
                if (eq > 0)
                    key = line.substring (0, eq).trim ();
            }

            if ((key != null) && !key.isEmpty () && active.containsKey (key))
            {
                /* Активная строка: пишем итоговое значение один раз, дубли
                 * выбрасываем — иначе демон возьмёт последний, а GUI покажет
                 * первый. */
                if (emitted.add (key))
                    out.add (key + "=" + active.get (key));
                continue;
            }

            out.add (raw);
        }

        boolean added = false;
        for (java.util.Map.Entry<String, String> e : active.entrySet ())
        {
            if (emitted.contains (e.getKey ()))
                continue;

            out.add (e.getKey () + "=" + e.getValue ());
            emitted.add (e.getKey ());
            added = true;
        }

        StringBuilder res = new StringBuilder ();
        for (int i = 0; i < out.size (); i++)
        {
            if (i > 0)
                res.append ('\n');
            res.append (out.get (i));
        }

        if ((trailing || added) && (out.size () > 0))
            res.append ('\n');

        return res.toString ();
    }

    /** Активные пары файла; последнее вхождение ключа выигрывает (так же
     * читает демон, cfg_parse.c). */
    static java.util.LinkedHashMap<String, String> active (String text)
    {
        java.util.LinkedHashMap<String, String> map = new java.util.LinkedHashMap<> ();

        for (String raw : text.split ("\n"))
        {
            String line = raw.trim ();
            if (line.isEmpty () || (line.charAt (0) == '#'))
                continue;

            int eq = line.indexOf ('=');
            if (eq <= 0)
                continue;

            String key = line.substring (0, eq).trim ();
            if (key.isEmpty ())
                continue;

            map.remove (key);		/* последнее вхождение — в конец порядка */
            map.put (key, line.substring (eq + 1).trim ());
        }

        return map;
    }
}
