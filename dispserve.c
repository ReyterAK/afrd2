/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * Постоянный помощник смены режима (Android 10/11): см. dispserve.h.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "afrd.h"
#include "dispserve.h"

/* Сколько ждать ответа на одну смену. HAL на проверенных стендах тратит до
 * 1.1 с; запас нужен для медленного HDMI-линка и нагрузки. */
#define DISPSERVE_REPLY_MS	4000

/* Сколько раз за жизнь демона разрешено поднимать помощник заново. После
 * этого работаем разовым запуском: он медленнее, зато не зависит от здоровья
 * долгоживущего процесса. */
#define DISPSERVE_STARTS_MAX	3

static pid_t g_pid = -1;
static int g_to_child = -1;	/* stdin помощника */
static int g_from_child = -1;	/* stdout помощника */
static int g_starts = 0;
static int g_dead = 0;		/* помощник отказал — больше не пробуем */

static void close_fd (int *fd)
{
	if (*fd >= 0)
	{
		close (*fd);
		*fd = -1;
	}
}

void dispserve_stop (void)
{
	close_fd (&g_to_child);
	close_fd (&g_from_child);

	if (g_pid > 0)
	{
		kill (g_pid, SIGKILL);
		waitpid (g_pid, NULL, 0);
		g_pid = -1;
	}
}

/* Читать строку ответа с дедлайном. Возврат: 0 — строка получена, -1 — нет
 * (таймаут, EOF, ошибка). */
static int read_line (char *buf, size_t n)
{
	size_t len = 0;

	while (len + 1 < n)
	{
		struct pollfd pfd = { .fd = g_from_child, .events = POLLIN };
		int pr = poll (&pfd, 1, DISPSERVE_REPLY_MS);

		if (pr == 0)
			return -1;		/* нет ответа */
		if (pr < 0)
		{
			if (errno == EINTR)
				continue;
			return -1;
		}

		char c;
		ssize_t r = read (g_from_child, &c, 1);
		if (r <= 0)
			return -1;		/* помощник закрыл поток (умер) */

		if (c == '\n')
		{
			buf [len] = 0;
			return 0;
		}

		buf [len++] = c;
	}

	return -1;
}

/* Запустить помощник и дождаться приветствия. Возврат: 0 — готов. */
static int start (const char *script)
{
	if (g_starts >= DISPSERVE_STARTS_MAX)
		return -1;

	g_starts++;

	/* Запись в трубу умершего помощника даёт SIGPIPE, а он по умолчанию
	 * убивает процесс: без этого демон падал бы вместе с помощником (живая
	 * находка 18.10 — после kill помощника демон исчез). Теперь EPIPE просто
	 * возвращает ошибку, и мы уходим на разовый запуск. */
	signal (SIGPIPE, SIG_IGN);

	int to_child [2], from_child [2];
	if ((pipe (to_child) != 0) || (pipe (from_child) != 0))
	{
		if (to_child [0] >= 0)
		{
			close (to_child [0]);
			close (to_child [1]);
		}
		return -1;
	}

	pid_t pid = fork ();
	if (pid < 0)
	{
		close (to_child [0]);
		close (to_child [1]);
		close (from_child [0]);
		close (from_child [1]);
		return -1;
	}

	if (pid == 0)
	{
		/* Своя группа процессов: помощник и его потомки уходят вместе. */
		setpgid (0, 0);

		dup2 (to_child [0], STDIN_FILENO);
		dup2 (from_child [1], STDOUT_FILENO);

		int null = open ("/dev/null", O_RDWR);
		if (null >= 0)
			dup2 (null, STDERR_FILENO);

		close (to_child [0]);
		close (to_child [1]);
		close (from_child [0]);
		close (from_child [1]);

		/* Обёртка помощника: тот же файл, что раскладывают приложение и
		 * Magisk-модуль; режим --serve запускает постоянный процесс. */
		execl ("/system/bin/sh", "sh", script, "--serve", (char *) NULL);
		_exit (127);
	}

	setpgid (pid, pid);
	close (to_child [0]);
	close (from_child [1]);

	g_pid = pid;
	g_to_child = to_child [1];
	g_from_child = from_child [0];

	char hello [128];
	if (read_line (hello, sizeof (hello)) != 0)
	{
		trace (0, "помощник режима не поздоровался — работаю разовым запуском\n");
		dispserve_stop ();
		return -1;
	}

	if (strcmp (hello, "ready") != 0)
	{
		trace (0, "помощник режима не готов (%s) — работаю разовым запуском\n", hello);
		dispserve_stop ();
		return -1;
	}

	trace (1, "помощник смены режима запущен (pid %d)\n", (int) pid);
	return 0;
}

int dispserve_apply (const char *script, const char *mode_name)
{
	if (g_dead || !script || !*script || !mode_name || !*mode_name)
		return -1;

	if ((g_pid < 0) && (start (script) != 0))
	{
		if (g_starts >= DISPSERVE_STARTS_MAX)
			g_dead = 1;
		return -1;
	}

	char req [96];
	int len = snprintf (req, sizeof (req), "%s\n", mode_name);
	if ((len <= 0) || ((size_t) len >= sizeof (req)))
		return -1;

	/* Пишем одной операцией: помощник читает построчно. */
	ssize_t w = write (g_to_child, req, (size_t) len);
	if (w != len)
	{
		trace (0, "помощник режима не принял запрос — разовый запуск\n");
		dispserve_stop ();
		return -1;
	}

	char rsp [192];
	if (read_line (rsp, sizeof (rsp)) != 0)
	{
		/* Молчание или смерть процесса: перезапустим при следующей смене,
		 * а эту выполним разовым запуском. */
		trace (0, "помощник режима не ответил за %d мс — разовый запуск\n",
		       DISPSERVE_REPLY_MS);
		dispserve_stop ();
		return -1;
	}

	if (strcmp (rsp, "ok") == 0)
		return 0;

	trace (1, "помощник режима отказал: %s\n", rsp);

	/* "err:3" — сервис пропал: помощник бесполезен, дальше разовый запуск
	 * (он сам перезапускает system_control при отказе регистрации). */
	if (strncmp (rsp, "err:3", 5) == 0)
	{
		dispserve_stop ();
		return -1;
	}

	return -2;
}
