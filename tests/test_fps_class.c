#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../fps_class.h"

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

/* Точный представитель группы — для mode.use.fract=0 (режимы 24.000/30.000/60.000) */
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

	/* имя пакета длиннее поля (64 байта): поправка отбрасывается целиком,
	 * иначе пустой пакет расширил бы её на все плееры */
	fps_correct_set_av1 (0.5, "com.google.android.youtube.tv.some.really.long.package.name.over.64.chars");
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", "короткий") == 0.0);
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", "com.google.android.youtube.tv.some.really.long.package.name.over.64.chars") == 0.0);

	/* fps_correct_clear сбрасывает и её */
	fps_correct_set_av1 (0.5, NULL);
	fps_correct_clear ();
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", NULL) == 0.0);

	/* выключенная поправка (0) — не действует */
	fps_correct_set_av1 (0.0, NULL);
	assert(fps_correct_av1_offset ("amvdec_av1_v4l", NULL) == 0.0);
}

int main(void)
{
	t_classify();
	t_classify_window();
	t_refine_declared();
	t_exact_member();
	t_live_scenario();
	t_group();
	t_table();
	t_correct_window();
	t_correct_list();
	t_corrected();
	t_correct_av1();
	printf("fps_class: OK\n");
	return 0;
}
