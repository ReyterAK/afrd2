/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd.gui;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.preference.EditTextPreference;
import android.preference.ListPreference;
import android.preference.MultiSelectListPreference;
import android.preference.Preference;
import android.preference.PreferenceFragment;
import android.preference.PreferenceGroup;
import android.preference.PreferenceScreen;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

import java.util.HashMap;
import java.util.Map;
import java.util.Vector;

import ru.cobra.zap.afrd.Apps;
import ru.cobra.zap.afrd.Control;
import ru.cobra.zap.xprefs.XAppsPreference;

/**
 * This fragment implements a editor for AFRd settings.
 */
public class SettingsFragment extends PreferenceFragment
    implements SharedPreferences.OnSharedPreferenceChangeListener,
        MainActivity.FragmentBack, MainActivity.FragmentPref
{
    private MainActivity mMain;
    Vector<PreferenceScreen> mPrefStack = new Vector<> ();

    /* Настройки, которые на новом пути демона (Android 10-14) не действуют:
     * режим переключает фреймворк транзакцией SurfaceFlinger 1035, затемнения
     * нет, цвета/HDR ведёт вендорский стек, uevent-фильтры не нужны. Демон эти
     * ключи не читает (см. таблицу в cfgkeys.c) и сообщает о них в логе.
     * В GUI они остаются видимыми, но выключенными — чтобы не создавать
     * впечатление работающей настройки. */
    private static final String [] kInactiveOnAtv14 =
    {
        "mode_extra",
        "cs_select",
        "switch_delay_on",
        "switch_delay_off",
        "switch_delay_retry",
        "switch_blackout",
        "switch_ignore",
        "switch_hdmi",
        "vdec_blacklist",
        "frhint_vdec_blacklist",
    };

    /* Настройки, которые демон объявляет ключами нового пути (CFGKEY_ATV14) и
     * потому на Android 6-9 не читает. switch.skip.pkg здесь нет: он действует
     * на обоих путях (перенос 2026-10-06). */
    private static final String [] kInactiveOnLegacy =
    {
        "fps_correct_enable",
        "fps_correct_list",
        "fps_correct_av1_offset",
        "fps_correct_av1_pkg",
    };

    private static boolean isInactiveLegacy (String key)
    {
        if (Control.isAtv14 ())
            return false;

        for (String k : kInactiveOnLegacy)
            if (k.equals (key))
                return true;

        return false;
    }

    private static boolean isInactive (String key)
    {
        if (!Control.isAtv14 ())
            return false;

        for (String k : kInactiveOnAtv14)
            if (k.equals (key))
                return true;

        return false;
    }

    private void markInapplicable (PreferenceGroup group)
    {
        for (int i = 0; i < group.getPreferenceCount (); i++)
        {
            Preference p = group.getPreference (i);
            if (p instanceof PreferenceGroup)
                markInapplicable ((PreferenceGroup)p);
            else if (isInactive (p.getKey ()))
                markInactive (p, R.string.not_applicable_atv14);
            else if (isInactiveLegacy (p.getKey ()))
                markInactive (p, R.string.not_applicable_legacy);
        }
    }

    private void markInactive (Preference p, int msgRes)
    {
        p.setEnabled (false);
        CharSequence s = p.getSummary ();
        p.setSummary ((s == null ? "" : s + " ") + getString (msgRes));
    }

    /* Настройки-списки приложений: ключ настройки → режим выбора. Значение
     * таких ключей — имена пакетов, их задают выбором из списка установленных
     * приложений, а не вводом с пульта. */
    private static final String [][] kAppsPrefs =
    {
        { "switch_skip_pkg", AppsPickerActivity.MODE_MULTI },
        { "fps_correct_av1_pkg", AppsPickerActivity.MODE_SINGLE },
    };

    private static final int REQ_APPS = 1001;

    /* Пояснения строк из XML: при пустом значении настройки возвращаем их на
     * место — Preference с пустой строкой скрывает summary вместе с пояснением. */
    private final Map<String, CharSequence> mAppsHints = new HashMap<> ();

    private static String appsMode (String key)
    {
        for (String [] item : kAppsPrefs)
            if (item [0].equals (key))
                return item [1];

        return AppsPickerActivity.MODE_MULTI;
    }

    private void setupAppsPrefs ()
    {
        for (String [] item : kAppsPrefs)
        {
            Preference p = findPreference (item [0]);
            if (!(p instanceof XAppsPreference))
                continue;

            final XAppsPreference pref = (XAppsPreference) p;
            mAppsHints.put (p.getKey (), p.getSummary ());
            pref.setPickListener (new XAppsPreference.OnPickListener ()
            {
                @Override
                public void onPickApps (XAppsPreference p)
                {
                    Intent in = new Intent (getActivity (), AppsPickerActivity.class);
                    in.putExtra (AppsPickerActivity.EXTRA_KEY, p.getKey ());
                    /* getTitle() — CharSequence, а extra объявлен строкой:
                     * нормализуем здесь, чтобы getStringExtra не зависел от
                     * того, вернёт ли ресурс String или Spanned. */
                    CharSequence title = p.getTitle ();
                    in.putExtra (AppsPickerActivity.EXTRA_TITLE,
                        title == null ? null : title.toString ());
                    in.putExtra (AppsPickerActivity.EXTRA_MODE, appsMode (p.getKey ()));
                    in.putExtra (AppsPickerActivity.EXTRA_VALUE, p.getApps ());
                    startActivityForResult (in, REQ_APPS);
                }
            });

            setAppsSummary (pref);
        }
    }

    private void setAppsSummary (XAppsPreference pref)
    {
        String names = Apps.names (getActivity (), pref.getApps ());
        CharSequence hint = mAppsHints.get (pref.getKey ());

        /* Пустой список — состояние по умолчанию: возвращаем пояснение из XML,
         * иначе Preference скрывает summary вместе с ним (пустая строка). */
        pref.setSummary (names.isEmpty () ? hint : names);
    }

    @Override
    public void onActivityResult (int requestCode, int resultCode, Intent data)
    {
        super.onActivityResult (requestCode, resultCode, data);

        if ((requestCode != REQ_APPS) || (resultCode != Activity.RESULT_OK) || (data == null))
            return;

        Preference p = findPreference (data.getStringExtra (AppsPickerActivity.EXTRA_KEY));
        if (!(p instanceof XAppsPreference))
            return;

        XAppsPreference pref = (XAppsPreference) p;
        /* notifyChanged() у Preference — protected, поэтому обновление вида
         * заказываем через setApps(..., true); setSummary() ниже уведомит
         * повторно, если текст строки изменился. */
        pref.setApps (data.getStringExtra (AppsPickerActivity.RESULT_VALUE), true);
        setAppsSummary (pref);

        /* Настройки применяются сразу (как у остальных строк) — демон
         * перечитывает конфиг по изменению файла. */
        if (mMain != null)
            mMain.applyPrefs ();
    }

    static SettingsFragment create ()
    {
        return new SettingsFragment ();
    }

    @Override
    public void onCreate (Bundle savedInstanceState)
    {
        super.onCreate (savedInstanceState);

        getPreferenceManager ().setSharedPreferencesName ("ini_options");

        // Load the preferences from an XML resource
        for (int t = 0; t < 2; t++)
            try
            {
                addPreferencesFromResource (R.xml.preferences);
                /* Сначала сводки списков приложений, затем пометки: иначе
                 * setAppsSummary() затрёт дописанную пометку. */
                setupAppsPrefs ();
                markInapplicable (getPreferenceScreen ());
                break;
            }
            catch (Exception ignored)
            {
                mMain.resetPrefs ();
            }
    }

    @Override
    public View onCreateView (LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState)
    {
        View root = inflater.inflate (R.layout.fragment_prefs, container, false);

        View b = root.findViewById (R.id.buttonPrefsReset);
        if (b != null)
            b.setOnClickListener (new View.OnClickListener ()
            {
                @Override
                public void onClick (View v)
                {
                    mMain.resetPrefs ();
                }
            });

        return root;
    }

    @Override
    public void onResume ()
    {
        super.onResume ();

        getPreferenceManager ().getSharedPreferences ()
            .registerOnSharedPreferenceChangeListener (this);
    }

    @Override
    public void onPause ()
    {
        super.onPause ();

        getPreferenceScreen ().getSharedPreferences ()
            .unregisterOnSharedPreferenceChangeListener (this);
    }

    @Override
    public void onAttach (Context context)
    {
        super.onAttach (context);
        if (context instanceof MainActivity)
            mMain = (MainActivity)context;
    }

    @Override
    public void onDetach ()
    {
        super.onDetach ();
        mMain = null;
    }

    @Override
    public void onSharedPreferenceChanged (SharedPreferences sharedPreferences, String key)
    {
        if (mMain != null)
            mMain.applyPrefs ();
    };

    @Override
    public boolean onBackPressed ()
    {
        if (mPrefStack.size () > 0)
        {
            int last = mPrefStack.size () - 1;
            PreferenceScreen ps = mPrefStack.elementAt (last);
            mPrefStack.removeElementAt (last);
            setPreferenceScreen (ps);
            return true;
        }

        return false;
    }

    @Override
    public boolean onPreferenceStartFragment (PreferenceFragment caller, Preference pref)
    {
        if (pref instanceof PreferenceScreen)
        {
            mPrefStack.add (caller.getPreferenceScreen ());
            caller.setPreferenceScreen ((PreferenceScreen) pref);
        }
        return false;
    }
}
