#ifndef VDEC_H
#define VDEC_H

#include <stdbool.h>

/* Максимум каналов, которые парсим из vdec_status. Штатно канал один;
 * два появляются в переходные моменты и при «залипшем» (zombie) инстансе
 * декодера — тогда драйвер печатает старый блок первым, новый последним. */
#define VDEC_MAX_CHANNELS 4

typedef struct
{
	bool active;			/* есть ли активный декодер */
	char device [40];		/* device name, напр. ammvdec_h264_v4 */
	unsigned long long frames;	/* frame count (для замера fps по дельте) */
	unsigned decl_dur;		/* frame dur: длительность кадра в тиках 1/96000 с,
					 * 0 — поля нет. Только как уточнение к замеру */
} vdec_status_t;

/* Один канал vdec_status. */
typedef struct
{
	char device [40];
	unsigned long long frames;
	unsigned decl_dur;
} vdec_chan_t;

/*
 * Состояние выбора канала для vdec_select(): счётчики кадров прошлого опроса.
 * Живёт в run_atv14() и сохраняется между итерациями.
 */
typedef struct
{
	unsigned long long prev_frames [VDEC_MAX_CHANNELS];
	bool have_prev;
} vdec_sel_t;

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

/* Разобрать все каналы. Возвращает число каналов (0..max). Канал без
 * "device name" или без "frame count" не считается (как и прежде). */
int vdec_parse_channels (const char *text, vdec_chan_t *out, int max);

/*
 * Выбор активного канала (R61, вариант 2): канал, чей счётчик кадров вырос
 * относительно прошлого опроса; если растут несколько — самый свежий (последний);
 * если ни один не растёт (пауза/залипание) — последний (самый свежий инстанс).
 * Обновляет st. Возвращает индекс канала или -1, если каналов нет.
 */
int vdec_select (const vdec_chan_t *chans, int n, vdec_sel_t *st);

/* Чтение + разбор + выбор канала. out заполняется выбранным каналом;
 * active=false, если видео нет. Возвращает -1 только при ошибке чтения
 * (состояние st при этом не трогается). */
int vdec_read (const char *path, vdec_status_t *out, vdec_sel_t *st);

/* Быстрая проверка «есть ли хоть один канал» без изменения состояния выбора
 * (нужна для подтверждения старта/стопа в дебаунсе). */
int vdec_probe_active (const char *path, bool *active);

/* CLOCK_MONOTONIC в микросекундах. */
unsigned long long now_us (void);

#endif
