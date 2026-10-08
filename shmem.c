/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * Shared memory functions for afrd
 */

#include "afrd.h"
#include "crc32.h"

#include <unistd.h>
#include <libgen.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

static int g_shmem_h;
static char *g_shmem_path;
// pointer to shared memory
static afrd_shmem_t *g_shmem;
// true if shmem is open for reading
static bool g_shmem_read;
// local copy of the statistics
afrd_shmem_t g_afrd_stats;

// Путь файла разделяемой памяти (рядом с pid-файлом). Возврат: строка во
// внутреннем буфере — живёт до следующего вызова.
static const char *shmem_file_path (void)
{
	static char path [200];

	// place shared memory file in same dir where pid file is
	char *pidfile = strdup (g_pidfile);
	char *dn = dirname (pidfile);
	if (*dn && (access (dn, F_OK) != 0))
		mkdir (dn, 0755);
	snprintf (path, sizeof (path), "%s/afrd.ipc", dn);
	free (pidfile);

	return path;
}

bool shmem_peek (afrd_shmem_t *out)
{
	if (!out)
		return false;

	memset (out, 0, sizeof (*out));

	crc32_init ();

	int h = open (shmem_file_path (), O_RDONLY | O_CLOEXEC);
	if (h < 0)
		return false;

	afrd_shmem_t tmp;
	ssize_t n = read (h, &tmp, sizeof (tmp));
	close (h);

	if (n != (ssize_t) sizeof (tmp))
		return false;
	if (tmp.size != sizeof (afrd_shmem_t))
		return false;
	if (tmp.crc32 != tmp.crc32_copy)
		return false;
	if (tmp.crc32 != crc32_finish (crc32_update (CRC32_START,
			(uint8_t *)&tmp.size,
			sizeof (afrd_shmem_t) - sizeof (uint32_t) * 2)))
		return false;

	*out = tmp;
	return true;
}

bool shmem_init (bool read)
{
	g_shmem_read = read;

	crc32_init ();

	const char *shmem_path = shmem_file_path ();

	if (g_shmem_path)
		free (g_shmem_path);
	g_shmem_path = strdup (shmem_path);

	if (read)
		g_shmem_h = open (g_shmem_path, O_RDONLY | O_CLOEXEC);
	else
		g_shmem_h = open (g_shmem_path, O_CREAT | O_RDWR | O_CLOEXEC, 0644);

	if (g_shmem_h < 0) {
		trace (0, "failed to open shared memory %s\n", g_shmem_path);
		shmem_fini ();
		return false;
	}

	memset (&g_afrd_stats, 0, sizeof (afrd_shmem_t));
	if (!read) {
		g_afrd_stats.size = sizeof (afrd_shmem_t);
		strncpy (g_afrd_stats.bdate, g_bdate, sizeof (g_afrd_stats.bdate));
		strncpy (g_afrd_stats.ver_sfx, g_ver_sfx, sizeof (g_afrd_stats.ver_sfx));

		// we can safely assume version format "%d.%d.%d"
		char *cur = (char *)g_version;
		g_afrd_stats.ver_major = strtoul (cur, &cur, 10);
		cur++;
		g_afrd_stats.ver_minor = strtoul (cur, &cur, 10);
		cur++;
		g_afrd_stats.ver_micro = strtoul (cur, &cur, 10);

		write (g_shmem_h, &g_afrd_stats, sizeof (afrd_shmem_t));
		fsync (g_shmem_h);
	}

	g_shmem = (afrd_shmem_t *)mmap (NULL, sizeof (afrd_shmem_t),
		PROT_READ | (read ? 0 : PROT_WRITE), MAP_SHARED, g_shmem_h, 0);
	if (g_shmem == MAP_FAILED) {
		g_shmem = NULL;
		trace (0, "failed to mmap file %s\n", g_shmem_path);
		shmem_fini ();
		return false;
	}

	return true;
}

void shmem_fini ()
{
	if (g_shmem) {
		// force clients to re-open the shm — это делает только писатель:
		// у читателя отображение PROT_READ, и запись в него — SIGSEGV
		// (падал `afrd -s` при живом демоне, проверено на стенде)
		if (!g_shmem_read) {
			g_shmem->size = 0;
			g_shmem->crc32++;
			msync (g_shmem, sizeof (afrd_shmem_t), MS_SYNC);
		}

		munmap (g_shmem, sizeof (afrd_shmem_t));
		g_shmem = NULL;
	}

	close (g_shmem_h);
	if (!g_shmem_read)
		unlink (g_shmem_path);

	if (g_shmem_path) {
		free (g_shmem_path);
		g_shmem_path = NULL;
	}
}

void shmem_emerg ()
{
	unlink (g_shmem_path);

	if (g_shmem) {
		// force clients to re-open the shm
		g_shmem->size = 0;
		g_shmem->crc32++;
		msync (g_shmem, sizeof (afrd_shmem_t), MS_SYNC);
	}
}

void shmem_update ()
{
	if (!g_shmem || g_shmem_read)
		return;

	g_afrd_stats.crc32 = g_afrd_stats.crc32_copy =
		crc32_finish (crc32_update (CRC32_START,
			(uint8_t *)&g_afrd_stats.size,
			sizeof (afrd_shmem_t) - sizeof (uint32_t) * 2));

	memcpy (g_shmem, &g_afrd_stats, sizeof (afrd_shmem_t));
	msync (g_shmem, sizeof (afrd_shmem_t), MS_SYNC);
}

bool shmem_read ()
{
	if (!g_shmem || !g_shmem_read)
		return false;

	if (g_shmem->size != sizeof (afrd_shmem_t))
		return false;

	memcpy (&g_afrd_stats, g_shmem, sizeof (afrd_shmem_t));
	if (g_afrd_stats.crc32 != g_afrd_stats.crc32_copy)
		return false;

	if (g_afrd_stats.crc32 != crc32_finish (crc32_update (CRC32_START,
		(uint8_t *)&g_afrd_stats.size,
		sizeof (afrd_shmem_t) - sizeof (uint32_t) * 2)))
		return false;

	return true;
}
