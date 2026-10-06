/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd.gui;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.graphics.drawable.Drawable;
import android.os.AsyncTask;
import android.os.Bundle;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;

import ru.cobra.zap.afrd.Apps;
import ru.cobra.zap.afrd.PkgValues;
import ru.cobra.zap.afrd.jfun;

/**
 * Полноэкранный выбор приложений для настроек-списков пакетов.
 *
 * Экран ничего не пишет в конфиг: он возвращает новое значение хост-фрагменту
 * (SettingsFragment), а тот применяет его обычным путём (applyPrefs). Поэтому
 * экран переиспользуется любым числом настроек — сейчас это switch.skip.pkg
 * (режим multi), fps.correct.av1.pkg (режим single) и mode.min.hz.list
 * (режим pkg_value: у каждого выбранного плеера — своя частота-порог).
 */
public class AppsPickerActivity extends Activity
{
    public static final String EXTRA_KEY = "key";
    public static final String EXTRA_TITLE = "title";
    public static final String EXTRA_MODE = "mode";
    public static final String EXTRA_VALUE = "value";
    public static final String EXTRA_FILTER = "filter";
    public static final String RESULT_VALUE = "value";

    public static final String MODE_MULTI = "multi";
    public static final String MODE_SINGLE = "single";
    /** «пакет=число»: отметка задаёт пакет, диалог строки — число. */
    public static final String MODE_PKG_VALUE = "pkg_value";

    private String mKey;
    private String mMode = MODE_MULTI;
    private boolean mPlayersOnly = true;
    private final LinkedHashSet<String> mSelected = new LinkedHashSet<> ();
    /* Значения «пакет → число» для режима pkg_value (только отмеченные пакеты). */
    private final LinkedHashMap<String, String> mValues = new LinkedHashMap<> ();
    private final List<Apps.App> mApps = new ArrayList<> ();
    private AppsAdapter mAdapter;
    private AsyncTask<Void, Void, Void> mLoader;
    /* Счётчик поколений загрузки: быстрое переключение флажка фильтра отменяет
     * предыдущий AsyncTask, но runOnUiThread отменённой задачи может выполниться
     * последней — showList() отбрасывает устаревшее поколение, чтобы не показать
     * список, не соответствующий текущему состоянию флажка. */
    private int mLoadGen;

    @Override
    protected void onCreate (Bundle savedInstanceState)
    {
        super.onCreate (savedInstanceState);

        jfun.limitDpi (this);
        setContentView (R.layout.activity_apps_picker);

        Intent in = getIntent ();
        mKey = in.getStringExtra (EXTRA_KEY);
        String mode = in.getStringExtra (EXTRA_MODE);
        if (MODE_SINGLE.equals (mode) || MODE_PKG_VALUE.equals (mode))
            mMode = mode;
        mPlayersOnly = !"all".equals (in.getStringExtra (EXTRA_FILTER));

        String value = in.getStringExtra (EXTRA_VALUE);
        if (MODE_PKG_VALUE.equals (mMode))
        {
            /* Разбор — теми же правилами, что у демона (PkgValues): то, что он
             * игнорирует, в значение не сохраняем — иначе строка настроек
             * показывала бы настройку, которой демон не видит. */
            mValues.putAll (PkgValues.parse (value, PkgValues.MIN_HZ, PkgValues.MAX_HZ));
            mSelected.addAll (mValues.keySet ());
        }
        else
            mSelected.addAll (Apps.split (value));

        String title = in.getStringExtra (EXTRA_TITLE);
        ((TextView) findViewById (R.id.pickerTitle)).setText (title == null ? "" : title);

        CheckBox players = findViewById (R.id.pickerPlayers);
        players.setChecked (mPlayersOnly);
        players.setOnCheckedChangeListener (new CompoundButton.OnCheckedChangeListener ()
        {
            @Override
            public void onCheckedChanged (CompoundButton button, boolean checked)
            {
                if (mPlayersOnly == checked)
                    return;

                mPlayersOnly = checked;
                load ();
            }
        });

        findViewById (R.id.pickerDone).setOnClickListener (new View.OnClickListener ()
        {
            @Override
            public void onClick (View v)
            {
                finishWithResult ();
            }
        });

        ListView list = findViewById (R.id.pickerList);
        mAdapter = new AppsAdapter (this, mApps, mSelected, mValues,
            MODE_PKG_VALUE.equals (mMode));
        list.setAdapter (mAdapter);
        list.setOnItemClickListener (new AdapterView.OnItemClickListener ()
        {
            @Override
            public void onItemClick (AdapterView<?> parent, View view, int position, long id)
            {
                String pkg = mApps.get (position).pkg;
                /* В режиме pkg_value отметка и число задаются одним диалогом:
                 * на пульте клик — единственное доступное действие, а без
                 * числа запись для демона бесполезна. */
                if (MODE_PKG_VALUE.equals (mMode))
                {
                    editValue (pkg);
                    return;
                }

                /* wasSelected нужен до clear(): в режиме single очистка набора обнулила бы
                 * признак, и выбранную строку нельзя было бы снять (пустое значение =
                 * «любой плеер»). В режиме multi снятие отметки — обычный путь. */
                boolean wasSelected = mSelected.contains (pkg);
                if (MODE_SINGLE.equals (mMode))
                    mSelected.clear ();
                if (wasSelected)
                    mSelected.remove (pkg);
                else
                    mSelected.add (pkg);

                mAdapter.notifyDataSetChanged ();
            }
        });

        load ();
    }

    /* Диалог числа для строки списка (режим pkg_value): имя пакета задаётся
     * отметкой в списке, число вводится с пульта. Кнопка сохранения гаснет,
     * пока число недопустимо (не число или вне диапазона, который примет
     * демон), — иначе «Сохранить» молча теряло бы запись. */
    private void editValue (final String pkg)
    {
        final boolean selected = mSelected.contains (pkg);
        final EditText input = new EditText (this);
        input.setInputType (InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL);
        input.setText (mValues.containsKey (pkg) ? mValues.get (pkg) : "");
        input.setHint (R.string.apps_value_hint);

        float d = getResources ().getDisplayMetrics ().density;
        int pad = (int) (16 * d);
        /* setView с отступами есть не во всех compileSdk (в 27 — только
         * setView(View)), поэтому поле оборачивается в контейнер с padding. */
        FrameLayout box = new FrameLayout (this);
        box.setPadding (pad, pad / 2, pad, pad / 2);
        box.addView (input);

        AlertDialog.Builder b = new AlertDialog.Builder (this)
            .setTitle (getString (R.string.apps_value_title, Apps.label (this, pkg)))
            .setView (box)
            .setNegativeButton (android.R.string.cancel, null)
            .setPositiveButton (R.string.apps_value_save, new DialogInterface.OnClickListener ()
            {
                @Override
                public void onClick (DialogInterface dialog, int which)
                {
                    applyValue (pkg, selected, input.getText ().toString ().trim ());
                }
            });

        if (selected)
            b.setNeutralButton (R.string.apps_value_clear, new DialogInterface.OnClickListener ()
            {
                @Override
                public void onClick (DialogInterface dialog, int which)
                {
                    mSelected.remove (pkg);
                    mValues.remove (pkg);
                    mAdapter.notifyDataSetChanged ();
                }
            });

        final AlertDialog dialog = b.create ();
        dialog.setOnShowListener (new DialogInterface.OnShowListener ()
        {
            @Override
            public void onShow (DialogInterface d)
            {
                if (!(d instanceof AlertDialog))
                    return;

                final Button ok = ((AlertDialog) d).getButton (AlertDialog.BUTTON_POSITIVE);
                ok.setEnabled (PkgValues.inRange (input.getText ().toString (),
                    PkgValues.MIN_HZ, PkgValues.MAX_HZ));

                input.addTextChangedListener (new TextWatcher ()
                {
                    @Override
                    public void beforeTextChanged (CharSequence s, int a, int b, int c)
                    {
                    }

                    @Override
                    public void onTextChanged (CharSequence s, int a, int b, int c)
                    {
                    }

                    @Override
                    public void afterTextChanged (Editable s)
                    {
                        ok.setEnabled (PkgValues.inRange (s.toString (),
                            PkgValues.MIN_HZ, PkgValues.MAX_HZ));
                    }
                });
            }
        });
        dialog.show ();
    }

    /* Запись значения в набор: новую строку отмечаем, только если демон ещё
     * примет запись (иначе он молча выбросит лишние — afrd.c:
     * MODE_MIN_HZ_MAX, и настройка работала бы не так, как показано). Считаем
     * заполненные записи: запись без числа демон не хранит, слот она не занимает. */
    private void applyValue (String pkg, boolean selected, String value)
    {
        if (!selected && (PkgValues.filledCount (mValues) >= PkgValues.MAX_ENTRIES))
        {
            Toast.makeText (this, getString (R.string.apps_value_limit, PkgValues.MAX_ENTRIES),
                Toast.LENGTH_LONG).show ();
            return;
        }

        mValues.put (pkg, value);
        mSelected.add (pkg);
        mAdapter.notifyDataSetChanged ();
    }

    /* Загрузка списка — в фоне: на ATV это десятки-сотни пакетов, а запрос
     * списка и загрузка имён идут через PackageManager. */
    private void load ()
    {
        if (mLoader != null)
            mLoader.cancel (true);

        final int gen = ++mLoadGen;

        mLoader = new AsyncTask<Void, Void, Void> ()
        {
            @Override
            protected Void doInBackground (Void... args)
            {
                final List<Apps.App> list = mPlayersOnly
                    ? Apps.videoPlayers (AppsPickerActivity.this)
                    : Apps.allApps (AppsPickerActivity.this);

                runOnUiThread (new Runnable ()
                {
                    @Override
                    public void run ()
                    {
                        showList (gen, list);
                    }
                });
                return null;
            }
        }.execute ();
    }

    private void showList (int gen, List<Apps.App> list)
    {
        /* Устаревшее поколение (пользователь успел переключить фильтр) или
         * закрывающийся экран — список не показываем. */
        if (isFinishing () || (gen != mLoadGen))
            return;

        /* Уже выбранные пакеты показываем всегда, даже если фильтр их отсеял:
         * приложение могло перестать быть плеером или быть удалено, но снять
         * отметку пользователь должен иметь возможность. */
        List<String> selected = new ArrayList<> (mSelected);
        for (String pkg : selected)
            if (Apps.indexOf (list, pkg) < 0)
                list.add (Apps.app (this, pkg));

        mApps.clear ();
        mApps.addAll (list);
        mAdapter.notifyDataSetChanged ();

        findViewById (R.id.pickerEmpty).setVisibility (mApps.isEmpty () ? View.VISIBLE : View.GONE);
    }

    /* На пульте Back ожидаемо означает «готово»: отдельного «отменить» у нас
     * нет, а потерять выбор хуже, чем сохранить его без подтверждения. */
    @Override
    public void onBackPressed ()
    {
        finishWithResult ();
    }

    private void finishWithResult ()
    {
        Intent out = new Intent ();
        out.putExtra (EXTRA_KEY, mKey);
        if (MODE_SINGLE.equals (mMode))
            out.putExtra (RESULT_VALUE, mSelected.isEmpty () ? "" : mSelected.iterator ().next ());
        else if (MODE_PKG_VALUE.equals (mMode))
            out.putExtra (RESULT_VALUE, PkgValues.format (mValues));
        else
            out.putExtra (RESULT_VALUE, Apps.join (new ArrayList<> (mSelected)));

        setResult (RESULT_OK, out);
        finish ();
    }

    private static class AppsAdapter extends BaseAdapter
    {
        private final Activity mCtx;
        private final List<Apps.App> mApps;
        private final LinkedHashSet<String> mSelected;
        private final Map<String, String> mValues;
        private final boolean mShowValues;
        private final Map<String, Drawable> mIcons = new HashMap<> ();

        AppsAdapter (Activity ctx, List<Apps.App> apps, LinkedHashSet<String> selected,
                     Map<String, String> values, boolean showValues)
        {
            mCtx = ctx;
            mApps = apps;
            mSelected = selected;
            mValues = values;
            mShowValues = showValues;
        }

        @Override
        public int getCount ()
        {
            return mApps.size ();
        }

        @Override
        public Object getItem (int position)
        {
            return mApps.get (position);
        }

        @Override
        public long getItemId (int position)
        {
            return position;
        }

        @Override
        public View getView (int position, View convertView, ViewGroup parent)
        {
            View v = convertView;
            if (v == null)
                v = mCtx.getLayoutInflater ().inflate (R.layout.item_app, parent, false);

            Apps.App app = mApps.get (position);

            ((TextView) v.findViewById (R.id.appLabel)).setText (app.label);
            ((TextView) v.findViewById (R.id.appPkg)).setText (app.installed ? app.pkg
                : mCtx.getString (R.string.apps_not_installed, app.pkg));
            ((CheckBox) v.findViewById (R.id.appCheck)).setChecked (mSelected.contains (app.pkg));

            /* В режиме pkg_value справа от пакета видно заданное число: иначе
             * пустая рамка отметки не отличалась бы от отмеченной строки без
             * значения. */
            TextView val = v.findViewById (R.id.appValue);
            String num = mValues.get (app.pkg);
            if (mShowValues && (num != null) && !num.isEmpty ())
            {
                val.setVisibility (View.VISIBLE);
                val.setText (mCtx.getString (R.string.apps_value_hz, num));
            }
            else
                val.setVisibility (View.GONE);

            ImageView iv = v.findViewById (R.id.appIcon);
            Drawable icon = mIcons.get (app.pkg);
            if (icon == null)
            {
                icon = Apps.icon (mCtx, app.pkg);
                if (icon == null)
                    icon = mCtx.getResources ().getDrawable (android.R.drawable.sym_def_app_icon,
                        mCtx.getTheme ());
                mIcons.put (app.pkg, icon);
            }
            iv.setImageDrawable (icon);

            return v;
        }
    }
}
