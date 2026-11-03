/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#define __USE_GNU
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <errno.h>

#include "afrd.h"
#include "burst.h"

const char *g_version = "1.2.0";
const char *g_ver_sfx = "";
const char *g_bdate = BDATE;
const char *g_config = "/etc/afrd.ini";
const char *g_pidfile =
#ifdef ANDROID
	// android has no standard directory for PID files...
	// so use /dev/pid/ on tmpfs
	"/dev/run/afrd.pid";
#else
	"/var/run/afrd.pid";
#endif
const char *g_program;
int g_verbose = 0;
int g_daemon = 0;
int g_kill_daemon = 0;
volatile int g_shutdown = 0;
static int g_logh = -1;
static bool g_logfn_firstuse = true;

// the global config
struct cfg_struct *g_cfg = NULL;

static void show_version ()
{
	printf ("afr daemon version %s%s built %s\n", g_version, g_ver_sfx, g_bdate);
}

static void show_help (char *const *argv)
{
	show_version ();
	printf ("usage: %s [options] [config-file]\n", argv [0]);
	printf ("	--burst SEC	run one AFR session for SEC seconds and exit\n"
	        "			(sends the command to a running daemon;\n"
	        "			 with no daemon, runs the session itself)\n"
	        "	--burst 0	cancel a running session\n");
	printf ("	-D	daemonize the program\n");
	printf ("	-p FILE	write PID to file when running as daemon\n");
	printf ("	-k	kill the running daemon (can be used with -D)\n");
	printf ("	-l FILE	write the log to FILE (imposes -vvv)\n");
	printf ("	-s	display running daemon stats\n");
	printf ("	-h	display this help\n");
	printf ("	-v	verbose info about what's cooking\n");
	printf ("	-V	display program version\n");
}

void trace_log (const char *logfn)
{
	if (g_logh >= 0) {
		close (g_logh);
		g_logh = -1;
	}

	if (!logfn)
		return;

	/* on first open rename old log to *~ */
	if (g_logfn_firstuse) {
		g_logfn_firstuse = false;
		char backup_logfn [300];
		snprintf (backup_logfn, sizeof (backup_logfn), "%s~", logfn);
		rename (logfn, backup_logfn);
	}

	g_logh = open (logfn, O_WRONLY | O_APPEND | O_CLOEXEC | O_CREAT /*| O_SYNC*/, 0644);
	if (g_logh < 0) {
		fprintf (stderr, "%s: failed to open log file %s", g_program, logfn);
		return;
	}
}

void trace_sync ()
{
	if (g_logh >= 0)
		fdatasync (g_logh);
}

void trace (int level, const char *format, ...)
{
	va_list argp;
	struct tm tm;
	struct timeval tv;
	bool write_logh = (g_logh >= 0) && (level <= 2);
	bool write_stdout = !g_daemon && (level <= g_verbose);

	if (!(write_logh || write_stdout))
		return;

	if (gettimeofday (&tv, NULL) < 0)
		return;

	localtime_r (&tv.tv_sec, &tm);

	char buff [1024];
	int n = snprintf (buff, sizeof (buff), "%02d:%02d:%02d.%03d ",
	                  tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(tv.tv_usec / 1000));

	va_start (argp, format);
	n += vsnprintf (buff + n, sizeof (buff) - n, format, argp);
	va_end (argp);

	if (write_logh)
		write (g_logh, buff, n);
	if (write_stdout)
		write (STDOUT_FILENO, buff, n);
}

/* Завершение (SIGTERM/SIGINT): основной цикл вернёт исходный режим и снимет
 * флаг backdoor. Второй такой сигнал — немедленный выход: значит, цикл не
 * ответил и завершение затянулось. */
static void on_term (int sig)
{
	g_shutdown = 1;
	signal (sig, SIG_DFL);
}

static void signal_emerg (int sig)
{
	// shit happened, just remove files and quit
	/* Здесь нельзя читать pid-файл: open/read в обработчике сигнала не
	 * async-signal-safe. Удаляем как раньше — путь аварийный. */
	if (g_daemon)
		unlink (g_pidfile);
	afrd_emerg ();

	// restore original signal handler
	signal (sig, SIG_DFL);
}

int load_config (const char *config)
{
	int ret;

	trace (1, "loading config file '%s'\n", config);

	g_cfg = cfg_init ();

	if ((ret = cfg_load (g_cfg, config)) < 0) {
		cfg_free (g_cfg);
		g_cfg = NULL;
		trace (0, "failed to load config file '%s'\n", config);
		return ret;
	}

	trace (1, "\tsuccess\n");
	g_config = config;

	return 0;
}

static bool pid_is_afrd (pid_t pid);

// returns either the PID of running daemon, or
// -1 if PID file does not exist, or -2 if it
// exists but the contents are wrong.
static pid_t daemon_pid ()
{
	int h;
	pid_t pid = -1;

	h = open (g_pidfile, O_RDONLY);
	if (h >= 0) {
		int n;
		char tmp [11];

		pid = -2;
		n = read (h, tmp, sizeof (tmp) - 1);
		if (n > 0) {
			tmp [n] = 0;
			pid = strtoul (tmp, NULL, 0);
			// check if PID is valid and belongs to our daemon: после kill -9
			// pid мог достаться чужому процессу
			if ((pid < 1) || kill (pid, 0) || !pid_is_afrd (pid))
				pid = -2;
		}

		close (h);
	}

	return pid;
}

/* Это процесс нашего демона? PID из файла мог достаться чужому процессу
 * (kill -9, переиспользование pid), и SIGINT ушёл бы ему. */
static bool pid_is_afrd (pid_t pid)
{
	char path [64];
	char buf [256];

	snprintf (path, sizeof (path), "/proc/%d/cmdline", (int) pid);
	int h = open (path, O_RDONLY | O_CLOEXEC);
	if (h < 0)
		return false;

	ssize_t n = read (h, buf, sizeof (buf) - 1);
	close (h);
	if (n <= 0)
		return false;

	buf [n] = 0;
	return strstr (buf, "afrd") != NULL;
}

/* Что лежит в pid-файле сейчас (без проверки живости). */
static bool pidfile_read (pid_t *out)
{
	int h = open (g_pidfile, O_RDONLY | O_CLOEXEC);
	if (h < 0)
		return false;

	char tmp [11];
	ssize_t n = read (h, tmp, sizeof (tmp) - 1);
	close (h);

	if (n <= 0)
		return false;

	tmp [n] = 0;
	*out = (pid_t) strtoul (tmp, NULL, 0);
	return true;
}

static int kill_daemon ()
{
	pid_t pid = daemon_pid ();
	if (pid == -1) {
		fprintf (stderr, "%s: failed to read PID from file '%s'\n",
			g_program, g_pidfile);
	} else if (pid == -2) {
		unlink (g_pidfile);
		fprintf (stderr, "%s: PID file exists, but daemon is dead\n",
			g_program);
	} else if (!pid_is_afrd (pid)) {
		/* pid из файла принадлежит не демону: не трогаем чужой процесс и
		 * файл не удаляем — пусть разбирается человек (нашло ревью API). */
		fprintf (stderr, "%s: PID %d in '%s' is not afrd — not killing\n",
			g_program, (int) pid, g_pidfile);
	} else if (kill (pid, SIGINT)) {
		fprintf (stderr, "%s: failed to kill daemon PID %d\n",
			g_program, pid);
	} else {
		/* Ждём до 10 с: на выходе демон возвращает удержанный режим, и это
		 * может занять секунды. Раньше ждали 2 с и удаляли pid-файл
		 * безусловно — старый демон затем удалял файлы уже нового
		 * экземпляра (pid и shmem), и поднимались два демона. */
		bool dead = false;
		for (int i = 0; i < 400; i++) {
			usleep (25000);
			if (kill (pid, 0) && (errno == ESRCH)) {
				dead = true;
				break;
			}
		}

		if (!dead) {
			fprintf (stderr, "%s: daemon PID %d did not exit in 10 s — "
			         "not starting another one\n", g_program, (int) pid);
			return EXIT_FAILURE;
		}

		/* Удаляем файл, только если в нём всё ещё тот же pid: новый демон
		 * мог уже успеть записать свой. */
		pid_t cur = -1;
		if (pidfile_read (&cur) && (cur != pid)) {
			fprintf (stderr, "%s: PID file already belongs to %d — leave it\n",
			         g_program, (int) cur);
		} else
			unlink (g_pidfile);

		return EXIT_SUCCESS;
	}

	return EXIT_FAILURE;
}

static void switch_namespace (int pid)
{
#ifdef ANDROID
#if __ANDROID_API__ >= 21
	char tmp [64];
	int h;

	snprintf (tmp, sizeof (tmp), "/proc/%d/ns/cgroup", pid);
	h = open (tmp, O_RDONLY);
	if (h >= 0) {
		(void) setns (h, CLONE_NEWCGROUP);
		close (h);
	}

	snprintf (tmp, sizeof (tmp), "/proc/%d/ns/mnt", pid);
	h = open (tmp, O_RDONLY);
	if (h >= 0) {
		(void) setns (h, CLONE_NEWNS);
		close (h);
	}
#else
#error "afrd is not designed to run on Android API level < 21"
#endif
#endif
}

static void daemonize ()
{
	/* switch to root namespace */
	switch_namespace (1);

	/* check PID */
	pid_t pid = daemon_pid ();
	if (pid > 0) {
		fprintf (stderr, "%s: daemon is already running with PID %d\n",
			g_program, pid);
		exit (EXIT_FAILURE);
	}

	pid = fork ();
	if (pid < 0) {
		fprintf (stderr, "%s: can't demonize, aborting\n", g_program);
		exit (EXIT_FAILURE);
	}

	if (pid != 0) {
		/* if directory for pid file does not exist, try to create it */
		char *pidfile = strdup (g_pidfile);
		char *dn = dirname (pidfile);
		if (*dn && (access (dn, F_OK) != 0))
			mkdir (dn, 0755);
		else
			// ensure sane access rights to /dev/run
			chmod (dn, 0755);
		free (pidfile);

		int h = open (g_pidfile, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
		if (h >= 0) {
			char tmp [10];
			write (h, tmp, snprintf (tmp, sizeof (tmp), "%d", pid));
			close (h);
		} else
			fprintf (stderr, "%s: failed to write PID file '%s'\n",
				g_program, g_pidfile);

		/* successfuly daemonized */
		exit (EXIT_SUCCESS);
	}

	/* Continuing in child; leave the console.
	 *
	 * Закрывать дескрипторы нельзя: на Android 6-9 `dumpsys` и другие
	 * системные утилиты — это обёртки над `app_process`, которым для
	 * инициализации нужны рабочие стандартные дескрипторы. С закрытыми 0/1/2
	 * такой процесс не соединяется с servicemanager и печатает
	 * "Can't find service: activity" — на этих прошивках демон не мог
	 * определить пакет приложения на переднем плане (проверено на ATV9,
	 * стенд .76), из-за чего switch.skip.pkg молча не действовал.
	 * Перенаправление в /dev/null — канонический демонизм. */
	{
		int dn = open ("/dev/null", O_RDWR);
		if (dn >= 0) {
			(void) dup2 (dn, 0);
			(void) dup2 (dn, 1);
			(void) dup2 (dn, 2);
			if (dn > 2)
				close (dn);
		}
		else {
			/* /dev/null недоступен — ведём себя как раньше: закрываем,
			 * чтобы не удерживать чужие pipe/tty */
			close (0);
			close (1);
			close (2);
		}
	}

	/* set priority a bit higher than normal -
	   we're kind of time-critical process */
	setpriority (PRIO_PROCESS, getpid (), -16);
}

/* Отправить команду `burst <ms>` работающему демону по UDP API.
 * Возврат: 0 — демон ответил; -1 — ответа нет (демона нет или он не
 * отвечает); ответ печатается в out. */
#define AFRD_API_PORT	50505
/* Демон во время смены режима занят до 4 с (DISPSERVE_REPLY_MS в dispserve.c)
 * и сокет в это время не читает: 700 мс давали ложное «daemon … did not answer
 * the API» при живой команде, которая выполнялась следом (P2 прохода 2).
 * Ждём с запасом на занятый цикл. */
#define API_REPLY_MS	4500

static int api_send (const char *cmd, char *out, size_t out_sz)
{
	int fd = socket (AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -1;

	struct sockaddr_in sa;
	memset (&sa, 0, sizeof (sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons (AFRD_API_PORT);
	sa.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	if (sendto (fd, cmd, strlen (cmd), 0, (struct sockaddr *)&sa, sizeof (sa)) < 0)
	{
		close (fd);
		return -1;
	}

	/* Ответ короткий и приходит сразу, но цикл демона может быть занят сменой
	 * режима до 4 с — ждём API_REPLY_MS. */
	struct pollfd pfd = { fd, POLLIN, 0 };
	int rc = -1;
	if (poll (&pfd, 1, API_REPLY_MS) > 0)
	{
		int n = recv (fd, out, out_sz - 1, 0);
		if (n > 0)
		{
			out [n] = 0;
			rc = 0;
		}
	}

	close (fd);
	return rc;
}

static int burst_send_command (int ms, char *out, size_t out_sz)
{
	char cmd [32];
	snprintf (cmd, sizeof (cmd), "burst %d\n", ms);
	return api_send (cmd, out, out_sz);
}

static void display_stats ()
{
	if (!shmem_init (true))
		return;

	if (!shmem_read ()) {
		trace (0, "failed to read shared memory\n");
	} else {
		printf ("AFRd2 version: %d.%d.%d built %s\n",
			g_afrd_stats.ver_major, g_afrd_stats.ver_minor, g_afrd_stats.ver_micro,
			g_afrd_stats.bdate);
		printf ("AFRd2 is enabled: %s\n", g_afrd_stats.enabled ? "yes" : "no");
		printf ("Display refresh rate is switched: %s\n", g_afrd_stats.switched ? "yes" : "no");
		printf ("Display is blackened: %s\n", g_afrd_stats.blackened ? "yes" : "no");
		printf ("Current display refresh rate: %u.%02uHz\n",
			g_afrd_stats.current_hz >> 8, (100 * (g_afrd_stats.current_hz & 255)) >> 8);
		printf ("Original display refresh rate: %u.%02uHz\n",
			g_afrd_stats.original_hz >> 8, (100 * (g_afrd_stats.original_hz & 255)) >> 8);
		/* Состояние сеанса живёт в процессе демона, а не в shmem: берём его
		 * из ответа API status (те же три строки, что видит любой клиент). */
		char reply [512] = { 0 };
		if (api_send ("status\n", reply, sizeof (reply)) == 0)
		{
			const char *b = strstr (reply, "burst:");
			if (b)
				printf ("AFR session: %s", b);
		}
	}

	shmem_fini ();
}

int main (int argc, char *const *argv)
{
	int ret;

	// ensure a sane umask so that user processes can read our files
	umask (022);

	g_program = argv [0];

	/* --burst <сек> — сеанс «AFR по требованию» (журнал §84). Разбирается
	 * до getopt и ВЫРЕЗАЕТСЯ из argv: getopt длинных опций не знает, а на
	 * "--burst" он разбирает "-" как опцию, дальше "b","u","r","s" — и "-s"
	 * печатал статистику вместо запуска сеанса (живая находка). */
	int burst_sec = -1;
	{
		/* argv приходит как char *const *: копируем указатели в свою
		 * таблицу — иначе компилятор запрещает запись. */
		static char *argv_own [64];
		int w = 1;
		argv_own [0] = (char *) argv [0];
		for (int i = 1; i < argc; i++)
		{
			if (!strcmp (argv [i], "--burst"))
			{
				burst_sec = (i + 1 < argc) ? atoi (argv [i + 1]) : 0;
				i++;			/* пропускаем и значение */
				continue;
			}
			if (!strncmp (argv [i], "--burst=", 8))
			{
				burst_sec = atoi (argv [i] + 8);
				continue;
			}
			if (w < (int) ARRAY_SIZE (argv_own) - 1)
				argv_own [w++] = (char *) argv [i];
		}
		argv_own [w] = NULL;
		argv = argv_own;
		argc = w;
	}

	while ((ret = getopt (argc, argv, "Dp:kl:shvV")) >= 0)
		switch (ret) {
			case 'D':
				g_daemon = 1;
				break;

			case 'p':
				g_pidfile = optarg;
				break;

			case 'k':
				g_kill_daemon = 1;
				break;

			case 'l':
				trace_log (optarg);
				break;

			case 's':
				display_stats ();
				return 0;

			case 'v':
				g_verbose++;
				break;

			case 'h':
				show_help (argv);
				return EXIT_FAILURE;

			case 'V':
				show_version ();
				return EXIT_FAILURE;
		}

	if (g_daemon)
		// switch to root namespace
		switch_namespace (1);

	if (g_kill_daemon) {
		ret = kill_daemon ();
		if (!g_daemon)
			return ret;
	}

	/* --burst: сначала команда живущему демону. Если демона нет — сеанс
	 * запускается этим же процессом (форма B): второго демона рядом не
	 * появляется, потому что при живом (по pid-файлу) процессе мы отказываемся
	 * стартовать, а не идём напролом. */
	if (burst_sec >= 0)
	{
		int ms = (burst_sec == 0) ? 0 : burst_clamp_ms (burst_sec * 1000);
		char reply [256] = { 0 };

		if (burst_send_command (ms, reply, sizeof (reply)) == 0)
		{
			printf ("%s", reply);
			return EXIT_SUCCESS;
		}

		pid_t dp = daemon_pid ();
		if (dp > 0)
		{
			fprintf (stderr, "%s: daemon (pid %d) is running but did not answer "
			         "the API — not starting a second one\n", g_program, (int)dp);
			return EXIT_FAILURE;
		}

		if (ms <= 0)
		{
			fprintf (stderr, "%s: no running daemon, nothing to cancel\n", g_program);
			return EXIT_FAILURE;
		}

		/* Демона нет: сеанс ведёт этот процесс. */
		afrd_burst_self (ms);
	}

	if (g_daemon)
		daemonize ();

	// load the config files mentioned on command line, until one loads
	while (optind < argc)
		if ((ret = load_config (argv [optind++])) == 0)
			break;

	signal (SIGHUP, SIG_IGN);
	signal (SIGQUIT, on_term);	/* как SIGTERM/SIGINT: завершение с возвратом режима */
	signal (SIGINT, on_term);
	signal (SIGTERM, on_term);

	signal (SIGFPE, signal_emerg);
	signal (SIGILL, signal_emerg);
	signal (SIGSEGV, signal_emerg);

	for (;;) {
		/* Любой ненулевой код afrd_init — отказ инициализации: раньше
		 * EPERM (нет uevent) проходил проверку `< 0`, и демон жил без
		 * uevent, а затем выходил с кодом 0 (P2 прохода 2). */
		if ((ret = afrd_init ()) != 0) {
			ret = EXIT_FAILURE;
			break;
		}
		ret = afrd_run ();
		afrd_fini ();

		if (ret <= 0)
			break;
	}

	if (g_cfg)
		cfg_free (g_cfg);

	if (g_daemon) {
		/* Только свой файл: при наложении перезапусков здесь можно удалить
		 * pid уже нового демона. */
		pid_t cur = -1;
		if (pidfile_read (&cur) && (cur == getpid ()))
			unlink (g_pidfile);
	}

	return ret;
}
