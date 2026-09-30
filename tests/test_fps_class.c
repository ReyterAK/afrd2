#include <assert.h>
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
	assert(fps_refine_declared(25 * 256, 47.0, 5.0, &hz) == 0);  /* 47 не стандарт */
}

/* Сквозной путь замера, как он идёт в afrd.c: решение по окну -> уточнение
 * заявленной длительностью кадра. Числа — из живых замеров канала
 * TEAM Bigbang HD (OTT Navigator): 136 кадров за 5.507 с = 24.694 fps,
 * frame dur 4004 (23.976). До правки демон выбирал 25 Гц (mode id 20/50),
 * после — 24 Гц (id 21/51). */
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

static void t_table(void)
{
	assert(strcmp(fps_rate_name(0), "23.976") == 0);
	assert(fps_rate_hz(0) == 6138);
	assert(fps_rate_hz(7) == 60 * 256);
	assert(fps_rate_index_for_hz(25 * 256) == 2);
}

int main(void)
{
	t_classify();
	t_classify_window();
	t_refine_declared();
	t_live_scenario();
	t_group();
	t_table();
	printf("fps_class: OK\n");
	return 0;
}
