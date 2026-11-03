/*
 * AFRd2: Activity для сеанса «AFR по требованию».
 *
 * Вызывается маппером кнопок (tvQuickActions и подобные) как
 *   am start -n ru.cobra.zap.afrd.gui/ru.cobra.zap.afrd.gui.BurstActivity --ei ms 40000
 * либо intent-фильтром ru.cobra.afrd.BURST (совместимо с уже существующими
 * START/STOP). Своё окно не показывает: только тост с результатом.
 */

package ru.cobra.zap.afrd.gui;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.widget.Toast;

import ru.cobra.zap.afrd.Burst;

public class BurstActivity extends Activity
{
    static final String INTENT_BURST = "ru.cobra.afrd.BURST";

    @Override
    protected void onCreate (Bundle savedInstanceState)
    {
        super.onCreate (savedInstanceState);

        Intent intent = getIntent ();
        String action = intent.getAction ();

        /* ms: 0 — отмена; нет extra — умолчание демона (burst.ms). */
        int ms = -1;
        boolean cancel = false;

        if (INTENT_BURST.equals (action) || intent.hasExtra (Burst.EXTRA_MS))
            ms = intent.getIntExtra (Burst.EXTRA_MS, -1);
        if (intent.getBooleanExtra (Burst.EXTRA_CANCEL, false))
        {
            ms = 0;
            cancel = true;
        }

        if (action == null && !intent.hasExtra (Burst.EXTRA_MS) && !cancel)
        {
            finishAndRemoveTask ();
            return;
        }

        /* Сеть на главном потоке запрещена — отправляем из отдельного потока.
         * Само окно не нужно: закрываемся сразу (тема NoDisplay требует
         * finish() ещё в onCreate), а тост показываем от контекста приложения,
         * когда придёт ответ. */
        final boolean wasCancel = cancel;
        final Context app = getApplicationContext ();
        Burst.sendAsync (ms, new Burst.Callback ()
        {
            @Override
            public void onResult (String reply)
            {
                if (reply == null)
                    Toast.makeText (app, R.string.burst_no_daemon, Toast.LENGTH_LONG).show ();
                else if (reply.contains ("too-soon"))
                    /* Демон отвечает result:too-soon, если сеанс просили чаще
                     * раза в секунду: раньше показывали «сеанс запущен»
                     * (нашло ревью GUI). */
                    Toast.makeText (app, R.string.burst_too_soon, Toast.LENGTH_SHORT).show ();
                else if (wasCancel)
                    Toast.makeText (app, R.string.burst_cancelled, Toast.LENGTH_SHORT).show ();
                else
                    Toast.makeText (app, R.string.burst_started, Toast.LENGTH_SHORT).show ();
            }
        });

        finishAndRemoveTask ();
    }
}
