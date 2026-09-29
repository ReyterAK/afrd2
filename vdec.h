#ifndef VDEC_H
#define VDEC_H

#include <stdbool.h>

typedef struct
{
	bool active;			/* есть ли активный декодер */
	char device [40];		/* device name, напр. ammvdec_h264_v4 */
	unsigned long long frames;	/* frame count (для замера fps по дельте) */
} vdec_status_t;

/*
 * Единственный источник частоты кадров — frame count: fps считается
 * демоном как дельта счётчика за дельту времени (см. now_us).
 * Поля "frame rate"/"frame dur" из vdec_status намеренно НЕ парсятся:
 * на ATV14 (журнал, §4г) они недостоверны (24 fps при реальных 23.976
 * и т.п.), поэтому использовать их как частоту нельзя.
 */
int vdec_parse (const char *text, vdec_status_t *out);
int vdec_read (const char *path, vdec_status_t *out);

/* CLOCK_MONOTONIC в микросекундах. */
unsigned long long now_us (void);

#endif
