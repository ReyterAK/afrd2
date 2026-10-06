#!/bin/sh
set -e
cd "$(dirname "$0")/.."
mkdir -p out/host-tests
CC=${CC:-cc}
status=0
run() { # run <test.c> <extra-sources...>
	name=$(basename "$1" .c)
	shift
	"$CC" -Wall -Wno-unused-function -Icfg_parse -o "out/host-tests/$name" "tests/$name.c" "$@" -lm || { status=1; return; }
	./out/host-tests/$name || status=1
}
run test_fps_class fps_class.c
run test_fps_env fps_env.c fps_class.c
run test_sfmodes sfmodes.c
run test_cfgkeys cfgkeys.c cfg_parse/cfg_parse.c
run test_pkgdet pkgdet.c
run test_guard guard.c
run test_exec exec.c
run test_sink sink.c exec.c tests/stub.c
run test_vdec vdec.c fps_class.c
run test_reconf reconf.c
run test_hdrinfo hdrinfo.c exec.c tests/stub.c
exit $status
