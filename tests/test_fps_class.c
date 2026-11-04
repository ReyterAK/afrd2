#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../fps_class.h"
#include "../pkgdet.h"	/* PKG_NAME_LEN — общий предел имени пакета */

static void t_classify(void)
{
	assert(fps_classify(25.0) == 25 * 256);
	assert(fps_classify(24.0) == 24 * 256);
	assert(fps_classify(23.976) == fps_rate_hz(0));
	assert(fps_classify(29.97) == fps_rate_hz(3));
	assert(fps_classify(59.94) == fps_rate_hz(6));
	assert(fps_classify(35.0) == 0);   /* отн. ошибка >6% и к 30, и к 25 */
	assert(fps_classify(0.0) == 0);
	/* ближайший стандартный: 24.05 ближе к 24, чем к 23.976 -> 24 */
	assert(fps_classify(24.05) == 24 * 256);
	/* а 23.5 ближе к 23.976, чем к 24 */
	assert(fps_classify(23.5) == fps_rate_hz(0));
}

/* Решение по окну замера: счётчик кадров дискретен (±1 кадр), метки времени
 * имеют неопределённость. Окно годится, только если весь доверительный
 * интервал укладывается в одну группу режимов. */
static void t_classify_window(void)
{
	int hz = -1;

	/* Живой случай (лог 22:50:15, R47): 37 кадров за 1.5 с. 24.64 fps «ближе»
	 * к 25, но ±1 кадр накрывает и 24, и 25 -> решать нельзя, нужно окно шире. */
	assert(fps_classify_window(37, 1501500, 0, &hz) == 0);
	assert(hz == 0);

	/* 24 fps за 1.5 с (36 кадров) — тоже неоднозначно: ±1 кадр доходит до 25 */
	assert(fps_classify_window(36, 1500000, 0, &hz) == 0);
	/* 23.976 за 1.5 с (35 кадров) — однозначно */
	assert(fps_classify_window(35, 1500000, 0, &hz) == 1 && hz == fps_rate_hz(0));
	/* 25 fps за 1.5 с (38 кадров) — однозначно, быстрый путь сохраняется */
	assert(fps_classify_window(38, 1500000, 0, &hz) == 1 && hz == fps_rate_hz(2));

	/* далеко отстоящие частоты решаются в том же окне */
	assert(fps_classify_window(75, 1500000, 0, &hz) == 1 && hz == fps_rate_hz(5));  /* 50 */
	assert(fps_classify_window(90, 1500000, 0, &hz) == 1 && hz == fps_rate_hz(6));  /* 59.94 */
	assert(fps_classify_window(45, 1500000, 0, &hz) == 1 && hz == fps_rate_hz(3));  /* 29.97 */

	/* расширение окна до 2.0 с ещё не помогает, до 2.5 с — решает */
	assert(fps_classify_window(48, 2000000, 0, &hz) == 0);
	assert(fps_classify_window(60, 2500000, 0, &hz) == 1 && hz == fps_rate_hz(0));

	/* 3 с: 25 fps решается, а «24 + лишний кадр» — ещё нет */
	assert(fps_classify_window(75, 3000000, 0, &hz) == 1 && hz == fps_rate_hz(2));
	assert(fps_classify_window(73, 3001500, 0, &hz) == 0);

	/* 4.5 с: тот самый «24.642 fps» решается в пользу группы 24, а не 25 */
	assert(fps_classify_window(109, 4500000, 0, &hz) == 1 && hz == fps_rate_hz(0));

	/* неопределённость меток времени расширяет доверительный интервал:
	 * при ±30 мс окно 3 с не решает, а 6 с — решает */
	assert(fps_classify_window(73, 3001500, 30000, &hz) == 0);
	assert(fps_classify_window(144, 6000000, 30000, &hz) == 1 && hz == fps_rate_hz(0));

	/* вырожденные случаи */
	assert(fps_classify_window(0, 1500000, 0, &hz) == 0);
	assert(fps_classify_window(36, 0, 0, &hz) == 0);
	assert(fps_classify_window(36, 1000, 1000, &hz) == 0);   /* интервал не определён */
	assert(fps_classify_window(1, 1500000, 0, &hz) == 0);    /* слишком мало кадров */
	assert(fps_classify_window(255, 1500000, 0, &hz) == 0);  /* 170 fps — вне ряда */
}

/* Живой случай 2026-10-01: приложение YouTube на 24-фпс ролике (AV1) давало
 * по счётчику ~25.2 fps, из-за чего решение уходило в группу 25. Поправка
 * плеера (fps.correct.list: com.google.android.youtube.tv=4.5) возвращает
 * решение в группу 24 — на окне до fps.measure.window.max.ms (у 24 Гц и 25 Гц
 * доверительные интервалы ±1 кадр перекрываются дольше, чем без поправки). */
static void t_correct_window(void)
{
	int hz = 0;

	/* 25.17 fps на окне 6 с (ΔF=151): без поправки — группа 25... */
	assert(fps_classify_window(151, 6000000, 0, &hz) == 1 && hz == fps_rate_hz(2));
	/* ...с поправкой 4.5% (25.17 -> 24.08) — группа 24 */
	assert(fps_classify_window_scaled(151, 6000000, 0, 1.045, &hz) == 1 && hz == fps_rate_hz(0));

	/* YouTube на настоящем 25-фпс ролике: замер 26.17, с поправкой 25.04 —
	 * группа 25 остаётся правильной */
	assert(fps_classify_window(157, 6000000, 0, &hz) == 1 && hz == fps_rate_hz(2));
	assert(fps_classify_window_scaled(157, 6000000, 0, 1.045, &hz) == 1 && hz == fps_rate_hz(2));

	/* Множитель 1.0 (или <= 0) — ровно прежнее поведение */
	assert(fps_classify_window_scaled(151, 6000000, 0, 1.0, &hz) == 1 && hz == fps_rate_hz(2));
	assert(fps_classify_window_scaled(151, 6000000, 0, 0.0, &hz) == 1 && hz == fps_rate_hz(2));
}

/* Разбор списка поправок: "<пакет>=<проценты>" через пробел. */
static void t_correct_list(void)
{
	fps_correct_clear();
	assert(fps_correct_factor("com.google.android.youtube.tv") == 1.0);

	fps_correct_load("com.google.android.youtube.tv=4.5  studio.scillarium.ottnavigator=2.4");
	assert(fabs(fps_correct_factor("com.google.android.youtube.tv") - 1.045) < 1e-9);
	assert(fabs(fps_correct_factor("studio.scillarium.ottnavigator") - 1.024) < 1e-9);
	/* чужой пакет и NULL — без поправки */
	assert(fps_correct_factor("org.videolan.vlc") == 1.0);
	assert(fps_correct_factor(NULL) == 1.0);

	/* мусорные записи игнорируются, корректные рядом с ними — работают */
	fps_correct_load("мусор =5 =3 youtube=0 com.google.android.youtube.tv=5.0");
	assert(fps_correct_factor("мусор") == 1.0);
	assert(fps_correct_factor("") == 1.0);
	assert(fps_correct_factor("youtube") == 1.0);		/* 0% — не поправка */
	assert(fabs(fps_correct_factor("com.google.android.youtube.tv") - 1.05) < 1e-9);

	/* отрицательное значение (плеер занижает) тоже допустимо */
	fps_correct_load("some.player=-3");
	assert(fabs(fps_correct_factor("some.player") - 0.97) < 1e-9);

	fps_correct_clear();
	assert(fps_correct_factor("com.google.android.youtube.tv") == 1.0);
}

static void t_group(void)
{
	assert(fps_rate_group(fps_rate_hz(0)) == fps_rate_group(fps_rate_hz(1)));
	assert(fps_rate_group(fps_rate_hz(3)) == fps_rate_group(fps_rate_hz(4)));
	assert(fps_rate_group(fps_rate_hz(6)) == fps_rate_group(fps_rate_hz(7)));
	assert(fps_rate_group(fps_rate_hz(2)) != fps_rate_group(fps_rate_hz(1)));
	assert(fps_rate_group(fps_rate_hz(5)) != fps_rate_group(fps_rate_hz(2)));
	assert(fps_rate_group(12345) == 0);   /* нестандартная частота */
}

static void t_refine_declared(void)
{
	int hz = -1;

	/* Живой случай 30.09 (канал TEAM Bigbang HD): по PTS сегмента поток
	 * 23.976 fps, счётчик кадров давал 24.562 -> классификация 25 Гц.
	 * `frame dur` 4004 (23.976) рядом с замером => уточняем до группы 24. */
	assert(fps_refine_declared(25 * 256, 96000.0 / 4004.0, 5.0, &hz) == 1);
	assert(hz == fps_rate_hz(0));

	/* 25-фпс поток (Первый канал): заявлено 25.0, замер 25.95 — та же группа */
	assert(fps_refine_declared(25 * 256, 96000.0 / 3840.0, 5.0, &hz) == 1);
	assert(hz == fps_rate_hz(2));

	/* Локальный клип 23.976: драйвер рапортует 30 fps (мусор), от замера далеко —
	 * уточнять нельзя, иначе 23.976 уехал бы в 30 Гц */
	assert(fps_refine_declared(fps_rate_hz(0), 96000.0 / 3200.0, 5.0, &hz) == 0);

	/* выключено и вырожденные случаи */
	assert(fps_refine_declared(25 * 256, 23.976, 0.0, &hz) == 0);
	assert(fps_refine_declared(0, 23.976, 5.0, &hz) == 0);
	assert(fps_refine_declared(25 * 256, 0.0, 5.0, &hz) == 0);
	assert(fps_refine_declared(25 * 256, 35.0, 5.0, &hz) == 0);  /* 35 — вне стандартного ряда */
	assert(fps_refine_declared(25 * 256, 30.0, 5.0, &hz) == 0);  /* стандарт, но далеко (16.7%) */
}

/* Сквозной путь замера, как он идёт в afrd.c: решение по окну -> уточнение
 * заявленной длительностью кадра. Числа — из живых замеров канала
 * TEAM Bigbang HD (OTT Navigator): 136 кадров за 5.507 с = 24.694 fps,
 * frame dur 4004 (23.976). До правки демон выбирал 25 Гц (mode id 20/50),
 * после — 24 Гц (id 21/51).
 *
 * dt_unc_us = 0 здесь — идеализация для наглядности: в демоне неопределённость
 * считается по длительностям крайних чтений. Живой прогон решил окно за
 * 5006 мс (124 кадра = 24.77 fps) и уточнился до 23.976 — см. лог в README. */
static void t_live_scenario(void)
{
	int hz = 0, refined = 0;

	assert(fps_classify_window(136, 5507000, 0, &hz) == 1);
	assert(hz == fps_rate_hz(2));                    /* 25 Гц — как было */
	assert(fps_refine_declared(hz, 96000.0 / 4004.0, 5.0, &refined) == 1);
	assert(refined == fps_rate_hz(0));               /* 23.976 -> 24 Гц, как надо */

	/* а если бы frame dur был мусорным (30 fps, как на синтетических клипах),
	 * уточнения нет и остаётся прежнее решение */
	assert(fps_refine_declared(hz, 96000.0 / 3200.0, 5.0, &refined) == 0);
}

/* Точный представитель группы — для mode.use.fract=2 (режимы 24.000/30.000/60.000) */
static void t_exact_member(void)
{
	assert(fps_rate_exact_member(fps_rate_hz(0)) == fps_rate_hz(1));  /* 23.976 -> 24.000 */
	assert(fps_rate_exact_member(fps_rate_hz(1)) == fps_rate_hz(1));
	assert(fps_rate_exact_member(fps_rate_hz(2)) == fps_rate_hz(2));  /* 25 без изменений */
	assert(fps_rate_exact_member(fps_rate_hz(3)) == fps_rate_hz(4));  /* 29.97 -> 30.000 */
	assert(fps_rate_exact_member(fps_rate_hz(4)) == fps_rate_hz(4));
	assert(fps_rate_exact_member(fps_rate_hz(5)) == fps_rate_hz(5));  /* 50 без изменений */
	assert(fps_rate_exact_member(fps_rate_hz(6)) == fps_rate_hz(7));  /* 59.94 -> 60.000 */
	assert(fps_rate_exact_member(fps_rate_hz(7)) == fps_rate_hz(7));
	assert(fps_rate_exact_member(12345) == 0);                        /* не из таблицы */
}

/* Дробный представитель группы — для mode.use.fract=1 («всегда дробная»).
 * Сюда приходят источники, которые называют частоту точно: подсказка плеера
 * (24000) и быстрое решение по полю `frame rate` (24 fps -> 24.000). */
static void t_fract_member(void)
{
	assert(fps_rate_fract_member(fps_rate_hz(1)) == fps_rate_hz(0));  /* 24.000 -> 23.976 */
	assert(fps_rate_fract_member(fps_rate_hz(0)) == fps_rate_hz(0));
	assert(fps_rate_fract_member(fps_rate_hz(2)) == fps_rate_hz(2));  /* 25 без изменений */
	assert(fps_rate_fract_member(fps_rate_hz(4)) == fps_rate_hz(3));  /* 30.000 -> 29.97 */
	assert(fps_rate_fract_member(fps_rate_hz(3)) == fps_rate_hz(3));
	assert(fps_rate_fract_member(fps_rate_hz(5)) == fps_rate_hz(5));  /* 50 без изменений */
	assert(fps_rate_fract_member(fps_rate_hz(7)) == fps_rate_hz(6));  /* 60.000 -> 59.94 */
	assert(fps_rate_fract_member(fps_rate_hz(6)) == fps_rate_hz(6));
	assert(fps_rate_fract_member(12345) == 0);                        /* не из таблицы */
}

/* «Авто» (mode.use.fract=0): целое берётся только если источник заявил целое.
 * Заявленная частота приходит в 24.8 fixed-point — как frame_rate_hint (fps*1000
 * -> *256/1000) или как 96000/frame dur. */
static void t_declared_exact(void)
{
	/* hint: 24000 -> 6144 (24.000) против 23976 -> 6138 (23.976) */
	assert(fps_rate_declared_exact(24000 * 256 / 1000) == fps_rate_hz(1));
	assert(fps_rate_declared_exact(23976 * 256 / 1000) == 0);

	/* frame dur: 96000/4000 = 24.000, 96000/4004 = 23.976 */
	assert(fps_rate_declared_exact((int)(96000.0 * 256.0 / 4000.0 + 0.5)) == fps_rate_hz(1));
	assert(fps_rate_declared_exact((int)(96000.0 * 256.0 / 4004.0 + 0.5)) == 0);

	/* целые 30 и 60 заявляются так же, как дробные пары */
	assert(fps_rate_declared_exact(30 * 256) == fps_rate_hz(4));
	assert(fps_rate_declared_exact(29970 * 256 / 1000) == 0);
	assert(fps_rate_declared_exact(60 * 256) == fps_rate_hz(7));
	assert(fps_rate_declared_exact(59940 * 256 / 1000) == 0);

	/* 25 и 50 дробного варианта не имеют: заявлены — значит «целое» */
	assert(fps_rate_declared_exact(25 * 256) == fps_rate_hz(2));
	assert(fps_rate_declared_exact(50 * 256) == fps_rate_hz(5));

	/* мусор и границы: 0, 30 fps при реальных 23.976 (синтетика §4г),
	 * значение вне таблицы, dur с округлением 4001 -> 23.994 */
	assert(fps_rate_declared_exact(0) == 0);
	assert(fps_rate_declared_exact(30 * 256) != 0);
	assert(fps_rate_declared_exact(12345) == 0);
	assert(fps_rate_declared_exact((int)(96000.0 * 256.0 / 4001.0 + 0.5)) == fps_rate_hz(1));
}

/* Политика mode.use.fract целиком (то, что делает afrd.c после замера) */
static void t_policy(void)
{
	const int f24 = fps_rate_hz(1), f23976 = fps_rate_hz(0);

	/* 1 — дробные: решение не меняется, даже если заявлено целое */
	assert(fps_rate_apply_policy(f23976, 24000 * 256 / 1000, 1) == f23976);

	/* 1 — дробные: целая частота приводится к дробному представителю группы.
	 * Это источники, которые называют частоту точно: подсказка плеера и
	 * быстрое решение по полю `frame rate` (живой случай `.50` 04.11: первый
	 * переход уходил в 24.000, замер через 15 с — в 23.976). */
	assert(fps_rate_apply_policy(f24, 0, 1) == f23976);
	assert(fps_rate_apply_policy(fps_rate_hz(4), 0, 1) == fps_rate_hz(3));  /* 30 -> 29.97 */
	assert(fps_rate_apply_policy(fps_rate_hz(7), 0, 1) == fps_rate_hz(6));  /* 60 -> 59.94 */
	assert(fps_rate_apply_policy(fps_rate_hz(2), 0, 1) == fps_rate_hz(2));  /* 25 без пары */
	assert(fps_rate_apply_policy(fps_rate_hz(5), 0, 1) == fps_rate_hz(5));  /* 50 без пары */
	assert(fps_rate_apply_policy(0, 0, 1) == 0);                            /* мусор */
	assert(fps_rate_apply_policy(12345, 0, 1) == 12345);                    /* не из таблицы */

	/* 2 — целые: всегда точный представитель группы */
	assert(fps_rate_apply_policy(f23976, 0, 2) == f24);
	assert(fps_rate_apply_policy(fps_rate_hz(2), 0, 2) == fps_rate_hz(2));

	/* 0 «Авто»: целое, если заявлено целое */
	assert(fps_rate_apply_policy(f23976, 24000 * 256 / 1000, 0) == f24);
	assert(fps_rate_apply_policy(fps_rate_hz(3), 30 * 256, 0) == fps_rate_hz(4));
	assert(fps_rate_apply_policy(fps_rate_hz(6), 60 * 256, 0) == fps_rate_hz(7));

	/* 0 «Авто»: заявлено дробное или источника нет — остаётся дробное */
	assert(fps_rate_apply_policy(f23976, 23976 * 256 / 1000, 0) == f23976);
	assert(fps_rate_apply_policy(f23976, 0, 0) == f23976);

	/* 0 «Авто»: мусор в заявленном не перетягивает на чужую группу
	 * (синтетика: драйвер заявляет 30 fps при реальных 23.976, §4г) */
	assert(fps_rate_apply_policy(f23976, 30 * 256, 0) == f23976);
	assert(fps_rate_apply_policy(f23976, 12345, 0) == f23976);

	/* 25/50 дробной пары не имеют: «целое» и «дробное» совпадают */
	assert(fps_rate_apply_policy(fps_rate_hz(2), 25 * 256, 0) == fps_rate_hz(2));
	assert(fps_rate_apply_policy(fps_rate_hz(5), 50 * 256, 0) == fps_rate_hz(5));
}

/* Сверка источников (§87.4): отношение меньше 1.6 — молчим, ровно вдвое — 200 */
static void t_sources_ratio(void)
{
	int f24 = fps_rate_hz(1), f60 = fps_rate_hz(7);

	assert(fps_sources_ratio_pct(f60, f60) == 0);          /* сошлись */
	assert(fps_sources_ratio_pct(f24, f24) == 0);
	/* 25 против 50 — тоже ровно вдвое (законная пара 2:1: 25 к/с на 50 Гц
	 * подаются по кадру на кадр, расхождения в подаче не будет) */
	assert(fps_sources_ratio_pct(fps_rate_hz(2), fps_rate_hz(5)) == 200);
	assert(fps_sources_ratio_pct(0, f60) == 0);            /* нет данных */
	assert(fps_sources_ratio_pct(f60, 0) == 0);

	/* «немного разошлись»: 50 против 59.94 (1.2) — не наш случай */
	assert(fps_sources_ratio_pct(fps_rate_hz(5), fps_rate_hz(6)) == 0);

	/* ровно вдвое: счётчик 59.94 против подачи 29.97 (33 мс) — картина §87 */
	assert(fps_sources_ratio_pct(fps_rate_hz(6), fps_rate_hz(3)) == 200);
	assert(fps_sources_ratio_pct(fps_rate_hz(3), fps_rate_hz(6)) == 200); /* порядок не важен */
	assert(fps_sources_ratio_pct(fps_rate_hz(4), fps_rate_hz(2)) == 0);   /* 30 против 25 — 1.2 */

	/* 23.976 против 12? — такой частоты в таблице нет, но отношение считается */
	assert(fps_sources_ratio_pct(fps_rate_hz(0), fps_rate_hz(0) / 2) == 200);
}

/* Одна настройка на поправку AV1 (fps.correct.av1): модель выбирает демон,
 * двойной коррекции быть не может (журнал §99.5). */
static void t_av1_resolve(void)
{
	fps_av1_t a;

	/* ключа нет, старых значений нет -> выключено */
	fps_av1_resolve (NULL, 0.0, 0.0, 0.1, 0.0, &a);
	assert (a.src == FPS_AV1_OFF && a.factor == 1.0 && a.offset == 0.0);

	/* ключа нет, задана только аддитивная -> она */
	fps_av1_resolve (NULL, 0.1, 0.0, 0.1, 0.0, &a);
	assert (a.src == FPS_AV1_LEGACY_ADD && a.offset == 0.1 && a.factor == 1.0);
	assert (!a.legacy_conflict);

	/* ключа нет, задана только относительная -> она */
	fps_av1_resolve (NULL, 0.0, 5.7, 0.1, 0.0, &a);
	assert (a.src == FPS_AV1_LEGACY_PCT && a.offset == 0.0);
	assert (fabs (a.factor - 1.057) < 1e-9);

	/* ключа нет, заданы ОБЕ -> только относительная, конфликт помечен */
	fps_av1_resolve (NULL, 0.1, 5.7, 0.1, 0.0, &a);
	assert (a.src == FPS_AV1_LEGACY_PCT && a.offset == 0.0 && a.legacy_conflict);

	/* off / 0 — выключено, даже если старые значения заданы */
	fps_av1_resolve ("off", 0.1, 5.7, 0.1, 0.0, &a);
	assert (a.src == FPS_AV1_OFF && a.factor == 1.0 && a.offset == 0.0);
	fps_av1_resolve ("0", 0.1, 5.7, 0.1, 0.0, &a);
	assert (a.src == FPS_AV1_OFF);

	/* auto на ATV14: аддитивная 0.1, относительной нет */
	fps_av1_resolve ("auto", 0.0, 0.0, 0.1, 0.0, &a);
	assert (a.src == FPS_AV1_AUTO && a.offset == 0.1 && a.factor == 1.0);

	/* auto на некалиброванной платформе: ничего не делает */
	fps_av1_resolve ("auto", 0.0, 0.0, 0.0, 0.0, &a);
	assert (a.src == FPS_AV1_OFF);

	/* auto перекрывает старые ключи: задан offset=0.5, но auto -> 0.1 */
	fps_av1_resolve ("auto", 0.5, 5.7, 0.1, 0.0, &a);
	assert (a.src == FPS_AV1_AUTO && a.offset == 0.1 && a.factor == 1.0);
	assert (!a.legacy_conflict);

	/* число — только относительная, аддитивной нет (двойной коррекции нет) */
	fps_av1_resolve ("5.7", 0.1, 0.0, 0.1, 0.0, &a);
	assert (a.src == FPS_AV1_PCT && a.offset == 0.0);
	assert (fabs (a.factor - 1.057) < 1e-9);

	/* «570» вместо «5.7» — опечатка: значение вне разумного, но формула та же;
	 * проверяем, что отрицательное и мусорное не включают поправку */
	fps_av1_resolve ("-3", 0.0, 0.0, 0.0, 0.0, &a);
	assert (a.src == FPS_AV1_OFF);
	fps_av1_resolve ("abc", 0.0, 0.0, 0.0, 0.0, &a);
	assert (a.src == FPS_AV1_OFF);
}

static void t_table(void)
{
	assert(strcmp(fps_rate_name(0), "23.976") == 0);
	assert(fps_rate_hz(0) == 6138);
	assert(fps_rate_hz(7) == 60 * 256);
	assert(fps_rate_index_for_hz(25 * 256) == 2);
}

/* Аддитивная поправка (fps.correct.av1.offset) и множитель плеера применяются
 * к числу кадров окна, а не к уже готовой частоте. */
static void t_corrected(void)
{
	int hz = -1;

	/* 24 fps за 5 с (120 кадров): без поправок — группа 24 */
	assert(fps_classify_window_corrected(120, 5000000, 0, 1.0, 0.0, &hz) == 1);
	assert(hz == fps_rate_hz(0));

	/* завышение AV1 на 0.5 кадра/с: 122.5 кадра за 5 с — всё ещё группа 24 */
	assert(fps_classify_window_corrected(122, 5000000, 0, 1.0, 0.5, &hz) == 1);
	assert(hz == fps_rate_hz(0));

	/* 128 кадров за 5 с (25.6 fps) без поправки — это уже 25 */
	assert(fps_classify_window_corrected(128, 5000000, 0, 1.0, 0.0, &hz) == 1);
	assert(hz == fps_rate_hz(2));

	/* 125 кадров за 5 с = 25.0; поправка 0.5 даёт 24.5, а ±1 кадр накрывает
	 * и 24, и 25 -> решения нет */
	assert(fps_classify_window_corrected(125, 5000000, 0, 1.0, 0.5, &hz) == 0);

	/* множитель (fps.correct.list) работает как раньше: 126/1.05 = 120 -> 24 */
	assert(fps_classify_window_corrected(126, 5000000, 0, 1.05, 0.0, &hz) == 1);
	assert(hz == fps_rate_hz(0));

	/* factor <= 0 трактуется как «без поправки» */
	assert(fps_classify_window_corrected(120, 5000000, 0, 0.0, 0.0, &hz) == 1);
	assert(hz == fps_rate_hz(0));
}

static void t_correct_av1(void)
{
	fps_correct_clear ();

	/* поправка не задана — ноль для любого устройства */
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", "com.google.android.youtube.tv") == 0.0);

	/* поправка без пакета действует для любого плеера, но только на AV1 */
	fps_correct_set_av1 (0.5, NULL);
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", "org.videolan.vlc") == 0.5);
	assert(fps_correct_av1_offset ("AMVDEC_AV1_V4L", NULL) == 0.5);	/* без учёта регистра, без пакета */
	assert(fps_correct_av1_offset (NULL, NULL) == 0.0);		/* нет устройства — нет поправки */
	assert(fps_correct_av1_offset ("ammvdec_h264_v4", "org.videolan.vlc") == 0.0);
	assert(fps_correct_av1_offset ("", "org.videolan.vlc") == 0.0);

	/* с пакетом — только для него (пакет в конфиге бывает с пробелами) */
	fps_correct_set_av1 (1.05, " com.google.android.youtube.tv ");
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", "com.google.android.youtube.tv") == 1.05);
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", "org.videolan.vlc") == 0.0);

	/* Имя пакета длиннее поля (PKG_NAME_LEN): поправка отбрасывается целиком,
	 * иначе пустой пакет расширил бы её на все плееры. Предел — общий для
	 * демона (pkgdet.h): 63 байт не хватало, система принимает имена заметно
	 * длиннее, поэтому слишком длинным считается только то, что не влезает
	 * в PKG_NAME_LEN. */
	fps_correct_set_av1 (0.5, "com.google.android.youtube.tv.some.really.long.package.name.over.64.chars");
	assert (strlen ("com.google.android.youtube.tv.some.really.long.package.name.over.64.chars")
	        < PKG_NAME_LEN);
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", "com.google.android.youtube.tv.some.really.long.package.name.over.64.chars") == 0.5);
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", "короткий") == 0.0);

	char too_long [PKG_NAME_LEN + 16];
	memset (too_long, 'x', sizeof (too_long) - 1);
	too_long [sizeof (too_long) - 1] = 0;
	fps_correct_set_av1 (0.5, too_long);
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", too_long) == 0.0);
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", "org.videolan.vlc") == 0.0);

	/* fps_correct_clear сбрасывает и её */
	fps_correct_set_av1 (0.5, NULL);
	fps_correct_clear ();
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", NULL) == 0.0);

	/* выключенная поправка (0) — не действует */
	fps_correct_set_av1 (0.0, NULL);
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", NULL) == 0.0);
}

/* Относительная поправка AV1 (fps.correct.av1.pct): пропорциональное завышение,
 * живой замер 18.10 — 23.976 -> 25.40 (+5.9 %), 59.94 -> 63.30 (+5.6 %). Одна
 * величина лечит обе частоты, в отличие от аддитивной. */
static void t_correct_av1_pct(void)
{
	fps_correct_clear();

	/* по умолчанию выключена */
	assert(fps_correct_av1_factor("amvdec_av1_v4l", NULL) == 1.0);

	fps_correct_set_av1_pct(5.7, NULL);
	assert(fabs(fps_correct_av1_factor("amvdec_av1_v4l", NULL) - 1.057) < 1e-9);

	/* 24 -> 24, 60 -> 60 после деления (сравниваем ГРУППЫ: 23.976 и 24.000 —
	 * одна группа режимов, различие задаёт mode.use.fract) */
	assert(fps_rate_group(fps_classify(25.40 / fps_correct_av1_factor("amvdec_av1_v4l", NULL))) ==
	       fps_rate_group(fps_classify(23.976)));
	assert(fps_rate_group(fps_classify(63.30 / fps_correct_av1_factor("amvdec_av1_v4l", NULL))) ==
	       fps_rate_group(fps_classify(59.94)));

	/* только AV1-декодер */
	assert(fps_correct_av1_factor("amvdec_h264_v4l", NULL) == 1.0);

	/* ограничение по пакету */
	fps_correct_set_av1_pct(5.7, "com.google.android.youtube.tv");
	assert(fps_correct_av1_factor("amvdec_av1_v4l", "com.google.android.youtube.tv") > 1.0);
	assert(fps_correct_av1_factor("amvdec_av1_v4l", "org.videolan.vlc") == 1.0);
	assert(fps_correct_av1_factor("amvdec_av1_v4l", NULL) == 1.0);

	/* список через запятую: auto на ATV14 ограничен Cobalt-плеерами, чтобы
	 * не ломать точный счётчик Just Player/VLC (живой замер §174: 24p →
	 * 23.98, 25p → 25.00 — глобальные 5 % уводили 25p в 24) */
	fps_correct_set_av1_pct(5.0,
		"io.gh.reisxd.tizentube.cobalt,com.google.android.youtube.tv");
	assert(fps_correct_av1_factor("amvdec_av1_v4l", "io.gh.reisxd.tizentube.cobalt") > 1.0);
	assert(fps_correct_av1_factor("amvdec_av1_v4l", "com.google.android.youtube.tv") > 1.0);
	assert(fps_correct_av1_factor("amvdec_av1_v4l", "com.brouken.player") == 1.0);
	assert(fps_correct_av1_factor("amvdec_av1_v4l", "io.gh.reisxd.tizentube.cobalt.extra") == 1.0);

	/* аддитивная и относительная сосуществуют: вызов одной не затирает другую.
	 * clear() нужен, потому что выше поправка была ограничена пакетом. */
	fps_correct_clear();
	fps_correct_set_av1(0.5, NULL);
	fps_correct_set_av1_pct(5.7, NULL);
	assert(fabs(fps_correct_av1_offset("amvdec_av1_v4l", NULL) - 0.5) < 1e-9);
	assert(fabs(fps_correct_av1_factor("amvdec_av1_v4l", NULL) - 1.057) < 1e-9);

	/* опечатка «570» вместо «5.7» не принимается */
	fps_correct_clear();
	fps_correct_set_av1_pct(570.0, NULL);
	assert(fps_correct_av1_factor("amvdec_av1_v4l", NULL) == 1.0);

	/* clear сбрасывает обе */
	fps_correct_set_av1_pct(5.7, NULL);
	fps_correct_clear();
	assert(fps_correct_av1_factor("amvdec_av1_v4l", NULL) == 1.0);
}


/* Вердикт подачи: «неровная» (медиана в норме) больше не лечится сменой
 * режима, лечится только «вдвое реже» (медиана 2x базы). */
static void t_cadence_verdict(void)
{
	/* Живой случай `.42` 03.11: 4K DV, Vimu, 19 % интервалов по 84 мс при
	 * медиане 41.7 мс — потери кадров у плеера, не vsync-путь. */
	assert(fps_cadence_verdict(42, 42, 42, 19, 10) == FPS_CADENCE_UNEVEN);
	assert(fps_cadence_verdict(42, 42, 42, 10, 10) == FPS_CADENCE_UNEVEN);
	assert(fps_cadence_verdict(42, 42, 42, 9, 10) == FPS_CADENCE_OK);

	/* Вся подача вдвое реже базы — дефект vsync-пути, лечение уместно. */
	assert(fps_cadence_verdict(84, 42, 42, 0, 10) == FPS_CADENCE_HALVED);
	assert(fps_cadence_verdict(83, 42, 42, 50, 10) == FPS_CADENCE_HALVED);

	/* Подача быстрее базы — поля 1080i (счётчик 25, в плоскость 50):
	 * «выбросы» считаются от завышенной базы и ничего не значат. */
	assert(fps_cadence_verdict(20, 42, 40, 50, 10) == FPS_CADENCE_OK);

	/* Ожидание контента больше периода дисплея — база берётся по максимуму.
	 * 24 к/с на 60 Гц: период дисплея 16.7, ожидание 41.7, медиана 41.7. */
	assert(fps_cadence_verdict(42, 17, 42, 0, 10) == FPS_CADENCE_OK);
	assert(fps_cadence_verdict(42, 17, 42, 19, 10) == FPS_CADENCE_UNEVEN);
	assert(fps_cadence_verdict(84, 17, 42, 0, 10) == FPS_CADENCE_HALVED);

	/* Нет данных — OK, лечить нечего. */
	assert(fps_cadence_verdict(0, 0, 0, 99, 10) == FPS_CADENCE_OK);
}

static void t_fb_declared(void)
{
	/* fb-декодер Amlogic: узел заявляет 24 (frame dur 4000), огибающая
	 * видит ~48 — берём заявленную (живой бокс X96 X10 Pro, §90). */
	assert(fps_fb_declared_hz(48 * 256, 24 * 256) == 24 * 256);
	assert(fps_fb_declared_hz(50 * 256, 24 * 256) == 24 * 256);
	assert(fps_fb_declared_hz(60 * 256, 30 * 256) == 30 * 256);

	/* обычное расхождение не трогаем */
	assert(fps_fb_declared_hz(25 * 256, 24 * 256) == 25 * 256);
	assert(fps_fb_declared_hz(84 * 256, 24 * 256) == 84 * 256);

	/* нет данных — возвращаем измеренное */
	assert(fps_fb_declared_hz(25 * 256, 0) == 25 * 256);
	assert(fps_fb_declared_hz(0, 24 * 256) == 0);
}

/* Подача видео-слоя как независимый источник (журнал §179). Числа — живые
 * замеры на `.50`: YouTube 24p 41.733 мс, 25p 39.946 мс, превью 30p 33.465 мс
 * при периоде дисплея 16.667 мс. */
static void t_layer_rate(void)
{
	int hz = -1;

	/* 24 к/с на 60 Гц: 3:2, среднее 41.7 мс */
	assert(fps_layer_rate_hz(127, 41.733, 16.667, 0, &hz) == 1);
	assert(hz == fps_rate_hz(0));

	/* 25 к/с: 2,2,3 — среднее 40.0 мс */
	assert(fps_layer_rate_hz(127, 39.946, 16.667, 0, &hz) == 1);
	assert(hz == fps_rate_hz(2));

	/* 30 к/с (превью YouTube) */
	assert(fps_layer_rate_hz(127, 33.465, 16.667, 0, &hz) == 1);
	assert(hz == fps_rate_hz(3));

	/* Слой обновляется на частоте дисплея — о контенте ничего не говорит */
	assert(fps_layer_rate_hz(127, 16.700, 16.667, 0, &hz) == 0);
	assert(hz == 0);

	/* Пауза/переход: выброс в буфере — среднему не верим */
	assert(fps_layer_rate_hz(127, 57.700, 16.667, 5, &hz) == 0);

	/* Мало меток — буфер ещё не заполнился после смены ролика */
	assert(fps_layer_rate_hz(60, 41.733, 16.667, 0, &hz) == 0);

	/* Мусор на входе */
	assert(fps_layer_rate_hz(127, 0.0, 16.667, 0, &hz) == 0);
	assert(fps_layer_rate_hz(127, 41.733, 0.0, 0, &hz) == 0);
}

/* Признак перехода контента по двум половинам буфера слоя (журнал §181):
 * реклама в начале ролика и смена канала — две каденции в одном окне. */
static void t_layer_transition(void)
{
	/* Реклама 30 к/с (33.3 мс) → ролик 24 к/с (41.7 мс) */
	assert(fps_layer_transition(33.33, 41.71, 63, 64, 5000.0, 30000.0) == 1);

	/* Смена канала 60 → 24 */
	assert(fps_layer_transition(16.67, 41.71, 63, 64, 3000.0, -1.0) == 1);

	/* 25 к/с против 24 — разные классы (смена потока), а не шум */
	assert(fps_layer_transition(40.00, 41.71, 60, 60, 5000.0, -1.0) == 1);

	/* Один и тот же контент: 3:2 даёт 33/50 мс, но медианы половин
	 * одинаковые — перехода нет */
	assert(fps_layer_transition(41.71, 41.80, 63, 64, 5000.0, -1.0) == 0);
	assert(fps_layer_transition(40.00, 40.05, 63, 64, 5000.0, -1.0) == 0);

	/* Мало интервалов в половине — сравнивать нечего */
	assert(fps_layer_transition(33.33, 41.71, 39, 64, 5000.0, -1.0) == 0);
	assert(fps_layer_transition(33.33, 41.71, 63, 39, 5000.0, -1.0) == 0);

	/* Наша смена режима попала в буфер (2 с назад при буфере 5 с): половины
	 * разных классов — ожидаемо, контент тот же */
	assert(fps_layer_transition(16.67, 41.71, 63, 64, 5000.0, 2000.0) == 0);
	assert(fps_layer_transition(16.67, 41.71, 63, 64, 5000.0, 5500.0) == 0);
	/* Смена была давно — это уже смена контента */
	assert(fps_layer_transition(16.67, 41.71, 63, 64, 5000.0, 20000.0) == 1);

	/* Мусор: интервал вне классов частот (100 мс ≈ 10 к/с) */
	assert(fps_layer_transition(100.0, 41.71, 63, 64, 5000.0, -1.0) == 0);
	assert(fps_layer_transition(0.0, 41.71, 63, 64, 5000.0, -1.0) == 0);
	assert(fps_layer_transition(33.33, 0.0, 63, 64, 5000.0, -1.0) == 0);
}

int main(void)
{
	t_classify();
	t_classify_window();
	t_refine_declared();
	t_exact_member();
	t_fract_member();
	t_declared_exact();
	t_policy();
	t_sources_ratio();
	t_av1_resolve();
	t_live_scenario();
	t_group();
	t_table();
	t_correct_window();
	t_correct_list();
	t_corrected();
	t_correct_av1();
	t_correct_av1_pct();
	t_cadence_verdict();
	t_layer_rate();
	t_layer_transition();
	t_fb_declared();
	printf("fps_class: OK\n");
	return 0;
}
