#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "exec.h"

int exec_capture (const char *cmd, char *out, size_t out_sz)
{
	if (out_sz)
		out [0] = 0;

	/* stderr тоже нужен: 'service call' печатает ошибки туда, а вызывающий
	 * код (sink) различает успех и ошибку по тексту вывода.
	 * Команда оборачивается в подоболочку, иначе собственные перенаправления
	 * в самой команде переиграли бы наш "2>&1". */
	char full [EXEC_CMD_MAX];
	int r = snprintf (full, sizeof (full), "( %s ) 2>&1", cmd);
	if (r < 0 || (size_t)r >= sizeof (full))
		return -1;

	FILE *p = popen (full, "r");
	if (!p)
		return -1;

	size_t used = 0;
	while (used + 1 < out_sz)
	{
		size_t n = fread (out + used, 1, out_sz - 1 - used, p);
		if (!n)
			break;
		used += n;
	}
	if (out_sz)
		out [used] = 0;

	int rc = pclose (p);
	return (rc == -1) ? -1 : 0;
}
