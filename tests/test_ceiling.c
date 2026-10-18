/*
 * Host-тесты разбора `wm density` и сборки команды сброса потолка (§47.9).
 */

#include <stdio.h>
#include <string.h>

#include "../ceiling.h"

static int failures = 0;

#define CHECK(cond) do { \
	if (!(cond)) { fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static void test_parse_physical_only (void)
{
	int cur = 0, had = -1, next = 0;
	CHECK (ceiling_density_parse ("Physical density: 320\n", &cur, &had, &next) == 0);
	CHECK (cur == 320);
	CHECK (had == 0);
	CHECK (next == 321);
}

static void test_parse_with_override (void)
{
	int cur = 0, had = 0, next = 0;
	CHECK (ceiling_density_parse ("Physical density: 320\nOverride density: 480\n",
	                              &cur, &had, &next) == 0);
	CHECK (cur == 480);
	CHECK (had == 1);
	CHECK (next == 481);
}

static void test_parse_override_only (void)
{
	int cur = 0, had = 0, next = 0;
	CHECK (ceiling_density_parse ("Override density: 213\n", &cur, &had, &next) == 0);
	CHECK (cur == 213);
	CHECK (had == 1);
	CHECK (next == 214);
}

static void test_parse_bad (void)
{
	int cur = 0, had = 0, next = 0;
	CHECK (ceiling_density_parse ("", &cur, &had, &next) == -1);
	CHECK (ceiling_density_parse ("Physical density:\n", &cur, &had, &next) == -1);
	CHECK (ceiling_density_parse ("error: no display\n", &cur, &had, &next) == -1);
	CHECK (ceiling_density_parse ("Physical density: 0\n", &cur, &had, &next) == -1);
}

static void test_build_restore (void)
{
	char buf [64];

	CHECK (ceiling_build_restore (buf, sizeof (buf), 0, 320) == 0);
	CHECK (strcmp (buf, "wm density reset") == 0);

	CHECK (ceiling_build_restore (buf, sizeof (buf), 1, 480) == 0);
	CHECK (strcmp (buf, "wm density 480") == 0);

	/* override без значения (не должно быть, но код возврата обязан быть
	 * безопасным): сбрасываем, а не пишем «wm density 0» */
	CHECK (ceiling_build_restore (buf, sizeof (buf), 1, 0) == 0);
	CHECK (strcmp (buf, "wm density reset") == 0);

	CHECK (ceiling_build_restore (buf, 8, 0, 320) == -1);
}

/* Решение о сбросе: цель против уже подтверждённого потолка. Границы — те,
 * что нашлись живьём (59.94 против 60.00) и предел допуска. */
static void test_should_reset (void)
{
	CHECK (ceiling_should_reset (0, 15360) == 1);	/* потолок неизвестен */
	CHECK (ceiling_should_reset (15345, 15360) == 0);	/* 59.94 -> 60.00 */
	CHECK (ceiling_should_reset (15360, 15360) == 0);	/* та же частота */
	CHECK (ceiling_should_reset (15360, 15360 + 128) == 0);	/* ровно допуск */
	CHECK (ceiling_should_reset (15360, 15360 + 129) == 1);	/* больше допуска */
	CHECK (ceiling_should_reset (15360, 6144) == 0);	/* цель ниже */
	CHECK (ceiling_should_reset (15345, 0) == 0);	/* цель не определена */
}

static void test_parse_no_newline (void)
{
	int cur = 0, had = 0, next = 0;
	CHECK (ceiling_density_parse ("Physical density: 213", &cur, &had, &next) == 0);
	CHECK (cur == 213);
	CHECK (next == 214);
	CHECK (ceiling_density_parse ("Physical density: 20000\n", &cur, &had, &next) == -1);
}

int main (void)
{
	test_parse_physical_only ();
	test_parse_with_override ();
	test_parse_override_only ();
	test_parse_bad ();
	test_build_restore ();
	test_should_reset ();
	test_parse_no_newline ();

	if (failures)
	{
		fprintf (stderr, "%d checks failed\n", failures);
		return 1;
	}
	printf ("ceiling: all checks passed\n");
	return 0;
}
