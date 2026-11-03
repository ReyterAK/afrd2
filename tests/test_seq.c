#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../seq.h"

/* Host-тест последовательности «сверили частоту → повторили → сдались».
 * Гоняет фикстуры из tests/fixtures/seq — они повторяют то, что демон видел в
 * логах тестера и на стенде. Каждая строка фикстуры — одно событие:
 *
 *   settle <vhz> <expect> <mid_hz> <mid_is_double> <действие>
 *   retryfail <mid_applied> <mid_is_double> <действие>
 *   newvideo | noteapplied | gaveup <0|1>
 *
 * Действия: nothing, recheck, retry, keep, restore, format.
 *
 * Частоты в фикстурах — «человеческие» целые Гц: seq_settled сравнивает группы
 * через fps_group_differs, поэтому 23.976 и 24 считаются одной группой и в
 * тесте это тоже проверяется. */

static const char *act_name (seq_action_t a)
{
	switch (a)
	{
		case SEQ_NOTHING: return "nothing";
		case SEQ_RECHECK: return "recheck";
		case SEQ_RETRY: return "retry";
		case SEQ_KEEP_MID: return "keep";
		case SEQ_RESTORE: return "restore";
		case SEQ_SET_FORMAT: return "format";
	}
	return "?";
}

static seq_action_t act_parse (const char *s)
{
	if (!strcmp (s, "nothing")) return SEQ_NOTHING;
	if (!strcmp (s, "recheck")) return SEQ_RECHECK;
	if (!strcmp (s, "retry")) return SEQ_RETRY;
	if (!strcmp (s, "keep")) return SEQ_KEEP_MID;
	if (!strcmp (s, "restore")) return SEQ_RESTORE;
	if (!strcmp (s, "format")) return SEQ_SET_FORMAT;
	fprintf (stderr, "seq: неизвестное действие «%s» в фикстуре\n", s);
	return SEQ_NOTHING;
}

static int fails;

#define CHECK(cond, fmt, ...) do { \
	if (!(cond)) { \
		fprintf (stderr, "seq: " fmt "\n", ##__VA_ARGS__); \
		fails++; \
	} \
} while (0)

static void run_fixture (const char *name)
{
	char path [256];
	snprintf (path, sizeof (path), "tests/fixtures/seq/%s.txt", name);

	FILE *f = fopen (path, "r");
	if (!f)
	{
		fprintf (stderr, "seq: нет фикстуры %s\n", path);
		fails++;
		return;
	}

	seq_t st = {SEQ_IDLE, false};
	char line [256];
	int n = 0;

	while (fgets (line, sizeof (line), f))
	{
		n++;
		char a [32] = "", b [32] = "";
		int vhz = 0, expect = 0, mid = 0, dbl = 0;

		char *p = line;
		while ((*p == ' ') || (*p == '\t')) p++;
		if ((*p == '#') || (*p == '\n') || !*p)
			continue;

		if (!strncmp (p, "settle ", 7))
		{
			if (sscanf (p + 7, "%d %d %d %d %31s", &vhz, &expect, &mid, &dbl, a) != 5)
			{
				fprintf (stderr, "seq: %s:%d не разобрана строка settle\n", name, n);
				fails++;
				continue;
			}
			seq_step_t r = seq_settled (&st, vhz, expect, mid, dbl != 0);
			CHECK (r.action == act_parse (a),
			       "%s:%d settle %d/%d mid %d double %d: ждали %s, получили %s",
			       name, n, vhz, expect, mid, dbl, a, act_name (r.action));
		}
		else if (!strncmp (p, "retryfail ", 10))
		{
			int mid_applied = 0, mid_double = 0;
			if (sscanf (p + 10, "%d %d %31s", &mid_applied, &mid_double, b) != 3)
			{
				fprintf (stderr, "seq: %s:%d не разобрана строка retryfail\n", name, n);
				fails++;
				continue;
			}
			seq_action_t r = seq_retry_result (&st, mid_applied != 0, mid_double != 0);
			CHECK (r == act_parse (b),
			       "%s:%d retryfail mid_applied %d double %d: ждали %s, получили %s",
			       name, n, mid_applied, mid_double, b, act_name (r));
		}
		else if (!strncmp (p, "newvideo", 8))
			seq_new_video (&st);
		else if (!strncmp (p, "noteapplied", 11))
			seq_note_applied (&st);
		else if (!strncmp (p, "gaveup ", 7))
		{
			int want = atoi (p + 7);
			CHECK (seq_gave_up (&st) == (want != 0),
			       "%s:%d gaveup: ждали %d, получили %d",
			       name, n, want, seq_gave_up (&st) ? 1 : 0);
		}
		else
		{
			fprintf (stderr, "seq: %s:%d неизвестное событие «%s»\n", name, n, p);
			fails++;
		}
	}

	fclose (f);
}

int main (void)
{
	/* Числовые случаи, которые в фикстурах не читаются: группа частот.
	 * 23.976 и 24 — одна группа (6139 и 6144 в 24.8): расхождения нет, значит
	 * и повтора не будет (I3). */
	seq_t st = {SEQ_IDLE, false};
	CHECK (seq_settled (&st, 6139, 6144, 0, false).action == SEQ_SET_FORMAT,
	       "23.976 против 24 — одна группа, ждали format");

	/* Сверять нечего (узел vinfo не читается): формат ставится, повтора нет. */
	st = (seq_t) {SEQ_IDLE, false};
	CHECK (seq_settled (&st, 0, 6400, 0, false).action == SEQ_SET_FORMAT,
	       "vinfo недоступен — ждали format, а не повтор");

	/* Два одинаковых расхождения подряд без нового видео дают ровно один
	 * повтор (I1): после сдачи — ничего. */
	st = (seq_t) {SEQ_IDLE, false};
	CHECK (seq_settled (&st, 15360, 6400, 12800, true).action == SEQ_RECHECK, "1-й заход");
	CHECK (seq_settled (&st, 15360, 6400, 12800, true).action == SEQ_RETRY, "2-й заход");
	CHECK (seq_settled (&st, 12800, 6400, 12800, true).action == SEQ_KEEP_MID, "3-й заход");
	CHECK (seq_settled (&st, 15360, 6400, 12800, true).action == SEQ_NOTHING,
	       "после сдачи повтора быть не должно");
	CHECK (seq_gave_up (&st), "сдача не выставлена");

	/* Применение режима по решению снимает состояние проверки, но не сдачу. */
	seq_note_applied (&st);
	CHECK (seq_gave_up (&st), "noteapplied не должен снимать сдачу");
	CHECK (seq_settled (&st, 15360, 6400, 12800, true).action == SEQ_NOTHING,
	       "после применения и сдачи — ничего");

	run_fixture ("ok-25");
	run_fixture ("no-doubling");
	run_fixture ("retry-fail");
	run_fixture ("tester-25hz");

	if (fails)
	{
		fprintf (stderr, "seq: %d проверок не прошло\n", fails);
		return 1;
	}

	printf ("seq: OK\n");
	return 0;
}
