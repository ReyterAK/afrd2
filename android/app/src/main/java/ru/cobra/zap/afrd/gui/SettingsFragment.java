/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd.gui;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.AsyncTask;
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
import android.widget.Toast;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Vector;

import ru.cobra.zap.afrd.Apps;
import ru.cobra.zap.afrd.Control;
import ru.cobra.zap.afrd.CsPolicy;
import ru.cobra.zap.afrd.PkgValues;
import ru.cobra.zap.afrd.RateList;
import ru.cobra.zap.afrd.TvModes;
import ru.cobra.zap.xprefs.XAppsPreference;
import ru.cobra.zap.xprefs.XListPreference;
import ru.cobra.zap.xprefs.XRatesPreference;

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
     * впечатление работающей настройки.
     * cs.select здесь нет: правила цветового пространства применяются и на
     * ATV14 (через вендорский сервис, см. afrd.c: atv14_apply_mode).
     * switch.delay.off здесь нет: ключ действует на обоих путях, но значит
     * разное — на ATV14 это предел терпения к паузе (0 — держать режим до её
     * конца), а на старом пути — задержка возврата после остановки видео.
     * Согласованность списка с cfgkeys.c проверяет tests/test_inactive.c. */
    private static final String [] kInactiveOnAtv14 =
    {
        "mode_extra",
        "switch_delay_on",
        "switch_delay_retry",
        "switch_blackout",
        "switch_ignore",
        "switch_hdmi",
        "vdec_blacklist",
        "frhint_vdec_blacklist",
    };

    /* Настройки, которые демон объявляет ключами нового пути (CFGKEY_ATV14) и
     * потому на Android 6-9 не читает. Список должен покрывать ВСЕ такие ключи,
     * видимые в GUI: иначе пользователь старой прошивки видит рабочие на вид
     * настройки, которых демон не касается.
     * switch.skip.pkg здесь нет: он действует на обоих путях (перенос 2026-10-06). */
    private static final String [] kInactiveOnLegacy =
    {
        /* триггер и окно замера */
        "trigger_poll_ms",
        "trigger_start_debounce_ms",
        "trigger_stop_debounce_ms",
        "fps_measure_envelope_ms",
        "fps_measure_envelope_early_ms",
        "fps_measure_envelope_max_ms",
        "fps_measure_envelope_sub_ms",
        "fps_measure_envelope_skip_ms",
        "fps_measure_retries",
        "fps_declared_crosscheck_pct",
        "fps_recheck_delay_ms",
        "fps_cadence_fix_ms",
        "fps_prefer_api",
        "fps_declare_fast",
        "pause_keep",
        "switch_restore_on_stop",
        /* калибровка по плееру */
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
        { "mode_min_hz_list", AppsPickerActivity.MODE_PKG_VALUE },
        { "fps_correct_list", AppsPickerActivity.MODE_PKG_PERCENT },
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
        String value = pref.getApps ();
        CharSequence hint = mAppsHints.get (pref.getKey ());
        String mode = appsMode (pref.getKey ());
        String names = (AppsPickerActivity.MODE_PKG_VALUE.equals (mode) ||
                        AppsPickerActivity.MODE_PKG_PERCENT.equals (mode))
            ? valueSummary (mode, value)
            : Apps.names (getActivity (), value);

        /* Пустой список — состояние по умолчанию: возвращаем пояснение из XML,
         * иначе Preference скрывает summary вместе с ним (пустая строка). */
        pref.setSummary (names.isEmpty () ? hint : names);
    }

    /* Значение «пакет=число» в виде «VLC = 50 Гц, Just Player = 59.94 Гц»:
     * имя пакета пользователю бесполезно, а Apps.names() на таком значении
     * ищет метку для всей строки «пакет=число» и находит только её саму.
     * Единица зависит от настройки: у mode.min.hz.list герцы, у fps.correct.list
     * проценты поправки. */
    private String valueSummary (String mode, String value)
    {
        boolean pct = AppsPickerActivity.MODE_PKG_PERCENT.equals (mode);
        int unitRes = pct ? R.string.apps_value_percent : R.string.apps_value_hz;

        StringBuilder sb = new StringBuilder ();
        Map<String, String> entries = pct ? PkgValues.parsePercent (value)
                                          : PkgValues.parse (value, PkgValues.MIN_HZ, PkgValues.MAX_HZ);
        for (Map.Entry<String, String> e : entries.entrySet ())
        {
            if (sb.length () > 0)
                sb.append (", ");
            sb.append (Apps.label (getActivity (), e.getKey ()));
            if (!e.getValue ().isEmpty ())
                sb.append (" = ").append (getString (unitRes, e.getValue ()));
        }

        return sb.toString ();
    }

    /* Настройка «Запрещённые частоты развёртки»: значение — частоты через
     * пробел (mode.blacklist.rates), выбираются из частот, которые поддерживает
     * ТВ. Ввод текста тут был бы ловушкой: демон сравнивает частоту запрета с
     * частотой режима численно (допуск ±1 в 1/256 Гц), поэтому 23.976 и 24 —
     * разные записи, и угадать нужную из подсказки нельзя. */
    private final Map<String, CharSequence> mRatesHint = new HashMap<> ();

    private void setupRatesPref ()
    {
        Preference p = findPreference ("mode_blacklist_rates");
        if (!(p instanceof XRatesPreference))
            return;

        final XRatesPreference pref = (XRatesPreference) p;
        mRatesHint.put (pref.getKey (), pref.getSummary ());
        pref.setPickListener (new XRatesPreference.OnPickListener ()
        {
            @Override
            public void onPickRates (XRatesPreference p)
            {
                readRatesAndPick (p);
            }
        });

        setRatesSummary (pref);
    }

    private void setRatesSummary (XRatesPreference pref)
    {
        List<String> rates = RateList.parse (pref.getRates ());
        StringBuilder sb = new StringBuilder ();
        for (String r : rates)
        {
            if (sb.length () > 0)
                sb.append (", ");
            sb.append (r);
        }

        CharSequence hint = mRatesHint.get (pref.getKey ());
        /* Пустой список — состояние по умолчанию: возвращаем пояснение из XML,
         * иначе Preference скрывает summary вместе с ним. */
        pref.setSummary (rates.isEmpty () ? hint
            : getString (R.string.rates_summary, sb.toString ()));
    }

    /* Список частот берём у самого бокса (режимы ТВ): запрет частоты, которой
     * на этом ТВ нет, ничего не отсекает. Чтение узла EDID и дампа
     * SurfaceFlinger идёт под su, поэтому — в фоне. */
    private void readRatesAndPick (final XRatesPreference pref)
    {
        new AsyncTask<Void, Void, String> ()
        {
            @Override
            protected String doInBackground (Void... args)
            {
                return TvModes.read ();
            }

            @Override
            protected void onPostExecute (String text)
            {
                showRatesDialog (pref, text);
            }
        }.execute ();
    }

    private void showRatesDialog (final XRatesPreference pref, String text)
    {
        if (!isAdded () || (getActivity () == null))
            return;

        List<String> tv = RateList.fromSources (text);
        boolean known = !tv.isEmpty ();
        if (!known)
            tv = Arrays.asList (RateList.CANON);

        final List<String> items = RateList.displayList (tv, pref.getRates ());
        final boolean [] checked = new boolean [items.size ()];
        List<String> selected = RateList.parse (pref.getRates ());
        for (int i = 0; i < items.size (); i++)
            checked [i] = selected.contains (items.get (i));

        AlertDialog.Builder b = new AlertDialog.Builder (getActivity ())
            .setTitle (R.string.rates_title)
            .setMultiChoiceItems (items.toArray (new CharSequence [0]), checked,
                new DialogInterface.OnMultiChoiceClickListener ()
                {
                    @Override
                    public void onClick (DialogInterface dialog, int which, boolean isChecked)
                    {
                        checked [which] = isChecked;
                    }
                })
            .setPositiveButton (android.R.string.ok, new DialogInterface.OnClickListener ()
            {
                @Override
                public void onClick (DialogInterface dialog, int which)
                {
                    List<String> sel = new ArrayList<> ();
                    for (int i = 0; i < items.size (); i++)
                        if (checked [i])
                            sel.add (items.get (i));

                    /* Демон хранит десять частот: лишние он выбрасывает молча,
                     * поэтому хвост отрезаем здесь и говорим об этом. */
                    List<String> kept = RateList.trim (sel);
                    int dropped = sel.size () - kept.size ();
                    if (dropped > 0)
                        Toast.makeText (getActivity (), getString (R.string.rates_truncated,
                            dropped, RateList.MAX_ENTRIES), Toast.LENGTH_LONG).show ();

                    pref.setRates (RateList.format (kept), true);
                    setRatesSummary (pref);

                    /* Как у остальных настроек: применяем сразу (демон
                     * перечитывает конфиг по изменению файла). */
                    if (mMain != null)
                        mMain.applyPrefs ();
                }
            })
            .setNegativeButton (android.R.string.cancel, null);

        if (!known)
            b.setMessage (R.string.rates_no_tv_list);

        b.show ();
    }

    /* Подписи пунктов «Формат HDMI» — по порядку значений CsPolicy.VALUES. */
    private static final int [] kCsLabels =
    {
        R.string.cs_policy_auto, R.string.cs_policy_rgb, R.string.cs_policy_yuv444,
    };

    /* Настройка «Формат HDMI»: пользователь выбирает политику, а в cs.select
     * уходит правило (значения — CsPolicy.VALUES, те же, что в arrays.xml).
     * Раньше это было текстовое поле с регулярными выражениями: чтобы поменять
     * формат, надо было знать и синтаксис правил, и то, что правило защиты
     * полосы 4K трогать нельзя. */
    private void setupCsPref ()
    {
        Preference p = findPreference ("cs_select");
        if (!(p instanceof XListPreference))
            return;

        XListPreference pref = (XListPreference) p;
        String value = pref.getValue ();
        int idx = CsPolicy.indexOf (value);

        List<CharSequence> entries = new ArrayList<> ();
        List<CharSequence> values = new ArrayList<> ();
        for (int i = 0; i < CsPolicy.VALUES.length; i++)
        {
            entries.add (getString (kCsLabels [i]));
            values.add (CsPolicy.VALUES [i]);
        }

        /* Значение не из списка (правка ini руками или текст, оставшийся от
         * прежней версии с полем ввода) показываем отдельным пунктом: иначе
         * строка настроек выглядела бы пустой, а выбор молча затирал бы правку. */
        if ((idx < 0) && (value != null) && !value.isEmpty ())
        {
            entries.add (getString (R.string.cs_policy_other, value));
            values.add (value);
        }

        pref.setEntries (entries.toArray (new CharSequence [0]));
        pref.setEntryValues (values.toArray (new CharSequence [0]));
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
                /* Сначала сводки и списки выбора, затем пометки: иначе
                 * setAppsSummary()/setRatesSummary() затрут дописанную
                 * пометку о недействующей настройке. */
                setupAppsPrefs ();
                setupRatesPref ();
                setupCsPref ();
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
