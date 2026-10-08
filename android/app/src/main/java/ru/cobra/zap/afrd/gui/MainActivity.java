/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd.gui;

import android.app.ActionBar;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.Fragment;
import android.app.FragmentManager;
import android.app.FragmentTransaction;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.Resources;
import android.os.AsyncTask;
import android.os.Bundle;
import android.os.Handler;
import android.preference.Preference;
import android.preference.PreferenceFragment;
import android.preference.PreferenceManager;
import android.preference.PreferenceScreen;
import android.util.ArrayMap;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ListView;
import android.widget.SimpleAdapter;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Vector;

import ru.cobra.zap.afrd.RootShell;
import ru.cobra.zap.afrd.Config;
import ru.cobra.zap.afrd.Control;
import ru.cobra.zap.afrd.Seed;
import ru.cobra.zap.afrd.jfun;

public class MainActivity extends Activity
    implements PreferenceFragment.OnPreferenceStartFragmentCallback
{
    PackageInfo mPackageInfo;
    Control mControl;
    SharedPreferences mOptions;
    // used for the "press Back again to quit" thing
    long mBackTime = 0;
    // We don't use fragment stack; instead, we always have just one active fragment
    Fragment activeFrag;

    @Override
    protected void onCreate (Bundle savedInstanceState)
    {
        super.onCreate (savedInstanceState);

        jfun.limitDpi (this);
        ActionBar ab = getActionBar ();
        if (ab != null)
            ab.hide ();

        setContentView (R.layout.activity_main);

        try
        {
            mPackageInfo = getPackageManager ().getPackageInfo (getPackageName (), 0);
        }
        catch (PackageManager.NameNotFoundException e)
        {
            // Can't find own package? Gross!
            finishAndRemoveTask ();
        }

        ((TextView) findViewById (R.id.version)).setText (
            String.format (getString (R.string.afrdctl_version),
                BuildConfig.VERSION_NAME, BuildConfig.BUILD_TYPE));

        mControl = new Control (this);
        mOptions = getSharedPreferences ("ini_options", 0);

        createMainMenu ();
        prepareConfig ();
        prepareDaemon ();
        prepareSuperuser ();

        addFragment (StatusFragment.create (), "status");
    }

    private void createMainMenu ()
    {
        List<Map<String, String>> data = new ArrayList<> ();
        Map<String, String> menuent;
        menuent = new ArrayMap<> (2);
        menuent.put ("item", getString (R.string.menu_status));
        menuent.put ("desc", getString (R.string.menu_desc_status));
        data.add (menuent);
        menuent = new ArrayMap<> (2);
        menuent.put ("item", getString (R.string.menu_settings));
        menuent.put ("desc", getString (R.string.menu_desc_settings));
        data.add (menuent);
        menuent = new ArrayMap<> (2);
        menuent.put ("item", getString (R.string.menu_log));
        menuent.put ("desc", getString (R.string.menu_desc_log));
        data.add (menuent);
        menuent = new ArrayMap<> (2);
        menuent.put ("item", getString (R.string.menu_faq));
        menuent.put ("desc", getString (R.string.menu_desc_faq));
        data.add (menuent);
        menuent = new ArrayMap<> (2);
        menuent.put ("item", getString (R.string.menu_about));
        menuent.put ("desc", getString (R.string.menu_desc_about));
        data.add (menuent);

        SimpleAdapter adapter = new SimpleAdapter (this, data, android.R.layout.simple_list_item_2,
            new String[] { "item", "desc" }, new int[] { android.R.id.text1, android.R.id.text2 });
        ListView list = findViewById (R.id.menu);
        list.setAdapter(adapter);
        list.setOnItemClickListener (new AdapterView.OnItemClickListener ()
        {
            @Override
            public void onItemClick (AdapterView<?> parent, View view, int position, long id)
            {
                switch ((int)id)
                {
                    case 0:
                        if (getFragment ("status") == null)
                            addFragment (StatusFragment.create (), "status");
                        break;

                    case 1:
                        if (getFragment ("settings") == null)
                            addFragment (SettingsFragment.create (), "settings");
                        break;

                    case 2:
                        if (getFragment ("log") == null)
                            addFragment (LogFragment.create (
                                mOptions.getString ("log_file", null),
                                mOptions.getBoolean ("log_enable", false)),
                                "log");
                        break;

                    case 3:
                        if (getFragment ("faq") == null)
                            addFragment (FAQFragment.create (), "faq");
                        break;

                    case 4:
                        if (getFragment ("about") == null)
                            addFragment (AboutFragment.create (), "about");
                        break;
                }
            }
        });

    }

    private boolean prepareConfig ()
    {
        SharedPreferences prefs = getSharedPreferences ("requisites", 0);
        boolean rewrite = true;

        if (mControl.mIni.exists ())
            try
            {
                /* ключ — время установки APK, а не versionName: форк держит
                 * versionName неизменным, и обновление APK не переизвлекало
                 * демон/профиль из ресурсов (в кэше оставался старый) */
                rewrite = (prefs.getLong ("afrd_ini_ver", 0) != mPackageInfo.lastUpdateTime);
            }
            catch (java.lang.ClassCastException ignored)
            {
            }

        if (rewrite)
        {
            String res = mControl.extractConfig (this);
            if (!res.isEmpty ())
            {
                showMessage (getString (R.string.msg_title_critical_error), res, null);
                return false;
            }

            prefs.edit ().putLong ("afrd_ini_ver", mPackageInfo.lastUpdateTime).apply ();
        }

        return updateConfig ();
    }

    private boolean updateConfig ()
    {
        // load config options from ini file
        Config conf = new Config ();
        if (!conf.load (mControl.mIni))
            return false;

        if (conf.getString ("log.file", null) == null)
            conf.put ("log.file", new File (getCacheDir (), "afrd.log").getPath ());

        // apply user preferences on top
        if (mOptions.contains ("enable"))
        {
            /* Сначала убираем записи, оставшиеся от версии, где ключ не попадал
             * в конфиг, — иначе наложение настроек сразу же подменит профильное
             * значение старым умолчанием XML. */
            resetInertKeys (conf);

            conf.load (mOptions);

            /* Ключи, появившиеся в GUI позже установки приложения, в настройках
             * отсутствуют: без этого строка показала бы пустое значение, а
             * первая же правка настроек записала бы пустое значение поверх
             * профильного умолчания (switch.skip.pkg потерял бы исключение
             * для YouTube TV). */
            seedOptions (conf);
        }
        else
        {
            PreferenceManager.setDefaultValues (this, "ini_options", 0, R.xml.preferences, true);
            conf.save (mOptions);
        }

        // and save the modified file
        atv14PauseDefault (conf);
        return conf.condSave (mControl.mIni);
    }

    /* Умолчания ключей, которые до этой версии не переносились в конфиг
     * (их не было в Config.itemType): в настройках приложения у таких ключей
     * осталось умолчание XML, которое ни на что не влияло, — демон читал
     * профиль. Оставить его нельзя: став действующим, оно молча подменит
     * проверенную профильную настройку (12000 вместо 14000 для окна раннего
     * решения). Поэтому один раз при первом запуске новой версии убираем такую
     * запись и записываем в конфиг новое умолчание — строка настроек и демон
     * после этого показывают одно и то же. Своё значение пользователя
     * (не равное старому умолчанию) не трогаем, а сделанную после обновления
     * правку не трогаем никогда — перенос одноразовый (флаг в requisites). */
    private static final String [][] kInertDefaults =
    {
        /* настройка (префы) | ключ ini | старое умолчание XML | новое */
        { "fps_measure_envelope_early_ms", "fps.measure.envelope.early.ms", "12000", "14000" },
        { "fps_cadence_fix_ms", "fps.cadence.fix.ms", "2500", "2500" },
    };

    private void resetInertKeys (Config conf)
    {
        SharedPreferences req = getSharedPreferences ("requisites", 0);
        if (req.getBoolean ("inert_defaults_reset", false))
            return;

        SharedPreferences.Editor edit = null;

        for (String [] item : kInertDefaults)
        {
            if (!mOptions.contains (item [0]))
                continue;

            /* Правило — в Seed.shouldReset (проверяется tests/SeedTest.java). */
            if (!Seed.shouldReset (mOptions.getString (item [0], ""), item [2]))
                continue;

            if (edit == null)
                edit = mOptions.edit ();
            edit.remove (item [0]);

            /* Настройка и демон должны показывать одно и то же: в конфиге
             * оставляем новое умолчание, а не то, что записал старый APK. */
            conf.put (item [1], item [3]);
        }

        if (edit != null)
            edit.apply ();

        req.edit ().putBoolean ("inert_defaults_reset", true).apply ();
    }

    /* Перенос умолчания switch.delay.off на новом пути. На ATV14 ключ означает
     * предел терпения к паузе, а его XML-умолчание 10000 осталось от старого
     * смысла (задержка возврата после остановки видео): без переноса чистая
     * установка молча дала бы 10 с терпения к паузе, о чём пользователь не
     * просил. Поэтому один раз при первом запуске новой версии на ATV14
     * приводим настройку к умолчанию нового пути — 0. Своё значение
     * пользователя (не равное старому умолчанию) не трогаем, а сделанную после
     * обновления правку не трогаем никогда: перенос одноразовый (флаг в
     * requisites), по образцу kInertDefaults. */
    private void atv14PauseDefault (Config conf)
    {
        if (!Control.isAtv14 ())
            return;

        SharedPreferences req = getSharedPreferences ("requisites", 0);
        if (req.getBoolean ("inert_pause_delay_reset", false))
            return;

        String val = mOptions.getString ("switch_delay_off", null);

        /* Отсутствие значения — не своё значение (при null Seed.shouldReset
         * вернул бы false), поэтому обрабатываем его отдельно. Правило
         * сравнения со старым умолчанием — в Seed.shouldReset. */
        if ((val == null) || Seed.shouldReset (val, "10000"))
        {
            mOptions.edit ().putString ("switch_delay_off", "0").apply ();
            conf.put ("switch.delay.off", "0");
        }

        req.edit ().putBoolean ("inert_pause_delay_reset", true).apply ();
    }

    /* Настройки, которых ещё нет в ini_options, берём из конфига профиля. */
    private void seedOptions (Config conf)
    {
        final String [] seeded = { "switch.skip.pkg", "fps.correct.av1.pkg", "mode.min.hz.list" };
        SharedPreferences.Editor edit = null;

        for (String key : seeded)
        {
            String pref = key.replace ('.', '_');
            String value = conf.getString (key, "");

            /* Правило «пустое значение профиля не сеем, непустое сеем, уже
             * заданное не перетираем» — в Seed (проверяется tests/SeedTest.java).
             * Пустое значение — не умолчание: посев пустоты превратил бы
             * закомментированную строку профиля в активную пустую и затёр
             * ручную правку ключа при следующем сохранении настроек. Непустое,
             * наоборот, переносим: иначе первая же правка настроек заменила бы
             * его пустотой (switch.skip.pkg потерял бы исключение для YouTube). */
            if (!Seed.shouldSeed (value, mOptions.contains (pref)))
                continue;

            if (edit == null)
                edit = mOptions.edit ();
            edit.putString (pref, Seed.value (value));
        }

        if (edit != null)
            edit.apply ();
    }

    private void prepareDaemon ()
    {
        SharedPreferences prefs = getSharedPreferences ("requisites", 0);
        boolean rewrite = true;

        if (mControl.mAfrd.exists ())
            try
            {
                /* см. prepareConfig: ключ — время установки APK, иначе
                 * обновлённый APK оставлял в кэше старый бинарник демона */
                rewrite = (prefs.getLong ("afrd_ver", 0) != mPackageInfo.lastUpdateTime);
            }
            catch (java.lang.ClassCastException ignored)
            {
            }

        if (!rewrite)
            return;

        String res = mControl.extractDaemon (this);
        if (!res.isEmpty ())
        {
            showMessage (getString (R.string.msg_title_critical_error), res, null);
            return;
        }

        prefs.edit ().putLong ("afrd_ver", mPackageInfo.lastUpdateTime).apply ();
    }

    private void prepareSuperuser ()
    {
        AsyncTask.execute (new Runnable ()
        {
            @Override
            public void run ()
            {
                if (!RootShell.suAvailable ())
                {
                    showToast (R.string.afrd_requires_su, true);
                    return;
                }

                startService (new Intent (getApplicationContext (), AFRService.class));
            }
        });
    }

    private void showMessage (String title, String msg, DialogInterface.OnClickListener onOk)
    {
        AlertDialog.Builder dlg = new AlertDialog.Builder (this);
        dlg.setMessage (msg);
        dlg.setTitle (title);
        dlg.setCancelable (true);
        dlg.setPositiveButton (getResources ().getString (R.string.ok), onOk);
        dlg.create ().show ();
    }

    private void showMessage (int title_id, int msg_id, Object... args)
    {
        Resources res = getResources ();
        String msg = res.getString (msg_id);
        if (args.length > 0)
            msg = String.format (msg, args);
        showMessage (res.getString (title_id), msg, null);
    }

    private static Handler mAppHandler = new Handler ();

    public void showToast (final String msg, final boolean durable)
    {
        mAppHandler.post (new Runnable ()
        {
            @Override
            public void run ()
            {
                Toast.makeText (getApplicationContext (), msg,
                    durable ? Toast.LENGTH_LONG : Toast.LENGTH_SHORT).show ();
            }
        });
    }

    public void showToast (int msg_id, boolean durable)
    {
        String msg = getResources ().getString (msg_id);
        showToast (msg, durable);
    }

    public void addFragment (Fragment fragment, String tag, boolean toplevel)
    {
        if (fragment == null)
            return;

        FragmentManager fm = getFragmentManager ();

        if (toplevel)
        {
            if (fm.getBackStackEntryCount () > 0)
                fm.popBackStack (fm.getBackStackEntryAt (0).getId (),
                    FragmentManager.POP_BACK_STACK_INCLUSIVE);
        }

        activeFrag = fragment;
        fm.beginTransaction ().
            replace (R.id.content_holder, fragment, tag).
            setTransition (FragmentTransaction.TRANSIT_FRAGMENT_FADE).
            addToBackStack (null).
            commit ();
    }

    public void addFragment (Fragment fragment, String tag)
    {
        addFragment (fragment, tag, true);
    }

    public Fragment getFragment (String tag)
    {
        return getFragmentManager ().findFragmentByTag (tag);
    }

    @Override
    public boolean onPreferenceStartFragment (PreferenceFragment caller, Preference pref)
    {
        if (activeFrag instanceof FragmentPref)
            return ((FragmentPref) activeFrag).onPreferenceStartFragment (caller, pref);

        return false;
    }

    @Override
    public void onBackPressed ()
    {
        if (activeFrag instanceof FragmentBack)
            if (((FragmentBack) activeFrag).onBackPressed ())
                return;

        long now = System.currentTimeMillis ();
        if ((mBackTime == 0) || (now - mBackTime > 1000))
        {
            mBackTime = now;
            showToast (R.string.press_back_again, false);
            return;
        }

        finishAndRemoveTask ();
    }

    public void applyPrefs ()
    {
        Config conf = new Config ();
        if (!conf.load (mControl.mIni))
        {
            showToast (R.string.msg_apply_prefs_failed, true);
            return;
        }

        // load SharedPreferences into the ini file
        conf.load (mOptions);
        if (!conf.isModified ())
            return;

        if (!conf.save (mControl.mIni))
        {
            showToast (R.string.msg_apply_prefs_failed, true);
            return;
        }

        /* Демон читает конфиг при входе (на ATV14 — и по изменению файла,
         * не чаще раза в ~5 с), поэтому после записи настроек будим его:
         * без этого, например, выключение «Автоматического подбора частоты»
         * не действовало до перезапуска демона (находка владельца 02.10). */
        mControl.reload ();

        showToast (R.string.msg_apply_prefs_ok, true);
    }

    public void resetPrefs ()
    {
        // reset application preferences
        mOptions.edit ().clear ().apply ();

        // delete ini file & re-extract ini file from raw resources
        if (!mControl.mIni.delete () ||
            !prepareConfig ())
        {
            showToast (R.string.msg_reset_prefs_failed, true);
            return;
        }

        showToast (R.string.msg_reset_prefs_ok, true);

        // reset preferences if it is the active fragment
        if (getFragment ("settings") != null)
            addFragment (new SettingsFragment (), "settings");
    }

    interface FragmentBack
    {
        boolean onBackPressed ();
    }

    interface FragmentPref
    {
        boolean onPreferenceStartFragment (PreferenceFragment caller, Preference pref);
    }
}
