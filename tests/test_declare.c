#include <assert.h>
#include <stdio.h>

#include "../fps_class.h"

/* Заявленная частота от драйвера — целые fps (24, 25, 30). Класс берётся тем
 * же классификатором, что и замер, поэтому «24» и «23.976» попадают в одну
 * группу и расхождением не считаются (иначе быстрый путь каждый раз «правил»
 * бы корректный режим). */
static void test_declared_class (void)
{
	/* 24 -> целый представитель группы (6144), а 23.976 -> дробный (6138):
	 * значения разные, поэтому сравнивать решения нужно ПО ГРУППЕ
	 * (fps_group_differs), иначе быстрый путь «правил» бы верный режим. */
	assert (fps_declared_class (24) == fps_classify (24.0));
	assert (fps_declared_class (24) != fps_classify (23.976));
	assert (!fps_group_differs (fps_declared_class (24), fps_classify (23.976)));
	assert (fps_declared_class (25) == fps_classify (25.0));
	/* то же и для остальных пар: заявленное — целое, замер — дробное */
	assert (fps_declared_class (30) == fps_classify (30.0));
	assert (!fps_group_differs (fps_declared_class (30), fps_classify (29.97)));
	assert (fps_declared_class (50) == fps_classify (50.0));
	assert (fps_declared_class (60) == fps_classify (60.0));
	assert (!fps_group_differs (fps_declared_class (60), fps_classify (59.94)));

	/* мусор и крайние значения — 0: быстрое решение пропускается */
	assert (fps_declared_class (0) == 0);
	assert (fps_declared_class (-1) == 0);
	assert (fps_declared_class (255) == 0);
	assert (fps_declared_class (1000) == 0);
}

/* Группы: 23.976/24 — одна (различить их замером нельзя, выбирает политика
 * дробности), 29.97/30 — одна; 24 и 30 — разные, это настоящее расхождение. */
static void test_group (void)
{
	assert (!fps_group_differs (fps_classify (23.976), fps_classify (24.0)));
	assert (!fps_group_differs (fps_classify (29.97), fps_classify (30.0)));
	assert (!fps_group_differs (fps_classify (59.94), fps_classify (60.0)));

	assert (fps_group_differs (fps_classify (24.0), fps_classify (30.0)));
	assert (fps_group_differs (fps_classify (24.0), fps_classify (25.0)));
	assert (fps_group_differs (fps_classify (50.0), fps_classify (60.0)));

	/* ноль — «сравнивать нечего», расхождением не считаем */
	assert (!fps_group_differs (0, fps_classify (24.0)));
	assert (!fps_group_differs (fps_classify (24.0), 0));
	assert (!fps_group_differs (0, 0));
}

int main (void)
{
	test_declared_class ();
	test_group ();

	printf ("declare: OK\n");
	return 0;
}
