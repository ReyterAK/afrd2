package ru.cobra.zap.xprefs;

import android.content.Context;
import android.preference.Preference;
import android.util.AttributeSet;

/**
 * Настройка, значение которой — список частот вертикальной развёртки
 * (mode.blacklist.rates). Ввод текста с пульта для такого значения непригоден,
 * а угадать частоту по подсказке нельзя: демон сравнивает её с частотой режима
 * численно, и 23.976 с 24 — разные записи. Поэтому нажатие не открывает диалог
 * ввода, а передаётся хосту: он показывает выбор из частот, которые
 * поддерживает ТВ (см. gui.SettingsFragment и afrd.RateList).
 */
public class XRatesPreference extends Preference
{
    /** Хост показывает выбор частот для этой настройки. */
    public interface OnPickListener
    {
        void onPickRates (XRatesPreference pref);
    }

    private OnPickListener mListener;

    public XRatesPreference (Context context)
    {
        this (context, null);
    }

    public XRatesPreference (Context context, AttributeSet attrs)
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
            mListener.onPickRates (this);
    }

    /** Текущее значение в формате ini (частоты через пробел). */
    public String getRates ()
    {
        return getPersistedString ("");
    }

    public void setRates (String value, boolean notify)
    {
        persistString (value == null ? "" : value);
        if (notify)
            notifyChanged ();
    }
}
