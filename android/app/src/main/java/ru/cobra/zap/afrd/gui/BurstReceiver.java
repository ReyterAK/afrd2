/*
 * AFRd2: приёмник broadcast-команды сеанса «AFR по требованию».
 *
 * Для мапперов кнопок, которые умеют только «send broadcast» (Button Mapper):
 *   adb shell am broadcast -a ru.reyter.afrd2.action.BURST --ei ms 40000
 *   adb shell am broadcast -a ru.reyter.afrd2.action.BURST --ez cancel true
 *
 * Ничего не рисует и не запускает служб: отправляет команду демону и уходит.
 * Состояние сеанса демон рассылает сам (BURST_STATE), поэтому приёмник не
 * нужен ни для чего, кроме самой команды.
 */

package ru.cobra.zap.afrd.gui;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.widget.Toast;

import ru.cobra.zap.afrd.Burst;

public class BurstReceiver extends BroadcastReceiver
{
    @Override
    public void onReceive (Context context, Intent intent)
    {
        if (intent == null)
            return;

        int ms = intent.getIntExtra (Burst.EXTRA_MS, -1);
        if (intent.getBooleanExtra (Burst.EXTRA_CANCEL, false))
            ms = 0;

        /* Приёмник живёт до возврата из onReceive: без goAsync поток с сетевой
         * отправкой мог быть убит вместе с процессом. */
        final PendingResult pending = goAsync ();
        final Context ctx = context.getApplicationContext ();
        final int reqMs = ms;

        Burst.sendAsync (ms, new Burst.Callback ()
        {
            @Override
            public void onResult (String reply)
            {
                /* Тост — только для ручной проверки из shell: обычный маппер
                 * кнопок ничего не увидит, а прошивка рисует своё по
                 * BURST_STATE. */
                if (reply == null)
                    Toast.makeText (ctx, R.string.burst_no_daemon, Toast.LENGTH_LONG).show ();
                else if (reply.contains ("too-soon"))
                    Toast.makeText (ctx, R.string.burst_too_soon, Toast.LENGTH_SHORT).show ();
                else if (reqMs == 0)
                    Toast.makeText (ctx, R.string.burst_cancelled, Toast.LENGTH_SHORT).show ();
                else
                    Toast.makeText (ctx, R.string.burst_started, Toast.LENGTH_SHORT).show ();

                pending.finish ();
            }
        });
    }
}
