#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../fps_class.h"

static void t_estimate(void)
{
	/* 100 кадров за 4.000 c = 25 fps */
	assert(fps_estimate(1000, 0, 1100, 4000000) > 24.99 && fps_estimate(1000, 0, 1100, 4000000) < 25.01);
	/* 240 кадров за 10.010 c = 23.976 fps */
	double f = fps_estimate(0, 0, 240, 10010000);
	assert(f > 23.97 && f < 23.98);
	/* вырожденные случаи -> 0 */
	assert(fps_estimate(500, 0, 500, 2000000) == 0.0);   /* видео на паузе */
	assert(fps_estimate(500, 0, 501, 2000000) == 0.0);   /* слишком короткое окно */
	assert(fps_estimate(0, 0, 100000, 1000000) == 0.0);  /* мусор (100000 fps) */
}

static void t_classify(void)
{
	assert(fps_classify(25.0) == 25 * 256);
	assert(fps_classify(24.0) == 24 * 256);
	assert(fps_classify(23.976) == fps_rate_hz(0));
	assert(fps_classify(29.97) == fps_rate_hz(3));
	assert(fps_classify(59.94) == fps_rate_hz(6));
	assert(fps_classify(35.0) == 0);   /* ровно между 30 и 25 — не угадываем */
	assert(fps_classify(0.0) == 0);
	/* ближайший стандартный: 24.05 ближе к 24, чем к 23.976 -> 24 */
	assert(fps_classify(24.05) == 24 * 256);
	/* а 23.5 ближе к 23.976, чем к 24 */
	assert(fps_classify(23.5) == fps_rate_hz(0));
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
	t_estimate();
	t_classify();
	t_table();
	printf("fps_class: OK\n");
	return 0;
}
