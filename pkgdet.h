/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 *
 * Разбор «пакета приложения на переднем плане» из дампа активности.
 * Один разбор на оба пути работы демона (Android 6-9 и 10-14): форматы поля
 * различаются, внутренняя часть записи — нет.
 */

#ifndef __PKGDET_H__
#define __PKGDET_H__

#include <stdbool.h>
#include <stddef.h>

/**
 * Размер буфера имени пакета приложения для всех мест, где демон его хранит
 * (определение пакета переднего плана, списки mode.min.hz.list, fps.correct.*).
 *
 * 64 байт не хватало: система принимает имена заметно длиннее (проверено
 * установкой APK с именем 171 символа), а обрезанное имя не совпадает с пакетом
 * переднего плана — настройки для такого плеера молча не работают.
 */
#define PKG_NAME_LEN	256

/**
 * Найти пакет приложения на переднем плане в выводе команды
 * `dumpsys activity activities | grep -m1 ResumedActivity`.
 *
 * Понимает оба формата поля:
 *   "topResumedActivity=ActivityRecord{<hash> u<uid> <пакет>/<активити> t<n>}"  (Android 10-14)
 *   "ResumedActivity: ActivityRecord{<hash> u<uid> <пакет>/<активити> t<n>}"    (Android 6-9)
 *
 * @arg dump   текст вывода (NUL-терминированный)
 * @arg out    буфер для имени пакета
 * @arg out_sz размер буфера
 * @return true — пакет найден и скопирован в out (out — всегда NUL-строка);
 *         false — признака нет, запись не разобрана или пакет не влез в буфер
 *                 (усечённое имя совпало бы с элементом списка неверно).
 */
bool pkgdet_parse (const char *dump, char *out, size_t out_sz);

#endif /* __PKGDET_H__ */
