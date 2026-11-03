/*
 * AFRd2: сеанс «AFR по требованию» — сторона приложения.
 *
 * Демон принимает команду `burst <мс>` по UDP (127.0.0.1:50505) и сам делает
 * всё остальное: замер, одно переключение, возврат режима. Здесь только
 * отправка команды и разбор ответа — без root, без прав, без второго демона.
 *
 * Интерфейсы (согласованы с разработчиком прошивки slimBOXtv, см.
 * docs/afr-on-demand-answers.md):
 *   Activity  ru.cobra.afrd.BURST   extra `ms` (int, мс; 0 — отмена)
 *   Broadcast ru.reyter.afrd2.action.BURST   extras `ms` (int) / `cancel` (bool)
 * Состояние сеанса демон рассылает сам:
 *   Broadcast ru.reyter.afrd2.action.BURST_STATE
 *   extras state (started|switched|finished|cancelled), hz_milli, left_ms
 */

package ru.cobra.zap.afrd;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketTimeoutException;

public class Burst
{
    public static final String ACTION_BURST = "ru.reyter.afrd2.action.BURST";
    public static final String ACTION_STATE = "ru.reyter.afrd2.action.BURST_STATE";
    public static final String EXTRA_MS = "ms";
    public static final String EXTRA_CANCEL = "cancel";
    public static final String EXTRA_STATE = "state";
    public static final String EXTRA_HZ_MILLI = "hz_milli";
    public static final String EXTRA_LEFT_MS = "left_ms";

    /* Допуск и умолчание — те же, что в демоне (burst.h): 15 000…120 000,
     * 0 в команде значит «взять burst.ms из конфига». */
    public static final int MS_MIN = 15000;
    public static final int MS_MAX = 120000;
    public static final int MS_DEFAULT = 40000;

    private static final int API_PORT = 50505;

    /** Привести длительность к допуску; 0 остаётся нулём (это отмена). */
    public static int clamp (int ms)
    {
        if (ms == 0)
            return 0;
        if (ms < 0)
            return MS_DEFAULT;
        if (ms < MS_MIN)
            return MS_MIN;
        if (ms > MS_MAX)
            return MS_MAX;
        return ms;
    }

    /** Результат асинхронной отправки: ответ демона или null (не ответил). */
    public interface Callback
    {
        void onResult (String reply);
    }

    /**
     * Отправить команду, не блокируя вызывающий поток.
     *
     * Сеть на главном потоке запрещена (NetworkOnMainThreadException — живая
     * находка: Activity падала при старте), поэтому отправка идёт в отдельном
     * потоке, а результат приходит в главный.
     */
    public static void sendAsync (final int ms, final Callback cb)
    {
        new Thread (new Runnable ()
        {
            @Override
            public void run ()
            {
                final String reply = send (ms);
                new Handler (Looper.getMainLooper ()).post (new Runnable ()
                {
                    @Override
                    public void run ()
                    {
                        cb.onResult (reply);
                    }
                });
            }
        }, "afrd-burst").start ();
    }

    /**
     * Отправить команду демону.
     * @param ms длительность в мс; 0 — отмена, отрицательное — умолчание демона
     * @return ответ демона строкой или null, если он не ответил (демона нет)
     */
    public static String send (int ms)
    {
        DatagramSocket sock = null;
        try
        {
            sock = new DatagramSocket ();
            sock.setSoTimeout (700);

            byte[] cmd = ("burst " + ms + "\n").getBytes ("UTF-8");
            InetAddress loopback = InetAddress.getByName ("127.0.0.1");
            sock.send (new DatagramPacket (cmd, cmd.length, loopback, API_PORT));

            byte[] buf = new byte[256];
            DatagramPacket reply = new DatagramPacket (buf, buf.length);
            sock.receive (reply);

            return new String (buf, 0, reply.getLength (), "UTF-8").trim ();
        }
        catch (SocketTimeoutException exc)
        {
            Log.d ("afrd", "burst: daemon did not answer");
        }
        catch (IOException exc)
        {
            jfun.logExc ("Burst.send", exc);
        }
        finally
        {
            /* Сокет закрываем всегда: раньше на таймауте он оставался до GC
             * (нашло ревью GUI). */
            if (sock != null)
                sock.close ();
        }

        return null;
    }
}
