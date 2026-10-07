.PHONY: all clean test tests

# release or debug
MODE = debug

CC = $(CROSS_COMPILE)gcc -c
LD = $(CROSS_COMPILE)gcc

CFLAGS.release = -s -O2 -Wall
CFLAGS.debug = -g -Wall

LDFLAGS.release = -s
LDFLAGS.debug = -g

CFLAGS.local = $(CFLAGS.$(MODE)) -Icfg_parse -DBDATE="\"$(shell date +"%Y-%m-%d %H:%M:%S")\""
LDFLAGS.local = $(LDFLAGS.$(MODE))

OUT = out/$(CROSS_COMPILE)$(MODE)/

all: $(OUT)afrd

clean:
	rm -rf $(OUT)

# host-юнит-тесты (gcc, без Android)
test tests:
	sh tests/run_tests.sh

# BDATE («дата сборки», видна в `afrd -v`, в `afrd -s` и в приложении)
# вычисляется при каждом запуске сборки, но make о смене флагов не знает:
# main.o считался готовым, если не менялся main.c, поэтому в бинарнике
# оставалась дата ПРЕЖНЕЙ сборки (проверено: повторный `make` печатал
# «Nothing to be done», дата не менялась). Чтобы поле не врало, main.o
# зависит от FORCE и пересобирается каждый раз — это один файл и линковка.
$(OUT)main.o: FORCE

FORCE:

$(OUT)%.o: %.c $(OUT).stamp.dir
	$(CC) $(CFLAGS.local) $(CFLAGS) -o $@ $<

$(OUT)%.o: cfg_parse/%.c $(OUT).stamp.dir
	$(CC) $(CFLAGS.local) $(CFLAGS) -o $@ $<

$(OUT).stamp.dir:
	mkdir -p $(@D)
	touch $@

AFRD_SRC = main.c afrd.c sysfs.c cfg_parse.c cfg.c cfgkeys.c modes.c mstime.c uevent_filter.c \
	colorspace.c strfun.c shmem.c apisock.c crc32.c androp.c hdcp.c \
	fps_class.c fps_env.c sfmodes.c sink.c guard.c exec.c vdec.c platform.c reconf.c \
	hdrinfo.c pkgdet.c stuck.c

$(OUT)afrd: $(addprefix $(OUT),$(AFRD_SRC:.c=.o))
	$(LD) $(LDFLAGS.local) $(LDFLAGS) -o $@ $^ -lm
