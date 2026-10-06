package ru.cobra.zap.xprefs;

import android.content.Context;
import android.preference.Preference;
import android.util.AttributeSet;

/**
 * Настройка, значение которой — имена пакетов приложений (switch.skip.pkg,
 * fps.correct.av1.pkg). Ввод текста с пульта для таких значений непригоден,
 * поэтому нажатие не открывает диалог, а передаётся хосту: он открывает
 * экран выбора приложений (см. gui.AppsPickerActivity).
 */
public class XAppsPreference extends Preference
{
    /** Хост открывает экран выбора для этой настройки. */
    public interface OnPickListener
    {
        void onPickApps (XAppsPreference pref);
    }

    private OnPickListener mListener;

    public XAppsPreference (Context context)
    {
        this (context, null);
    }

    public XAppsPreference (Context context, AttributeSet attrs)
    {
        super (context, attrs);
    }

    public void setPickListener (OnPickListener listener)
    {
        mListener = listener;
    }

    @Override
    protected void onClick ()
    {
        if (mListener != null)
            mListener.onPickApps (this);
    }

    /** Текущее значение в формате ini (пакеты через пробел). */
    public String getApps ()
    {
        return getPersistedString ("");
    }

    public void setApps (String value, boolean notify)
    {
        persistString (value == null ? "" : value);
        if (notify)
            notifyChanged ();
    }
}
