/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.drawable.Drawable;

import java.text.Collator;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

/**
 * Список приложений для настроек, значения которых — имена пакетов
 * (switch.skip.pkg, fps.correct.av1.pkg). Значение настройки — пакеты через
 * пробел, ровно в том виде, в каком его читает демон (strlist_load).
 */
public class Apps
{
    /** Строка списка выбора. */
    public static class App
    {
        public final String pkg;
        public final CharSequence label;
        public final boolean installed;

        App (String pkg, CharSequence label, boolean installed)
        {
            this.pkg = pkg;
            this.label = label;
            this.installed = installed;
        }
    }

    /* Типы, которые спрашиваем у системы. Одного "video/*" мало: интент с типом
     * video/* не совпадает с фильтром, объявившим video/mp4 (совпадение идёт по
     * точному типу или по wildcard в фильтре), поэтому перечисляем и частные. */
    private static final String [] VIDEO_MIME =
    {
        "video/*", "video/mp4", "video/x-matroska", "video/webm", "video/x-msvideo",
        "video/mpeg", "video/quicktime", "video/mp2t", "video/x-flv", "video/3gpp",
        "application/x-mpegURL", "application/vnd.apple.mpegurl",
    };

    /** Пакеты из значения настройки. */
    public static List<String> split (String value)
    {
        List<String> out = new ArrayList<> ();
        if (value == null)
            return out;

        for (String s : value.trim ().split ("\\s+"))
            if (!s.isEmpty () && !out.contains (s))
                out.add (s);

        return out;
    }

    /** Значение настройки из списка пакетов. */
    public static String join (List<String> pkgs)
    {
        StringBuilder sb = new StringBuilder ();
        for (String s : pkgs)
        {
            if (s == null || s.isEmpty ())
                continue;
            if (sb.length () > 0)
                sb.append (' ');
            sb.append (s);
        }
        return sb.toString ();
    }

    /** Имя приложения или сам пакет, если приложение не установлено. */
    public static CharSequence label (Context ctx, String pkg)
    {
        PackageManager pm = ctx.getPackageManager ();
        try
        {
            return pm.getApplicationInfo (pkg, 0).loadLabel (pm);
        }
        catch (PackageManager.NameNotFoundException e)
        {
            return pkg;
        }
    }

    /** Иконка приложения (null — приложение не установлено). */
    public static Drawable icon (Context ctx, String pkg)
    {
        PackageManager pm = ctx.getPackageManager ();
        try
        {
            return pm.getApplicationInfo (pkg, 0).loadIcon (pm);
        }
        catch (PackageManager.NameNotFoundException e)
        {
            return null;
        }
    }

    /** Имена приложений для строки настроек: "YouTube TV, VLC". */
    public static String names (Context ctx, String value)
    {
        List<String> pkgs = split (value);
        StringBuilder sb = new StringBuilder ();
        for (String pkg : pkgs)
        {
            if (sb.length () > 0)
                sb.append (", ");
            sb.append (label (ctx, pkg));
        }
        return sb.toString ();
    }

    /** Приложения, умеющие открывать видео. */
    public static List<App> videoPlayers (Context ctx)
    {
        PackageManager pm = ctx.getPackageManager ();
        List<String> pkgs = new ArrayList<> ();

        for (String mime : VIDEO_MIME)
        {
            Intent in = new Intent (Intent.ACTION_VIEW);
            in.setType (mime);
            List<ResolveInfo> found = pm.queryIntentActivities (in, 0);
            if (found == null)
                continue;

            for (ResolveInfo ri : found)
                if ((ri.activityInfo != null) &&
                    !pkgs.contains (ri.activityInfo.packageName))
                    pkgs.add (ri.activityInfo.packageName);
        }

        return build (ctx, pkgs);
    }

    /** Все установленные приложения, которые можно запустить. */
    public static List<App> allApps (Context ctx)
    {
        PackageManager pm = ctx.getPackageManager ();
        List<String> pkgs = new ArrayList<> ();

        for (ApplicationInfo ai : pm.getInstalledApplications (0))
            pkgs.add (ai.packageName);

        return build (ctx, pkgs);
    }

    /** Строка для одного пакета (в том числе удалённого). */
    public static App app (Context ctx, String pkg)
    {
        try
        {
            ApplicationInfo ai = ctx.getPackageManager ().getApplicationInfo (pkg, 0);
            return new App (pkg, ai.loadLabel (ctx.getPackageManager ()), true);
        }
        catch (PackageManager.NameNotFoundException e)
        {
            return new App (pkg, pkg, false);
        }
    }

    /** Индекс пакета в списке или -1. */
    public static int indexOf (List<App> list, String pkg)
    {
        for (int i = 0; i < list.size (); i++)
            if (list.get (i).pkg.equals (pkg))
                return i;

        return -1;
    }

    /* Отсеять не запускаемые приложения (на переднем плане такое оказаться не
     * может) и сам AFRd2, отсортировать по локализованному имени. Иконки здесь
     * не грузятся: их берёт адаптер лениво, только для видимых строк. */
    private static List<App> build (Context ctx, List<String> pkgs)
    {
        PackageManager pm = ctx.getPackageManager ();
        String self = ctx.getPackageName ();
        List<App> out = new ArrayList<> ();

        for (String pkg : pkgs)
        {
            if (pkg.equals (self) || (pm.getLaunchIntentForPackage (pkg) == null))
                continue;

            try
            {
                ApplicationInfo ai = pm.getApplicationInfo (pkg, 0);
                out.add (new App (pkg, ai.loadLabel (pm), true));
            }
            catch (PackageManager.NameNotFoundException ignored)
            {
            }
        }

        final Collator coll = Collator.getInstance ();
        Collections.sort (out, new Comparator<App> ()
        {
            @Override
            public int compare (App a, App b)
            {
                return coll.compare (a.label.toString (), b.label.toString ());
            }
        });

        return out;
    }
}
