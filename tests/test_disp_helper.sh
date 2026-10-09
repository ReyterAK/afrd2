#!/bin/sh
# Проверка обёртки вендорного помощника (tools/droidlogic/afrd-disp.sh): она
# выбирает droidlogic-res.apk из списка кандидатов, потому что жёсткий путь
# /system_ext/priv-app/... верен только с Android 11 (в 10 такого раздела нет),
# а на части OEM-сборок тот же apk лежит в /system, /vendor или /product.
#
# Живой дефект, который здесь закрывается: демон уходил в пассивный режим на
# Android 10 — не из-за драйверов, а из-за одного хардкод-пути.
#
# Тест запускает настоящую обёртку с app_process-заглушкой (Android не нужен):
# заглушка печатает CLASSPATH, по нему и проверяем выбор кандидата.

set -e
cd "$(dirname "$0")/.."

fail () { echo "test_disp_helper: $1" >&2; exit 1; }

WORK=out/host-tests/disp
SH=tools/droidlogic/afrd-disp.sh
rm -rf "$WORK"
mkdir -p "$WORK/bin" "$WORK/res_vendor"

[ -f "$SH" ] || fail "нет $SH"

# Заглушка app_process: печатает полученный CLASSPATH и выходит с кодом из
# APPRC_RC (нужен, чтобы проверить, что код возврата помощника не подменяется).
printf '#!/bin/sh\necho "CP: $CLASSPATH"\nexit "${APPRC_RC:-0}"\n' > "$WORK/bin/app_process"
chmod 755 "$WORK/bin/app_process"

touch "$WORK/res_vendor/droidlogic-res.apk"

# 1. Каталог-кандидат, стоящий в списке НЕ первым, должен быть найден.
out=$(PATH="$WORK/bin:$PATH" AFRD_DISP_RES_DIRS="/nonexistent/a $WORK/res_vendor" \
	sh "$SH" 1080p24hz) ||
	fail "обёртка не сработала при кандидате из списка"

echo "$out" | grep -q ":$WORK/res_vendor/droidlogic-res.apk$" ||
	fail "выбран не тот apk: $out"

# 2. Ни одного кандидата — свой код возврата 6 (демон трактует его как отказ).
rc=0
PATH="$WORK/bin:$PATH" AFRD_DISP_RES_DIRS="/nonexistent/a /nonexistent/b" \
	sh "$SH" 1080p24hz >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 6 ] ||
	fail "без droidlogic-res.apk ожидался код 6, получен $rc"

# 3. Явный AFRD_DISP_RES имеет приоритет над списком.
out=$(PATH="$WORK/bin:$PATH" AFRD_DISP_RES=/opt/res/droidlogic-res.apk \
	AFRD_DISP_RES_DIRS="$WORK/res_vendor" sh "$SH" 1080p24hz) ||
	fail "обёртка не сработала с явным AFRD_DISP_RES"

echo "$out" | grep -q ":/opt/res/droidlogic-res.apk$" ||
	fail "AFRD_DISP_RES не имеет приоритета: $out"

# 4. Без имени режима — код 2 (и никакого запуска app_process).
rc=0
out=$(PATH="$WORK/bin:$PATH" sh "$SH" 2>&1) || rc=$?
[ "$rc" -eq 2 ] || fail "без режима ожидался код 2, получен $rc"
echo "$out" | grep -q "CP: " && fail "без режима app_process запущен не должен быть"

# 5. Код возврата app_process передаётся наружу без подмены (демон считает
#    успехом только 0, поэтому «всегда 0» скрыл бы неприменённый режим).
rc=0
PATH="$WORK/bin:$PATH" AFRD_DISP_RES_DIRS="$WORK/res_vendor" APPRC_RC=4 \
	sh "$SH" 1080p24hz >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 4 ] || fail "код возврата app_process не передан: $rc"

echo "test_disp_helper: ok"
