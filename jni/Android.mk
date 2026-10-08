# Android makefile for building an bionic-linked binary of the afrd

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := afrd
LOCAL_SRC_FILES := $(addprefix ../,main.c afrd.c sysfs.c cfg_parse/cfg_parse.c \
	cfg.c cfgkeys.c modes.c mstime.c uevent_filter.c colorspace.c strfun.c shmem.c \
	apisock.c crc32.c androp.c hdcp.c fps_class.c fps_env.c sfmodes.c modes_sel.c sink.c guard.c \
	pause.c vdec.c platform.c exec.c reconf.c hdrinfo.c pkgdet.c stuck.c)
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../cfg_parse
# BDATE («дата сборки») вычисляется при разборе этого файла. Сам по себе он не
# заставляет пересобрать main.o: ни make, ни ndk-build не узнают, что строка
# флагов изменилась, — поэтому main.o пересобирает ./mka (удаляет объект перед
# сборкой). При сборке напрямую через ndk-build дата может остаться от прежней
# сборки — собирайте через ./mka.
LOCAL_CFLAGS := -DBDATE="\"$(shell date +"%Y-%m-%d %H:%M:%S")\""

include $(BUILD_EXECUTABLE)
