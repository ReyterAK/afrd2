/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd.gui;

import android.content.Context;
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

import java.util.Vector;

import ru.cobra.zap.afrd.Control;

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
            else if (!isInactive (p.getKey ()))
                continue;
            else
            {
                p.setEnabled (false);
                CharSequence s = p.getSummary ();
                p.setSummary ((s == null ? "" : s + " ") +
                              getString (R.string.not_applicable_atv14));
            }
        }
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
