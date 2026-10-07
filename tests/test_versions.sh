#!/bin/sh
# Согласованность версии по всем местам, где она записана.
#
# Источник правды — g_version/g_ver_sfx в main.c. Из него ./mka выводит
# versionName/versionCode в android/app/build.gradle, version/versionCode в
# install/magisk/module.prop и строку версии в README.md, а ChangeLog получает
# раздел с этим номером при выпуске.
#
# Почему это важно (иначе дефект на ровном месте):
#   * AFRService.checkDaemon() сравнивает versionName APK с версией демона из
#     shmem и перезапускает демон при расхождении — поднятый в одном месте
#     номер даёт цикл перезапусков;
#   * версия из module.prop — то, по чему Magisk решает, обновлён ли модуль;
#   * номер версии в ChangeLog — это то, что видит пользователь в «О программе».

set -e
cd "$(dirname "$0")/.."

fail () { echo "test_versions: $1" >&2; exit 1; }
get_version () {
	sed main.c -ne '/const char \*g_version/{' -e 's/^.*= *"//' -e 's/".*$//' -e p -e '}'
}
get_suffix () {
	sed main.c -ne '/const char \*g_ver_sfx/{' -e 's/^.*= *"//' -e 's/".*$//' -e p -e '}'
}

VERSION=`get_version`
SUFFIX=`get_suffix`
[ -n "$VERSION" ] || fail "в main.c не найден g_version"

# ожидаемый номер сборки: major*10000 + minor*100 + patch (0.3.3 -> 303)
vpart () { echo "$VERSION" | cut -d. -f"$1" | sed 's/^0*//' ; }
VMAJ=`vpart 1`; VMIN=`vpart 2`; VPAT=`vpart 3`
VMAJ=${VMAJ:-0}; VMIN=${VMIN:-0}; VPAT=${VPAT:-0}
VCODE=`expr "$VMAJ" \* 10000 + "$VMIN" \* 100 + "$VPAT"`

field () { # field <файл> <образец sed>
	sed -n "$2" "$1" | head -1
}

GRADLE=android/app/build.gradle
PROP=install/magisk/module.prop

# 1. Gradle: имя, суффикс и номер сборки
gn=`field "$GRADLE" "s/^ *versionName *'\(.*\)'/\1/p"`
gs=`field "$GRADLE" "s/^ *versionNameSuffix *'\(.*\)'/\1/p"`
gc=`field "$GRADLE" "s/^ *versionCode *\(.*\)/\1/p"`
[ "$gn" = "$VERSION" ] || fail "build.gradle: versionName='$gn', в main.c '$VERSION'"
[ "$gs" = "$SUFFIX" ] || fail "build.gradle: versionNameSuffix='$gs', в main.c '$SUFFIX'"
[ "$gc" = "$VCODE" ] || fail "build.gradle: versionCode=$gc, ожидается $VCODE (из $VERSION)"

# 2. Magisk-модуль: и имя версии, и номер сборки
pn=`field "$PROP" "s/^version=\(.*\)/\1/p"`
pc=`field "$PROP" "s/^versionCode=\(.*\)/\1/p"`
[ "$pn" = "$VERSION$SUFFIX" ] || fail "module.prop: version='$pn', в main.c '$VERSION$SUFFIX'"
[ "$pc" = "$VCODE" ] || fail "module.prop: versionCode=$pc, ожидается $VCODE"

# 3. README: строка о версии форка (её переписывает ./mka)
grep -q "^AFRd2 — версия $VERSION$SUFFIX" README.md ||
	fail "README.md: нет строки 'AFRd2 — версия $VERSION$SUFFIX'"

# 4. ChangeLog: раздел с этим номером есть в обоих языках
grep -q "^$VERSION$SUFFIX " ChangeLog-ru.txt ||
	fail "ChangeLog-ru.txt: нет раздела версии $VERSION$SUFFIX"
grep -q "^$VERSION$SUFFIX " ChangeLog-en.txt ||
	fail "ChangeLog-en.txt: нет раздела версии $VERSION$SUFFIX"

echo "versions: OK ($VERSION$SUFFIX / $VCODE)"
