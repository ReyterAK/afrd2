/*
 * AFR burst: запуск сеанса «AFR по требованию» из списка приложений.
 *
 * Ничего не рисует и не держит: отправляет адресный broadcast приёмнику AFRd2
 * (`ru.reyter.afrd2.action.BURST`, без extras = умолчание burst.ms) и
 * закрывается. Отдельный пакет нужен потому, что мапперы (tvQuickActions)
 * показывают одну запись на пакет и activity-alias внутри AFRd2 не видят.
 */
package ru.reyter.afrd2.burst;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.widget.Toast;

public class BurstLaunchActivity extends Activity
{
    private static final String ACTION_BURST = "ru.reyter.afrd2.action.BURST";
    private static final String AFRD2_PACKAGE = "ru.reyter.afrd2";

    @Override
    protected void onCreate (Bundle savedInstanceState)
    {
        super.onCreate (savedInstanceState);

        Intent cmd = new Intent (ACTION_BURST);
        cmd.setPackage (AFRD2_PACKAGE);
        sendBroadcast (cmd);

        Toast.makeText (this, R.string.burst_sent, Toast.LENGTH_SHORT).show ();
        finish ();
    }
}
