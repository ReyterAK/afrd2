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
run test_inactive cfgkeys.c cfg_parse/cfg_parse.c
run test_pkgdet pkgdet.c
run test_guard guard.c
run test_exec exec.c
run test_sink sink.c exec.c tests/stub.c
run test_vdec vdec.c fps_class.c
run test_reconf reconf.c
run test_hdrinfo hdrinfo.c exec.c tests/stub.c

# Java-логика без Android-зависимостей (значения «пакет=число»): тот же
# исходник, что идёт в APK, компилируется javac и запускается на JVM.
JAVAC=${JAVAC:-javac}
JAVA=${JAVA:-java}
mkdir -p out/host-tests/java
"$JAVAC" -d out/host-tests/java \
	android/app/src/main/java/ru/cobra/zap/afrd/PkgValues.java \
	tests/PkgValuesTest.java && "$JAVA" -cp out/host-tests/java ru.cobra.zap.afrd.PkgValuesTest || status=1

exit $status
