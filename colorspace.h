/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

#ifndef __COLORSPACE_H__
#define __COLORSPACE_H__

#include <stdbool.h>

/// load colorspace-related stuff from config file
extern void colorspace_init ();
/// free all memory occupied by colorspace stuff
extern void colorspace_fini ();
/// refresh current list of supported color spaces
extern bool colorspace_refresh ();
/// select and apply color space dependent on video mode
extern bool colorspace_apply (const char *mode);
/*
 * Чем применять цветовое пространство. По умолчанию — запись в sysfs-узел
 * (legacy). На Android 10+ этот узел доступен только на чтение, зато есть
 * вендорский сервис: задаём префикс команды, к которому дописывается значение
 * (например, "/vendor/bin/meson_display_client -s \"HDMI Color ATTR\"").
 * NULL или "" — вернуться к sysfs.
 */
extern void colorspace_set_apply_cmd (const char *cmd_prefix);

#endif /* __COLORSPACE_H__ */
