#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../exec.h"

int main (void)
{
	char out [256];

	/* stderr должен быть виден в out: 'service call' печатает ошибки туда,
	 * а sink различает успех и ошибку по тексту вывода. */
	assert (exec_capture ("echo err 1>&2", out, sizeof (out)) == 0);
	assert (strstr (out, "err") != NULL);

	assert (exec_capture ("echo out", out, sizeof (out)) == 0);
	assert (strstr (out, "out") != NULL);

	/* слишком длинная команда -> -1 (не влезла в EXEC_CMD_MAX) */
	char long_cmd [EXEC_CMD_MAX + 16];
	memset (long_cmd, 'x', sizeof (long_cmd) - 1);
	long_cmd [sizeof (long_cmd) - 1] = 0;
	assert (exec_capture (long_cmd, out, sizeof (out)) == -1);

	/* вывод длиннее буфера -> безопасное усечение */
	assert (exec_capture ("printf 'abcdefghijklmnop'", out, 8) == 0);
	assert (strcmp (out, "abcdefg") == 0);

	/* exec_status отдаёт код возврата команды: он нужен вендорному помощнику,
	 * который сообщает об отказе ненулевым кодом */
	assert (exec_status ("exit 0", out, sizeof (out)) == 0);
	assert (exec_status ("exit 3", out, sizeof (out)) == 3);
	assert (exec_status ("echo hi; exit 7", out, sizeof (out)) == 7);
	assert (strstr (out, "hi") != NULL);
	/* убитый сигналом — не «код выхода», а отказ */
	assert (exec_status ("kill -9 $$", out, sizeof (out)) == -1);
	/* слишком длинная команда -> -1 */
	assert (exec_status (long_cmd, out, sizeof (out)) == -1);

	printf ("exec: OK\n");
	return 0;
}
