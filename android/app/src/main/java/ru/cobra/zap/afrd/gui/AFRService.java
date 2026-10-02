/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd.gui;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.drawable.Icon;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.SystemClock;
import android.util.Log;
import android.widget.Toast;

import java.util.Locale;

import ru.cobra.zap.afrd.Control;
import ru.cobra.zap.afrd.Status;

public class AFRService extends Service
{
    public static final int NOTIF_STATUS_ID = 1;
    public static final String NOTIF_CHANNEL = "AFRd";
    private Handler mTimer = new Handler ();
    private Handler mHzTimer = new Handler ();
    private Control mControl;
    private Status mStatus = new Status ();
    private int mOldRefreshRate = -1;
    private boolean mOldBlackScreen = false;
    private SharedPreferences mOptions;
    private int mNotificationMask = -1;
    private boolean mFirstRun = true;
    /* R57: метка последнего перезапуска демона — чтобы сторож не устроил
     * шторм `su`, пока демон ещё поднимается после перезапуска */
    private long mLastRestart = 0;
    /* R57: сколько подряд обновлений статуса не меняли shmem (при живом демоне
     * heartbeat меняет штамп на каждой итерации) */
    private int mStaleTicks = 0;
    /* heartbeat хотя бы раз продвинулся — только тогда застой считается смертью
     * (иначе старый демон без heartbeat зациклил бы перезапуски) */
    private boolean mHeartbeatSeen = false;
    private static final int STALE_TICKS_MAX = 4;   /* 4 * 3 с = 12 с */

    @Override
    public void onCreate ()
    {
        super.onCreate ();

        mControl = new Control (this);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
        {
            NotificationManager notificationManager =
                (NotificationManager) getSystemService (Context.NOTIFICATION_SERVICE);
            if (notificationManager != null)
            {
                NotificationChannel notificationChannel = new NotificationChannel (
                    NOTIF_CHANNEL, getString (R.string.notif_channel), NotificationManager.IMPORTANCE_MIN);
                notificationManager.createNotificationChannel (notificationChannel);
            }
        }

        mOptions = getSharedPreferences ("ini_options", 0);
        updateAll ();

        mTimer.post (new Runnable ()
        {
            @Override
            public void run ()
            {
                updateAll ();
                mTimer.postDelayed (this, 3000);
            }
        });
    }

    private void updateAll ()
    {
        if (!mStatus.ok ())
            mStatus.open ();
        boolean changed = mStatus.refresh ();

        /* R57: считаем подряд идущие обновления, не менявшие shmem. Живой
         * демон на ATV14 обновляет heartbeat каждый цикл, поэтому застой
         * штампа = демон мёртв (проверяется в checkDaemon). */
        if (mStatus.ok ())
        {
            if (changed)
            {
                mStaleTicks = 0;
                mHeartbeatSeen = true;
            }
            else
                mStaleTicks++;
        }
        else
            mStaleTicks = 0;

        checkDaemon ();
        checkToast ();
        updateNotification (changed);
    }

    private void checkToast ()
    {
        if (!mStatus.ok ())
            return;

        if ((mOldRefreshRate == mStatus.mCurrentHz) && (mOldBlackScreen == mStatus.mBlackened))
            return;

        // skip the first toast as it means afrd was launched
        boolean firstTime = (mOldRefreshRate == -1);
        mOldRefreshRate = mStatus.mCurrentHz;
        mOldBlackScreen = mStatus.mBlackened;

        if (firstTime ||
            mStatus.mBlackened ||
            !mOptions.getBoolean ("toast_hz", true))
            return;

        mHzTimer.postDelayed (new Runnable ()
        {
            final int current_hz = mStatus.mCurrentHz;

            @Override
            public void run ()
            {
                Toast.makeText (getApplicationContext (),
                    Status.hz2str (getResources (), current_hz),
                    Toast.LENGTH_LONG).show ();
            }
        }, 1000);
    }

    @SuppressWarnings ("deprecation")
    private void updateNotification (boolean changed)
    {
        // Compute a mask of what's going to be displayed
        int nm = 0;

        if (mStatus.ok ())
        {
            nm = 1;
            if (mStatus.mEnabled)
                nm |= 2;

            nm ^= (mStatus.mCurrentHz << 8) ^ (mStatus.mOriginalHz << 8);
        }

        // If nothing changed, don't update the notification
        if ((mNotificationMask == nm) && !changed)
            return;

        mNotificationMask = nm;

        // When user presses the notification, the main activity shows up
        Intent runApp = new Intent (this, MainActivity.class);
        runApp.setFlags (Intent.FLAG_ACTIVITY_SINGLE_TOP);

        Notification.Builder nbuilder;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            nbuilder = new Notification.Builder (this, NOTIF_CHANNEL);
        else
            nbuilder = new Notification.Builder (this);

        nbuilder
            .setSmallIcon (R.drawable.ic_afrd)
            .setLargeIcon (Icon.createWithResource (this, R.drawable.ic_afrd))
            .setContentIntent (PendingIntent.getActivity (this, 0, runApp, 0))
            .setOngoing (true)
            .setOnlyAlertOnce (true)
            .setLocalOnly (true)
            .setCategory (Notification.CATEGORY_SERVICE);

        if (mStatus.ok ())
        {
            Intent startStop = new Intent (mStatus.mEnabled ?
                ActionActivity.INTENT_STOP : ActionActivity.INTENT_START);
            startStop.setFlags (Intent.FLAG_ACTIVITY_NO_HISTORY |
                Intent.FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS |
                Intent.FLAG_ACTIVITY_NO_ANIMATION);

            Notification.Action action = new Notification.Action.Builder (
                Icon.createWithResource (this, mStatus.mEnabled ? R.drawable.ic_stop : R.drawable.ic_play),
                getString (mStatus.mEnabled ? R.string.notif_stop_afrd : R.string.notif_start_afrd),
                PendingIntent.getActivity (this, 0, startStop, 0)).build ();
            nbuilder
                .addAction (action)
                .setContentText (getResources ().getString (R.string.notif_hz,
                    Status.hz2str (getResources (), mStatus.mCurrentHz),
                    Status.hz2str (getResources (), mStatus.mOriginalHz)))
                .setContentTitle (getString (mStatus.mEnabled ?
                    R.string.notif_enabled : R.string.notif_disabled));
        }
        else
            nbuilder
                .setContentTitle (getString (R.string.notif_notrunning));

        startForeground (NOTIF_STATUS_ID, nbuilder.build ());
    }

    @Override
    public IBinder onBind (Intent intent)
    {
        // TODO: Return the communication channel to the service.
        throw new UnsupportedOperationException ("Not yet implemented");
    }

    /* Перезапуск демона с паузой между попытками (демон поднимается не мгновенно). */
    private void restartDaemon (String why)
    {
        long now = SystemClock.elapsedRealtime ();
        if (now - mLastRestart < 10000)
            return;

        mLastRestart = now;
        mFirstRun = false;
        mStaleTicks = 0;
        mHeartbeatSeen = false;
        Log.i ("afrd", why);
        mStatus.close ();
        mControl.restart ();
    }

    private void checkDaemon ()
    {
        /* R57: на ATV14 демон держит флаг backdoor SurfaceFlinger; если он
         * умер, не сняв флаг (kill -9, паника ядра), приложения со своим AFR
         * перестают менять режим. Процессы root приложению не видны (/proc
         * смонтирован с hidepid=invisible), поэтому судим по «сердцебиению»
         * в общей памяти: демон обновляет его каждый цикл, и CRC-штамп
         * перестаёт меняться, когда он мёртв (файл shmem при этом остаётся
         * на диске, так что mStatus.ok() смерть не замечает). */
        if (Control.isAtv14 () && mHeartbeatSeen && mStatus.ok () &&
            mStaleTicks >= STALE_TICKS_MAX)
        {
            restartDaemon ("Daemon heartbeat stopped - restarting");
            return;
        }

        if (mStatus.ok ())
        {
            /* mStatus.mVersion может быть null: shmem уже отображён (ok()),
             * а версия ещё не разобрана (первый запуск после загрузки) — при
             * сравнении через .equals это был NPE и падение AFRService */
            if (BuildConfig.VERSION_NAME.equals (mStatus.mVersion))
                return;

            Log.i ("afrd", String.format (Locale.getDefault (),
                "Restarting daemon because of wrong version (%s, expected %s)",
                mStatus.mVersion, BuildConfig.VERSION_NAME));
        }
        else
        {
            // wait until failure detector triggers, then restart daemon
            if (!Status.mFail.giveUp () && !mFirstRun)
                return;

            if (!mFirstRun)
                Log.i ("afrd", "(re-)Starting afrd because failed to get daemon status");
        }

        mFirstRun = false;
        mStatus.close ();
        mControl.restart ();
    }
}