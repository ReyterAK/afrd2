#include <assert.h>
#include <stdio.h>
#include "../fps_env.h"
#include "../fps_class.h"

#define ENV_HORIZON	20000000ULL	/* профиль: fps.measure.envelope.ms */
#define ENV_SUB		5000000ULL	/* профиль: fps.measure.envelope.sub.ms */
#define ENV_SKIP	2000000ULL	/* профиль: fps.measure.envelope.skip.ms */

/* Фикстура: строки "<мс>\t<прирост счётчика>". */
static int load_fixture (const char *path, fps_env_t *env)
{
	FILE *f = fopen (path, "r");
	assert (f);

	fps_env_reset (env);
	char line [128];
	unsigned long long frames = 0;
	while (fgets (line, sizeof (line), f))
	{
		if (line [0] == '#' || line [0] == '\n')
			continue;

		unsigned long long ms = 0, d = 0;
		if (sscanf (line, "%llu\t%llu", &ms, &d) != 2)
			continue;

		/* второй столбец — накопленный счётчик кадров на этот момент */
		frames = d;
		fps_env_push (env, ms * 1000, frames, 2000);	/* чтение ~2 мс */
	}

	fclose (f);
	return env->n;
}

static void t_fixtures (void)
{
	fps_env_t env;
	int hz = -1;

	assert (load_fixture ("tests/fixtures/fps_env_24av1_ok.txt", &env) > 20);
	assert (fps_env_ready (&env, ENV_HORIZON));
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 1);
	assert (hz == fps_rate_hz (0));		/* 23.976 -> группа 24 */

	/* AV1 соврал настолько, что огибающая без поправки даёт 25 — фиксируем
	 * известное ограничение; с поправкой 0.5 кадра/с ряд даёт группу 24 */
	assert (load_fixture ("tests/fixtures/fps_env_24av1_high.txt", &env) > 20);
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 1);
	assert (hz == fps_rate_hz (2));
	/* тот же ряд с поправкой AV1 0.5 кадра/с даёт ~24.41 fps: самое медленное
	 * окно становится пограничным и не решается, но следующее по медленности
	 * решаемое окно уводит ряд в безопасный класс 25 (50 Гц) вместо
	 * бездействия. */
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.5, &hz) == 1);
	assert (hz == fps_rate_hz (2));

	assert (load_fixture ("tests/fixtures/fps_env_25av1.txt", &env) > 20);
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 1);
	assert (hz == fps_rate_hz (2));
	/* поправка -0.5 не должна сломать честный 25-фпс ряд */
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.5, &hz) == 1);
	assert (hz == fps_rate_hz (2));

	assert (load_fixture ("tests/fixtures/fps_env_24vp9.txt", &env) > 20);
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 1);
	assert (hz == fps_rate_hz (0));
}

/* Синтетика: 60 fps, первые 2 с — 50 fps (набор буфера). Прогрев 2 с
 * выбрасывает провальный участок, поэтому решение — 59.94, а не 50.
 * (Живой случай: журнал §13.9 — первое окно 50.332 fps при эталоне 60.086.) */
static void t_startup_dip (void)
{
	fps_env_t env;
	fps_env_reset (&env);

	unsigned long long t = 0, f = 0;
	for (int i = 1; i <= 31; i++)
	{
		t += 1000000;
		f += (i <= 2) ? 50 : 60;
		fps_env_push (&env, t, f, 2000);
	}

	int hz = -1;
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 1);
	assert (hz == fps_rate_hz (6));		/* 59.94 */
}

/* Пауза: окно с простоем не рассматривается, наблюдение начинается заново. */
static void t_pause (void)
{
	fps_env_t env;
	fps_env_reset (&env);

	unsigned long long t = 0, f = 0;
	for (int i = 0; i < 8; i++)
	{
		t += 1000000;
		f += 25;
		fps_env_push (&env, t, f, 2000);
	}
	assert (fps_env_push (&env, t + 1000000, f, 2000) == false);	/* счётчик стоит */
	assert (env.n == 1);						/* наблюдение заново */

	for (int i = 0; i < 12; i++)
	{
		t += 1000000;
		f += 25;
		fps_env_push (&env, t, f, 2000);
	}
	assert (fps_env_ready (&env, ENV_HORIZON - 9000000));	/* 11 с из 13 выборок */

	int hz = -1;
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 1);
	assert (hz == fps_rate_hz (2));		/* 25 */
}

/* Сброс счётчика (новое видео): кадры двух видео не попадают в одну огибающую. */
static void t_counter_reset (void)
{
	fps_env_t env;
	fps_env_reset (&env);

	unsigned long long t = 0;
	for (unsigned long long f = 0; f <= 120000; f += 60000)	/* 60 fps, 2 с */
	{
		t += 1000000;
		fps_env_push (&env, t, f, 2000);
	}
	assert (fps_env_push (&env, t + 1000000, 12, 2000) == false);	/* счётчик сброшен */
	assert (env.n == 1);

	for (int i = 1; i <= 12; i++)				/* дальше 25 fps */
		fps_env_push (&env, t + 1000000 + i * 1000000ULL, 12 + 25 * (unsigned)i, 2000);

	int hz = -1;
	assert (fps_env_decide (&env, ENV_HORIZON, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 1);
	assert (hz == fps_rate_hz (2));		/* 25, а не смесь 60 и 25 */
}

/* Живая находка (80bkVen5CJI, AV1, 24 fps): самое медленное окно
 * ~24.75 fps пограничное (доверительный интервал ±1 кадр и ±unc_us
 * пересекает границу класса 24.49) и решения не даёт; следующее по
 * медленности окно ~24.95 fps решается и уводит ролик в класс 25.
 * Старое правило («только самое медленное окно») возвращало 0, и AFR не
 * переключался вообще. */
static void t_slowest_decidable (void)
{
	fps_env_t env;
	fps_env_reset (&env);

	unsigned long long t = 0, f = 0;
	const unsigned long long step = 1272500;	/* каденция ~1.27 с, окно ~5.09 с */
	const unsigned long long unc = 45000;		/* длительность чтения */

	/* приросты счётчика: ранние окна дают ΔF=126 (~24.75 fps, не решаются),
	 * затем ΔF=127 (~24.95 fps, решается) */
	static const unsigned inc [24] =
	{
		32, 32, 31, 31,
		32, 32, 32, 32, 32, 32, 32, 32,
		32, 32, 32, 32, 32, 32, 32, 32,
		32, 32, 32, 32,
	};

	fps_env_push (&env, 0, 0, unc);
	for (unsigned k = 0; k < sizeof (inc) / sizeof (inc [0]); k++)
	{
		t += step;
		f += inc [k];
		fps_env_push (&env, t, f, unc);
	}

	int hz = -1;
	assert (fps_env_decide (&env, ENV_HORIZON, 0, ENV_SUB, 1.0, 0.0, &hz) == 1);
	assert (hz == fps_rate_hz (2));		/* 25 — безопасный класс для 24 fps */
}

/* Пока горизонт не набран — решения нет. */
static void t_ready (void)
{
	fps_env_t env;
	fps_env_reset (&env);

	unsigned long long t = 0;
	for (int i = 1; i <= 5; i++)
	{
		t += 1000000;
		fps_env_push (&env, t, (unsigned long long)(i * 25), 2000);
	}

	assert (!fps_env_ready (&env, ENV_HORIZON));

	int hz = -1;
	assert (fps_env_decide (&env, ENV_HORIZON, 0, ENV_SUB, 1.0, 0.0, &hz) == 0);
}

/* Кольцо: при переполнении старые выборки вытесняются, решение всё равно есть. */
static void t_ring_overflow (void)
{
	fps_env_t env;
	fps_env_reset (&env);

	unsigned long long t = 0;
	for (int i = 1; i <= FPS_ENV_MAX * 2 + 5; i++)
	{
		t += 300000;						/* 300 мс — штатный опрос */
		fps_env_push (&env, t, (unsigned long long)(i * 7), 2000);	/* 23.3 fps */
	}
	assert (env.n <= FPS_ENV_MAX);

	int hz = -1;
	assert (fps_env_decide (&env, ENV_HORIZON, 0, ENV_SUB, 1.0, 0.0, &hz) == 1);
}

/* Минимальный горизонт: подокну нужно не меньше skip + sub данных, иначе
 * огибающая не даёт ни одного окна. run_atv14() поэтому поднимает
 * fps.measure.envelope.ms до skip + sub — без этого envelope.ms=0
 * («решение по первому подокну») навсегда оставлял бы демон без решения. */
static void t_horizon_min (void)
{
	fps_env_t env;
	fps_env_reset (&env);

	/* 12 с ровного 24 fps (120 кадров за 5 с), опрос 250 мс */
	unsigned long long t = 0;
	for (int i = 1; i <= 48; i++)
	{
		t += 250000;
		fps_env_push (&env, t, (unsigned long long)(i * 6), 0);
	}

	unsigned long long min_us = ENV_SKIP + ENV_SUB;

	int hz = -1;
	/* горизонт короче skip + sub — ни одного подокна */
	assert (fps_env_decide (&env, 0, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 0);
	assert (fps_env_decide (&env, min_us - 1, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 0);

	/* ровно skip + sub — подокно [skip, skip + sub] уже укладывается */
	assert (fps_env_decide (&env, min_us, ENV_SKIP, ENV_SUB, 1.0, 0.0, &hz) == 1);
	assert (hz == fps_rate_hz (0));
}

int main (void)
{
	t_fixtures ();
	t_startup_dip ();
	t_pause ();
	t_counter_reset ();
	t_slowest_decidable ();
	t_ready ();
	t_ring_overflow ();
	t_horizon_min ();
	printf ("fps_env: OK\n");
	return 0;
}
