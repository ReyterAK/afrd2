/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

import android.content.Context;
import android.os.Build;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.Arrays;

import ru.cobra.zap.afrd.gui.R;

/**
 * This class provides a low-level API for communicating with afrd,
 */
public class Control
{
    private static final String AFRD_PID_FILE = "/dev/run/afrd.pid";
    // this attribute exists on Android 6 & 7
    private static final String SYSFS_ANDROID67 = "/sys/class/switch/hdmi/state";
    // this attribute exists on Android 8 & 9
    private static final String SYSFS_ANDROID89 = "/sys/class/switch/hdmi/cable.0/state";
    // these attributes should exist on all but castrated by MINIX kernels
    private static final String SYSFS_NOT_MINIX_1 = "/sys/class/vdec/vdec_status";
    private static final String SYSFS_NOT_MINIX_2 = "/sys/class/vdec/dump_vdec_blocks";
    private static final String SYSFS_NOT_MINIX_3 = "/sys/class/vdec/dump_vdec_chunks";

    public File mIni;
    public File mAfrd;
    public File mDispDex;
    public File mDispSh;

    /**
     * Новый путь демона (Android 10-14, ядро 5.x): определяется по узлам, как
     * platform_detect() в демоне — есть /sys/class/vdec/vdec_status и нет ни
     * одного из старых узлов /sys/class/switch/hdmi/*. Критерий обязан
     * совпадать с демоном: иначе профиль и путь демона разойдутся (R18).
     */
    public static boolean isAtv14 ()
    {
        return new File (SYSFS_NOT_MINIX_1).exists () &&
               !new File (SYSFS_ANDROID67).exists () &&
               !new File (SYSFS_ANDROID89).exists ();
    }

    public Control (Context ctx)
    {
        mIni = new File (ctx.getCacheDir (), "afrd.ini");
        mAfrd = new File (ctx.getCacheDir (), "afrd");
        mDispDex = new File (ctx.getCacheDir (), "afrd-disp.dex");
        mDispSh = new File (ctx.getCacheDir (), "afrd-disp.sh");
    }

    /**
     * Помощник смены режима для Android 10/11 (Amlogic): dex + обёртка.
     * Лежат в приватном каталоге приложения (демон работает от root и читает их),
     * путь к обёртке уходит в конфиг ключом vendor.disp.cli.
     */
    public String extractVendorCli (Context ctx)
    {
        if (!jfun.extractFile (ctx, R.raw.afrd_disp_dex, mDispDex))
            return ctx.getString (R.string.failed_copy_raw, mDispDex.getPath ());
        if (!jfun.extractFile (ctx, R.raw.afrd_disp_sh, mDispSh))
            return ctx.getString (R.string.failed_copy_raw, mDispSh.getPath ());

        mDispSh.setExecutable (true, false);
        return "";
    }

    /**
     * Записать ключ в ini, заменив существующую строку (или добавив её в конец).
     * Нужно для vendor.disp.cli: путь к помощнику зависит от каталога приложения.
     */
    private void setIniKey (String key, String value)
    {
        try
        {
            java.util.List<String> lines = new java.util.ArrayList<> ();
            boolean found = false;

            if (mIni.exists ())
            {
                java.io.BufferedReader r = new java.io.BufferedReader (
                        new java.io.InputStreamReader (new java.io.FileInputStream (mIni)));
                String line;
                while ((line = r.readLine ()) != null)
                {
                    if (line.startsWith (key + "="))
                    {
                        lines.add (key + "=" + value);
                        found = true;
                    }
                    else
                        lines.add (line);
                }
                r.close ();
            }

            if (!found)
            {
                lines.add ("# путь к помощнику смены режима (Android 10/11), ставит приложение");
                lines.add (key + "=" + value);
            }

            java.io.PrintWriter w = new java.io.PrintWriter (mIni, "UTF-8");
            for (String line : lines)
                w.println (line);
            w.close ();
        }
        catch (Exception exc)
        {
            jfun.logExc ("setIniKey", exc);
        }
    }

    public void restart ()
    {
        mAfrd.setExecutable (true);
        String[] cmd = new String[] { mAfrd.getPath () + " -k -D " + mIni.getPath () };
        Log.d ("afrd", "Run: " + Arrays.toString (cmd));
        RootShell.run (cmd, null);
        // give status another chance
        Status.mFail.success ();
    }

    public boolean isRunning ()
    {
        try
        {
            FileInputStream fis = new FileInputStream (AFRD_PID_FILE);
            byte[] data = new byte[300];
            int n = fis.read (data);
            fis.close ();

            while ((n > 0) && ((data[n - 1] < (byte) '0') || (data[n - 1] > (byte) '9')))
                n--;

            if (n > 0)
            {
                int pid = Integer.parseInt (new String (data, 0, n));
                fis = new FileInputStream ("/proc/" + pid + "/cmdline");
                fis.read (data);
                fis.close ();

                if (mAfrd.getPath ().equals (jfun.cstr (data)))
                    return true;

                Log.d ("afrd", "Cmdline: " + new String (data));
            }
        }
        catch (Exception exc)
        {
            jfun.logExc ("isRunning", exc);
        }

        return false;
    }

    public String extractConfig (Context ctx)
    {
        String kver = System.getProperty ("os.version", "");
        String hardware = jfun.getProperty ("ro.hardware");
        String model = jfun.getProperty ("ro.product.model");

        assert kver != null;
        assert hardware != null;
        assert model != null;

        int res_id;
        if (!hardware.equalsIgnoreCase ("amlogic"))
            return ctx.getString (R.string.only_amlogic, hardware);
        else if (kver.startsWith ("3.14.29") &&
            !new File (SYSFS_NOT_MINIX_1).exists () &&
            !new File (SYSFS_NOT_MINIX_2).exists () &&
            !new File (SYSFS_NOT_MINIX_3).exists ())
            res_id = R.raw.afrd_minix7;
        else if (isAtv14 ())
            // Android 10-14, ядро 5.x (ATV14): другой путь демона — опрос
            // /sys/class/vdec/vdec_status и backdoor SurfaceFlinger; узлов
            // /sys/class/switch/hdmi/* на этих ядрах нет. Конфиг — afrd2.ini.
            res_id = R.raw.afrd_atv14;
        else if (kver.startsWith ("4.") && new File (SYSFS_ANDROID89).exists ())
            // Android 8 or 9
            res_id = R.raw.afrd_8;
        else if (kver.startsWith ("3.") && new File (SYSFS_ANDROID67).exists ())
            // Android 6 or 7
            res_id = R.raw.afrd_7;
        else
            return ctx.getString (R.string.failed_detect_ostype, kver, hardware, model);

        if (!jfun.extractFile (ctx, res_id, mIni))
            return ctx.getString (R.string.failed_copy_raw, mIni.getPath ());

        /* Путь к помощнику смены режима (Android 10/11) зависит от каталога
         * приложения, поэтому профиль, извлечённый из ресурсов, дополняем
         * ключом — иначе демон искал бы помощник по умолчанию. */
        if (isAtv14 ())
            setIniKey ("vendor.disp.cli", mDispSh.getPath ());

        return "";
    }

    public String extractDaemon (Context ctx)
    {
        for (String arch : Build.SUPPORTED_ABIS)
        {
            int res_id;
            switch (arch)
            {
                case "armeabi-v7a":
                    res_id = R.raw.afrd_armeabi_v7a;
                    break;

                case "arm64-v8a":
                    res_id = R.raw.afrd_arm64_v8a;
                    break;

                default:
                    continue;
            }

            for (int i = 0; ; i++)
            {
                if (jfun.extractFile (ctx, res_id, mAfrd))
                    break;

                switch (i)
                {
                    case 0:
                        String[] cmd = new String[] { mAfrd.getPath () + " -k", "rm -f " + mAfrd.getPath () };
                        Log.d ("afrd", "Run: " + Arrays.toString (cmd));
                        RootShell.run (cmd, null);
                        break;

                    case 1:
                        return ctx.getString (R.string.failed_copy_raw, mAfrd.getPath ());
                }
            }

            return "";
        }

        return ctx.getString (R.string.arch_not_supported, Arrays.toString (Build.SUPPORTED_ABIS));
    }

    /**
     * Touch config file so that afrd reloads it in a few seconds.
     */
    public void reload ()
    {
        try
        {
            long now = System.currentTimeMillis ();
            if (!mIni.exists ())
                new FileOutputStream (mIni).close ();
            else
                mIni.setLastModified (now);
        }
        catch (IOException exc)
        {
            jfun.logExc ("reload", exc);
        }
    }
}