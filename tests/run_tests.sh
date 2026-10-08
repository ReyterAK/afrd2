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
run test_stuck stuck.c
run test_guard guard.c
run test_exec exec.c
run test_sink sink.c exec.c tests/stub.c
run test_vdec vdec.c fps_class.c
run test_reconf reconf.c
run test_hdrinfo hdrinfo.c exec.c tests/stub.c

# Проверки по исходникам и данным, а не по коду на C: согласованность версии
# во всех местах, где она записана, формат ЧАВО, контракт запуска Magisk-модуля.
for sh_test in tests/test_versions.sh tests/test_faq.sh tests/test_magisk_module.sh; do
	sh "$sh_test" || status=1
done

# Java-логика без Android-зависимостей (значения «пакет=число», правило посева
# настроек): те же исходники, что идут в APK, компилируются javac и работают на JVM.
JAVAC=${JAVAC:-javac}
JAVA=${JAVA:-java}
mkdir -p out/host-tests/java
"$JAVAC" -d out/host-tests/java \
	android/app/src/main/java/ru/cobra/zap/afrd/PkgValues.java \
	android/app/src/main/java/ru/cobra/zap/afrd/Seed.java \
	tests/PkgValuesTest.java tests/SeedTest.java && {
	"$JAVA" -cp out/host-tests/java ru.cobra.zap.afrd.PkgValuesTest &&
	"$JAVA" -cp out/host-tests/java ru.cobra.zap.afrd.SeedTest
} || status=1

exit $status
