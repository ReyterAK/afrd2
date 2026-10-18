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
import android.os.Build;
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

        /* Помощник смены режима (Android 10/11) — в кэше приложения, а кэш
         * система чистит при нехватке места; тогда демон молча остаётся без
         * переключения до следующего обновления APK. Поэтому извлекаем его не
         * только при обновлении, но и когда файлов нет или они подозрительно
         * коротки (обрыв записи при той же чистке кэша).
         *
         * Признак отдельный: проверка обновления APK ниже ПЕРЕЗАПИСЫВАЕТ
         * rewrite, поэтому присваивать его здесь нельзя — иначе условие
         * мёртвое (живая находка прогона приёмки). */
        boolean helper_missing = Control.isAtv14 () &&
                                 (!mControl.mDispSh.exists () ||
                                  !mControl.mDispDex.exists () ||
                                  (mControl.mDispSh.length () < 64) ||
                                  (mControl.mDispDex.length () < 64));

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

        if (helper_missing)
            rewrite = true;

        if (rewrite)
        {
            String res = mControl.extractConfig (this);
            if (!res.isEmpty ())
            {
                showMessage (getString (R.string.msg_title_critical_error), res, null);
                return false;
            }

            /* Вендорный помощник смены режима (Android 10/11) — рядом с профилем:
             * тот же срок жизни, что у APK, и путь уже прописан в ini. */
            if (Control.isAtv14 ())
            {
                res = mControl.extractVendorCli (this);
                if (!res.isEmpty ())
                {
                    showMessage (getString (R.string.msg_title_critical_error), res, null);
                    return false;
                }
            }

            /* Быстрое решение по заявленной частоте: умолчание зависит от
             * версии Android (10/11 — включено, как у штатного системного AFR;
             * 12-14 — выключено). Проставляем ЯВНО, иначе XML-умолчание строки
             * настроек перебило бы умолчание пути и включило быстрый путь на
             * 12-14 (нашло финальное ревью). */
            if (Control.isAtv14 ())
            {
                /* 30 — Android 11: на 10/11 быстрый путь включён, на 12+ выключен.
                 * Литерал, а не VERSION_CODES.R: compileSdk проекта — 27. */
                boolean fast = (android.os.Build.VERSION.SDK_INT <= 30);
                mControl.setIniKey ("fps.declare.fast", fast ? "1" : "0");
                prefs.edit ().putBoolean ("fps_declare_fast", fast).apply ();
            }

            prefs.edit ().putLong ("afrd_ini_ver", mPackageInfo.lastUpdateTime).apply ();
        }

        return updateConfig ();
    }

    /* Умолчание switch.skip.pkg на Android 10/11: там штатный YouTube остаётся
     * исключённым (холодный старт 60-фпс ролика под 50 Гц зажимается, а сброс
     * потолка стоит 1.5-2.5 с). Профиль этого значения больше не содержит —
     * умолчание зависит от пути, — поэтому на 10/11 записываем его в настройки
     * один раз, чтобы строка настроек и поведение демона совпадали. Пустое
     * значение (пользователь снял исключение) не перетираем. */
    private void atv11SkipSeed ()
    {
        if (!Control.isAtv14 () || (android.os.Build.VERSION.SDK_INT > 30))
            return;

        if (mOptions.contains ("switch_skip_pkg"))
            return;

        mOptions.edit ().putString ("switch_skip_pkg", kSkipYoutube).apply ();
    }

    private boolean updateConfig ()
    {
        atv11SkipSeed ();

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
        atv14SkipDefault (conf);
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

    /* Перенос умолчания switch.skip.pkg на Android 12-14 (журнал §48.10).
     *
     * В прежних версиях профиль исключал штатный YouTube из AFR: плеер выбирал
     * темп отрисовки в момент старта ролика и удержанный низкий режим «зажимал»
     * следующий. На Android 12-14 зажим закрыт подъёмом режима до максимума
     * (switch.bump) и сбросом потолка (switch.ceiling.reset), поэтому исключение
     * больше не нужно. Но у уже установленного приложения это значение лежит в
     * настройках и в ini — без переноса обновление ничего бы не изменило.
     *
     * Убираем только штатный YouTube, свои записи пользователя сохраняем;
     * сделанную после обновления правку не трогаем никогда (флаг в requisites).
     * На Android 10/11 исключение остаётся: там холодный старт 60-фпс ролика под
     * 50 Гц зажимается, а сброс потолка стоит 1.5-2.5 с. */
    private static final String kSkipYoutube = "com.google.android.youtube.tv";

    private void atv14SkipDefault (Config conf)
    {
        if (!Control.isAtv14 ())
            return;

        /* 30 — Android 11: на 12+ исключение снимаем, на 10/11 оставляем.
         * Литерал, а не VERSION_CODES.S: compileSdk проекта — 27. */
        if (android.os.Build.VERSION.SDK_INT <= 30)
            return;

        SharedPreferences req = getSharedPreferences ("requisites", 0);
        if (req.getBoolean ("inert_skip_youtube_reset", false))
            return;

        String val = mOptions.getString ("switch_skip_pkg", null);
        if (val == null)
        {
            req.edit ().putBoolean ("inert_skip_youtube_reset", true).apply ();
            return;
        }

        String cleared = Seed.withoutPkg (val, kSkipYoutube);
        if (!cleared.equals (val))
        {
            mOptions.edit ().putString ("switch_skip_pkg", cleared).apply ();
            conf.put ("switch.skip.pkg", cleared);
        }

        req.edit ().putBoolean ("inert_skip_youtube_reset", true).apply ();
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

    /* Проверка root идёт в фоне: `su` на первом запуске ждёт диалог Magisk, и это
     * заблокировало бы основной поток. Но сам старт службы обязан выполняться из
     * ВИДИМОГО состояния — с Android 8 фоновый старт запрещён, Context.startService
     * отвечает IllegalStateException, и приложение падало (живая находка 15.10 на
     * спящем боксе ATV9: `Background start not allowed` → FATAL EXCEPTION →
     * Force finishing activity). Поэтому:
     *   - проверка идёт в фоне, а её ответ возвращается в основной поток (mAppHandler),
     *     то есть startService вызывается всегда в основном потоке;
     *   - кроме этого, старт повторяется из onResume — активность там заведомо видима,
     *     и это же снимает случай, когда проверка ответила, пока активность была не на
     *     экране (страховка на API 34, где фоновый старт ограничен сильнее);
     *   - сам вызов обёрнут try/catch: отказ системы не должен ронять приложение. */
    private boolean mSuAvailable = false;
    private boolean mServiceStarted = false;

    private void prepareSuperuser ()
    {
        AsyncTask.execute (new Runnable ()
        {
            @Override
            public void run ()
            {
                final boolean su = RootShell.suAvailable ();

                mAppHandler.post (new Runnable ()
                {
                    @Override
                    public void run ()
                    {
                        mSuAvailable = su;

                        if (!su)
                        {
                            showToast (R.string.afrd_requires_su, true);
                            return;
                        }

                        startAfrdService ();
                    }
                });
            }
        });
    }

    @Override
    protected void onResume ()
    {
        super.onResume ();

        /* Активность видима — фонового запрета на старт службы нет. Обычно к
         * этому моменту root ещё проверяется, и старт сделает ответ проверки;
         * сюда попадаем, когда приложение открыли повторно, а root уже
         * подтверждён (mSuAvailable) — тогда старт идёт прямо отсюда. */
        if (mSuAvailable)
            startAfrdService ();
    }

    @Override
    public void onWindowFocusChanged (boolean hasFocus)
    {
        super.onWindowFocusChanged (hasFocus);

        /* Повтор после отказа системы: у спящего бокса окно фокуса не имеет, и
         * система считает uid фоновым. Когда экран включается, окно фокус
         * получает — здесь и повторяем. Без этого отказ оставлял приложение без
         * службы и без демона до следующего открытия. */
        if (hasFocus && mSuAvailable)
            startAfrdService ();
    }

    /* Запуск службы-сторожа ровно один раз за жизнь активности. */
    private void startAfrdService ()
    {
        if (mServiceStarted)
            return;

        mServiceStarted = true;

        try
        {
            /* На API 26+ — startForegroundService, как в BootCompleteReceiver:
             * AFRService.onCreate сразу вызывает startForeground (в
             * updateNotification), поэтому пятисекундный срок соблюдён. */
            Intent intent = new Intent (getApplicationContext (), AFRService.class);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                startForegroundService (intent);
            else
                startService (intent);
        }
        catch (IllegalStateException exc)
        {
            /* сюда попадает и ForegroundServiceStartNotAllowedException (API 31+):
             * система отказала в старте из фона — скажем об этом и повторим,
             * когда окно получит фокус (onWindowFocusChanged). */
            mServiceStarted = false;
            jfun.logExc ("startAfrdService", exc);
            showToast (R.string.afrd_service_no_start, true);
        }
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
