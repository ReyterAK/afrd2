#ifndef EXEC_H
#define EXEC_H

#include <stddef.h>

/* Максимальная длина команды (с учётом перенаправления stderr в stdout). */
#define EXEC_CMD_MAX	1024

/* Выполнить команду через /system/bin/sh и захватить stdout+stderr.
 * Возврат: 0 = команда запущена, -1 = не удалось запустить. */
int exec_capture (const char *cmd, char *out, size_t out_sz);

/* То же, что exec_capture, но отдаёт код возврата команды (0 — успех).
 * Нужно там, где важен именно статус (вендорный помощник: он возвращает
 * ненулевой код, если сервис недоступен или режим отвергнут).
 * Возврат: -1 — команду запустить не удалось; иначе — код возврата команды. */
int exec_status (const char *cmd, char *out, size_t out_sz);

#endif
