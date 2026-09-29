#ifndef EXEC_H
#define EXEC_H

#include <stddef.h>

/* Максимальная длина команды (с учётом перенаправления stderr в stdout). */
#define EXEC_CMD_MAX	1024

/* Выполнить команду через /system/bin/sh и захватить stdout+stderr.
 * Возврат: 0 = команда запущена, -1 = не удалось запустить. */
int exec_capture (const char *cmd, char *out, size_t out_sz);

#endif
