#ifndef VDEC_H
#define VDEC_H

#include <stdbool.h>

typedef struct
{
	bool active;			/* есть ли активный декодер */
	char device [40];		/* device name, напр. ammvdec_h264_v4 */
	unsigned long long frames;	/* frame count (для замера fps по дельте) */
	unsigned decl_dur;		/* frame dur: длительность кадра в тиках 1/96000 с,
					 * 0 — поля нет. Только как уточнение к замеру */
} vdec_status_t;

/*
 * Источник частоты кадров — frame count: fps считается демоном как дельта
 * счётчика за дельту времени (см. now_us).
 *
 * Поле "frame dur" парсится отдельно (decl_dur) и НЕ используется как основной
 * источник: на ATV14 (журнал, §4г) оно бывает мусором — на синтетических
 * клипах драйвер рапортует 30 fps при реальных 23.976. Зато на живых IPTV-
 * потоках оно совпадает с номиналом, а замер по счётчику там систематически
 * завышен (плеер повторно декодирует кадры на границах HLS-сегментов: поток
 * 23.976 измерялся как 24.56, +2.4%). Поэтому decl_dur применяется только как
 * уточнение рядом с замером — см. fps_refine_declared() в fps_class.h.
 */
int vdec_parse (const char *text, vdec_status_t *out);
int vdec_read (const char *path, vdec_status_t *out);

/* CLOCK_MONOTONIC в микросекундах. */
unsigned long long now_us (void);

#endif
