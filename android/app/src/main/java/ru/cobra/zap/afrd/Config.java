/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

import android.content.SharedPreferences;
import android.util.ArrayMap;
import android.util.Log;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;

public class Config
{
    private ArrayMap<String, Object> mItems;

    private boolean mModified = false;

    // Configuration item data type
    public static final int TYPE_MASK = IniValue.TYPE_MASK;
    public static final int TYPE_BOOL = IniValue.TYPE_BOOL;
    public static final int TYPE_INT  = IniValue.TYPE_INT;
    public static final int TYPE_STR  = IniValue.TYPE_STR;

    public static ArrayMap<String, Integer> itemType;

    static
    {
        itemType = new ArrayMap<> ();
        itemType.put ("enable", TYPE_BOOL);
        itemType.put ("log.enable", TYPE_BOOL);
        itemType.put ("log.file", TYPE_STR);
        itemType.put ("mode.prefer.exact", TYPE_BOOL);
        itemType.put ("mode.use.fract", TYPE_STR);
        itemType.put ("mode.blacklist.rates", TYPE_STR);
        itemType.put ("switch.skip.pkg", TYPE_STR);
        itemType.put ("switch.bump.pkg", TYPE_STR);
        itemType.put ("mode.min.hz.list", TYPE_STR);
        itemType.put ("mode.extra", TYPE_STR);
        itemType.put ("cs.select", TYPE_STR);
        itemType.put ("cs.restore", TYPE_BOOL);
        itemType.put ("switch.delay.on", TYPE_INT);
        itemType.put ("switch.delay.off", TYPE_INT);
        itemType.put ("switch.delay.retry", TYPE_INT);
        itemType.put ("switch.timeout", TYPE_INT);
        itemType.put ("switch.blackout", TYPE_INT);
        itemType.put ("switch.ignore", TYPE_INT);
        itemType.put ("switch.hdmi", TYPE_INT);
        itemType.put ("vdec.blacklist", TYPE_STR);
        itemType.put ("frhint.vdec.blacklist", TYPE_STR);

        // Новый путь (Android 10-14 / ATV14): если ключа нет в этой таблице,
        // приложение не перенесёт его значение из настроек в ini (см. load()),
        // и пункт GUI окажется инертным.
        itemType.put ("trigger.poll.ms", TYPE_INT);
        itemType.put ("trigger.start.debounce.ms", TYPE_INT);
        itemType.put ("trigger.stop.debounce.ms", TYPE_INT);
        itemType.put ("fps.measure.envelope.ms", TYPE_INT);
        itemType.put ("fps.measure.envelope.max.ms", TYPE_INT);
        itemType.put ("fps.measure.envelope.sub.ms", TYPE_INT);
        itemType.put ("fps.measure.envelope.skip.ms", TYPE_INT);
        itemType.put ("fps.measure.retries", TYPE_INT);
        itemType.put ("fps.declared.crosscheck.pct", TYPE_INT);
        itemType.put ("fps.recheck.delay.ms", TYPE_INT);
        /* Эти два ключа были в GUI и в cfgkeys.c, но не здесь: правка строк
         * молча не доезжала до afrd.ini, а строка показывала умолчание XML.
         * Проверяет tests/test_inactive.c. */
        itemType.put ("fps.measure.envelope.early.ms", TYPE_INT);
        itemType.put ("fps.cadence.fix.ms", TYPE_INT);
        itemType.put ("fps.prefer.api", TYPE_BOOL);
        // Быстрое решение по заявленной частоте кадра (Android 10/11 включено,
        // ATV14 выключено умолчанием пути — см. afrd_run).
        itemType.put ("fps.declare.fast", TYPE_BOOL);
        itemType.put ("fps.correct.enable", TYPE_BOOL);
        itemType.put ("fps.correct.list", TYPE_STR);
        itemType.put ("fps.correct.av1", TYPE_STR);
        itemType.put ("fps.correct.av1.pkg", TYPE_STR);
        itemType.put ("pause.mode", TYPE_STR);
        itemType.put ("pause.keep", TYPE_BOOL);
        itemType.put ("switch.restore.on.stop", TYPE_BOOL);
        /* «Устранять зажим частоты» (один пункт GUI): подъём режима до
         * максимума на старте видео. Без записи в этой таблице правка тумблера
         * не доезжала бы до afrd.ini (тот же класс дефекта, что был у
         * fps.cadence.fix.ms — см. tests/test_inactive.c). */
        itemType.put ("switch.bump", TYPE_BOOL);
        itemType.put ("switch.ceiling.reset", TYPE_BOOL);
    }

    public boolean getBoolean (String key, boolean defval)
    {
        if (mItems.containsKey (key))
        {
            Object ret = mItems.get (key);
            if (ret instanceof Boolean)
                return (Boolean) ret;
            return parseBoolean (ret.toString ());
        }

        return defval;
    }

    public int getInt (String key, int defval)
    {
        if (mItems.containsKey (key))
        {
            Object ret = mItems.get (key);
            if (ret instanceof Integer)
                return (Integer) ret;
            return Integer.parseInt (ret.toString ());
        }

        return defval;
    }

    public String getString (String key, String defval)
    {
        if (mItems.containsKey (key))
        {
            Object ret = mItems.get (key);
            if (ret instanceof String)
                return (String) ret;
            return ret.toString ();
        }

        return defval;
    }

    /* Удалить ключ из конфига. Нужно для переносов умолчаний: «ключа нет» и
     * «ключ пуст» — разные вещи (демон по-разному трактует switch.skip.pkg). */
    public void remove (String key)
    {
        if (mItems.remove (key) != null)
            mModified = true;
    }

    public void put (String key, Object val)
    {
        if (!mModified && !equals (mItems.get (key), val))
            mModified = true;
        mItems.put (key, val);
    }

    private boolean equals (Object a, Object b)
    {
        if ((a == null) || (b == null))
            return a == b;

        String as = a.toString ();
        String bs = b.toString ();
        return as.equals (bs);
    }

    public boolean load (File ini)
    {
        mItems = new ArrayMap<> ();
        mModified = false;

        try
        {
            FileInputStream fis = new FileInputStream (ini);
            BufferedReader bufr = new BufferedReader (new InputStreamReader (fis, StandardCharsets.UTF_8));
            String line;
            while ((line = bufr.readLine ()) != null)
            {
                line = line.trim ();
                // skip empty lines & comments
                if (line.length () == 0 || line.charAt (0) == '#')
                    continue;
                // otherwise, split key=value
                String key, val;
                int eq = line.indexOf ('=');
                if (eq < 0)
                {
                    // consider it a wrongly formed "key=" line
                    key = line;
                    val = "";
                }
                else
                {
                    key = line.substring (0, eq).trim ();
                    val = line.substring (eq + 1).trim ();
                }

                Object oval = IniValue.strip (val);
                Integer it = itemType.get (key);
                if (it != null)
                    oval = IniValue.parse (it, val);

                mItems.put (key, oval);
            }
        }
        catch (IOException e)
        {
            return false;
        }

        return true;
    }

    private Boolean parseBoolean (String val)
    {
        val = val.trim ().toLowerCase ();
        return !("0".equals (val) || "false".equals (val) || "no".equals (val));
    }

    public boolean isModified ()
    { return mModified; }

    public boolean condSave (File ini)
    {
        if (!mModified)
            return true;

        return save (ini);
    }

    public boolean save (File ini)
    {
        File out_ini = new File (ini.getPath () + "~");
        try
        {
            /* Итоговые значения: Boolean → 1/0. Пишем файл одним проходом через
             * IniRewrite — на каждый ключ одна активная строка, подсказки
             * остаются комментариями. Прежняя построчная запись «оживляла»
             * подсказки профиля и могла оставить две активные строки ключа:
             * демон читает последнюю, GUI показывает первую (журнал §104). */
            java.util.LinkedHashMap<String, String> values = new java.util.LinkedHashMap<> ();
            for (int i = 0; i < mItems.size (); i++)
            {
                Object v = mItems.valueAt (i);
                if (v == null)
                    continue;

                values.put (mItems.keyAt (i),
                            (v instanceof Boolean) ? (((Boolean) v) ? "1" : "0") : v.toString ());
            }

            String text = "";
            if (ini.exists ())
            {
                FileInputStream fis = new FileInputStream (ini);
                byte [] buf = new byte [(int) ini.length ()];
                int n = fis.read (buf);
                fis.close ();
                if (n > 0)
                    text = new String (buf, 0, n, StandardCharsets.UTF_8);
            }

            String out = IniRewrite.apply (text, values);

            FileOutputStream fos = new FileOutputStream (out_ini);
            BufferedWriter bufw = new BufferedWriter (new OutputStreamWriter (fos, StandardCharsets.UTF_8));
            bufw.write (out);
            bufw.flush ();
            bufw.close ();
        }
        catch (IOException e)
        {
            return false;
        }

        if (!ini.delete ())
            return false;

        if (!out_ini.renameTo (ini))
            return false;

        mModified = false;
        return true;
    }

    /**
     * Override ini values from user's application preferences
     *
     * @param prefs application preferences
     */
    public void load (SharedPreferences prefs)
    {
        for (String key : prefs.getAll ().keySet ())
        {
            String ini_key = key.replace ('_', '.');
            Integer it = itemType.get (ini_key);
            if (it != null)
                try
                {
                    Object val;
                    switch (it & TYPE_MASK)
                    {
                        case TYPE_BOOL: val = prefs.getBoolean (key, true); break;
                        /* Через тот же разбор, что и ini: пустая строка в
                         * целочисленной настройке раньше молча выпадала из
                         * переноса (NumberFormatException в пустом catch), и
                         * правка «не доезжала» до демона (P2-14). */
                        case TYPE_INT: val = IniValue.parse (it, prefs.getString (key, "0")); break;
                        case TYPE_STR: val = prefs.getString (key, ""); break;
                        default: continue;
                    }
                    put (ini_key, val);
                }
                catch (Exception e)
                {
                    Log.w ("afrd2", "настройка «" + key + "» не перенесена в ini: " + e);
                }
        }
    }

    /**
     * Save ini keys to user's application preferences.
     * This is usually called at first initialization to set default preference values.
     *
     * @param prefs application preferences
     */
    public void save (SharedPreferences prefs)
    {
        SharedPreferences.Editor edit = prefs.edit ();

        for (int i = 0; i < mItems.size (); i++)
        {
            String ini_key = mItems.keyAt (i);
            String key = ini_key.replace ('.', '_');
            Integer it = itemType.get (ini_key);
            if (it != null)
                switch (it & TYPE_MASK)
                {
                    case TYPE_BOOL: edit.putBoolean (key, getBoolean (ini_key, false)); break;
                    case TYPE_INT: edit.putString (key, ((Integer) getInt (ini_key, 0)).toString ()); break;
                    case TYPE_STR: edit.putString (key, getString (ini_key, "")); break;
                }
        }

        edit.apply ();
    }
}
