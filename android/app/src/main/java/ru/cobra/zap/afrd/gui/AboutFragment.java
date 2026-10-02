/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd.gui;

import android.app.Fragment;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

public class AboutFragment extends Fragment
{
    private TextView mLicenseText;
    private LinearLayout mAboutBottom;
    private Button mShowAll;
    private boolean mLicenseDisplayed = false;

    public static AboutFragment create ()
    {
        return new AboutFragment ();
    }

    @Override
    public View onCreateView (LayoutInflater inflater, ViewGroup container,
        Bundle savedInstanceState)
    {
        View root = inflater.inflate (R.layout.fragment_about, container, false);

        TextView whatsnew = root.findViewById (R.id.whatsnew);
        whatsnew.setText (readWhatsnew (R.raw.changelog));

        mLicenseText = root.findViewById (R.id.license);
        mAboutBottom = root.findViewById (R.id.about_bottom);

        mShowAll = root.findViewById (R.id.butLicense);
        mShowAll.setOnClickListener (new View.OnClickListener ()
        {
            @Override
            public void onClick (View v)
            {
                toggleLicense ();
            }
        });

        mLicenseText.setOnClickListener (new View.OnClickListener ()
        {
            @Override
            public void onClick (View v)
            {
                toggleLicense ();
            }
        });

        return root;
    }

    private void toggleLicense ()
    {
        mLicenseDisplayed = !mLicenseDisplayed;
        if (mLicenseDisplayed)
        {
            mLicenseText.setText (readLicense (R.raw.copying));
            mShowAll.setText (R.string.about_hidelic);
            mAboutBottom.setVisibility (View.GONE);
        }
        else
        {
            mLicenseText.setText (getString (R.string.about_gpl3));
            mShowAll.setText (R.string.about_showlic);
            mAboutBottom.setVisibility (View.VISIBLE);
        }
    }

    private String readFile (int resid)
    {
        try
        {
            InputStream is = getResources ().openRawResource (resid);
            byte[] buff = new byte[is.available ()];
            if (is.read (buff) < 0)
                return "";

            return new String (buff, StandardCharsets.UTF_8);
        }
        catch (IOException ignored)
        {
        }

        return "";
    }

    private String readWhatsnew (int resid)
    {
        String ret = readFile (resid);
        // Remove newlines not followed by another newline
        return ret.replaceAll (" *([^\n])\n +", "$1 ");
    }

    private String readLicense (int resid)
    {
        String ret = readFile (resid);
        // Remove newlines not followed by a line starting with space
        return ret.replaceAll ("(.+) *\n([^ \t\n])", "$1 $2");
    }

}
