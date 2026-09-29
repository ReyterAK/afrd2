#!/bin/sh
set -e
cd "$(dirname "$0")/.."
mkdir -p out/host-tests
CC=${CC:-cc}
status=0
run() { # run <test.c> <extra-sources...>
	name=$(basename "$1" .c)
	shift
	"$CC" -Wall -Wno-unused-function -o "out/host-tests/$name" "tests/$name.c" "$@" || { status=1; return; }
	./out/host-tests/$name || status=1
}
run test_fps_class fps_class.c
exit $status
