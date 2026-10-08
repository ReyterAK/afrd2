#include <errno.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/wait.h>
#include "exec.h"

/* Предел времени на команду. Раньше команды запускались через popen и читались
 * до конца вывода: зависший app_process (например, заклинивший вызов HIDL)
 * останавливал весь цикл демона — страховка switch.timeout при этом не работала
 * вовсе (финальное ревью). Теперь у команды есть дедлайн, а по его истечении
 * убивается вся группа процессов: помощник запускается как «sh -c ...», сам
 * зовёт скрипт, а тот через exec становится app_process. */
static int g_exec_timeout_ms = EXEC_TIMEOUT_MS_DEFAULT;

/* Монотонные миллисекунды: часы, а не mstime демона — exec.c линкуется и в
 * хостовые тесты (tests/test_exec.c), где глобального времени демона нет. */
static int64_t exec_now_ms (void)
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void exec_set_timeout_ms (int ms)
{
	g_exec_timeout_ms = (ms > 0) ? ms : EXEC_TIMEOUT_MS_DEFAULT;
}

/* Общий запуск: /bin/sh -c "<cmd>" с захватом stdout+stderr.
 * Возврат: код выхода команды (0..255), -1 — не запустить/убита сигналом,
 * EXEC_TIMEOUT — не уложилась в g_exec_timeout_ms. */
static int exec_run (const char *cmd, char *out, size_t out_sz)
{
	if (out_sz)
		out [0] = 0;

	char full [EXEC_CMD_MAX];
	int r = snprintf (full, sizeof (full), "( %s ) 2>&1", cmd);
	if (r < 0 || (size_t)r >= sizeof (full))
		return -1;

	int fds [2];
	if (pipe (fds) != 0)
		return -1;

	pid_t pid = fork ();
	if (pid < 0)
	{
		close (fds [0]);
		close (fds [1]);
		return -1;
	}

	if (pid == 0)
	{
		/* Своя группа процессов: по дедлайну убиваем всё дерево, иначе
		 * остались бы жить app_process и его HAL-вызовы. */
		setpgid (0, 0);

		dup2 (fds [1], STDOUT_FILENO);
		dup2 (fds [1], STDERR_FILENO);
		close (fds [0]);
		close (fds [1]);

		execl ("/bin/sh", "sh", "-c", full, (char *)NULL);
		_exit (127);
	}

	close (fds [1]);

	size_t used = 0;
	int64_t deadline = exec_now_ms () + g_exec_timeout_ms;
	bool timed_out = false;
	int status = 0;

	for (;;)
	{
		int64_t left = deadline - exec_now_ms ();
		if (left <= 0)
		{
			timed_out = true;
			break;
		}

		struct pollfd pfd = { .fd = fds [0], .events = POLLIN };
		int pr = poll (&pfd, 1, (int)left);
		if (pr < 0)
		{
			if (errno == EINTR)
				continue;

			timed_out = true;
			break;
		}
		if (pr == 0)
		{
			timed_out = true;
			break;
		}

		char tmp [512];
		ssize_t n = read (fds [0], tmp, sizeof (tmp));
		if (n < 0)
		{
			if (errno == EINTR)
				continue;

			timed_out = true;
			break;
		}
		if (n == 0)
			break;	/* конец вывода: команда закрыла поток */

		/* Вывод за пределами буфера выбрасываем, но читаем до конца: иначе
		 * команда заблокировалась бы на заполненной трубе. */
		for (ssize_t i = 0; (i < n) && (used + 1 < out_sz); i++)
			out [used++] = tmp [i];
	}

	close (fds [0]);

	if (timed_out)
	{
		/* Сначала группе, потом процессу: команда могла уже выйти, а её
		 * потомки (app_process) — остаться. */
		killpg (pid, SIGKILL);
		kill (pid, SIGKILL);
	}

	while (waitpid (pid, &status, 0) < 0)
		if (errno != EINTR)
			break;

	if (out_sz)
		out [used] = 0;

	if (timed_out)
		return EXEC_TIMEOUT;

	return WIFEXITED (status) ? WEXITSTATUS (status) : -1;
}

int exec_status (const char *cmd, char *out, size_t out_sz)
{
	return exec_run (cmd, out, out_sz);
}

int exec_capture (const char *cmd, char *out, size_t out_sz)
{
	int rc = exec_run (cmd, out, out_sz);

	/* Прежний контракт: 0 — команда отработала (её код возврата вызывающего
	 * здесь не интересует), -1 — не запустить или убита, EXEC_TIMEOUT —
	 * не уложилась в дедлайн. */
	return (rc < 0) ? rc : 0;
}
