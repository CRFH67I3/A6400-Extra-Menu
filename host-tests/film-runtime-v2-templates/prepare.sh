#!/bin/sh
# Verify and stage data into RAM. Never execute a staged runtime artifact.
set -uf
umask 077
export PATH=/usr/bin:/bin:/sbin:/usr/sbin
BB=/bin/busybox
ROOT=/mnt/A6400/RUNTIME
RAM=/tmp_bt
STAGE=$RAM/a6400-film-stage.lock
fail() { echo "FILM_STAGE_FAIL stage=$1"; exit 1; }
hash_is() {
    test -f "$1" && test ! -L "$1" || return 1
    if hash_line=$("$BB" sha256sum "$1" 2>/dev/null); then
        test "${hash_line%% *}" = "$2"
        return $?
    fi
    hash_line=$(/usr/bin/openssl dgst -sha256 "$1" 2>/dev/null) || return 1
    test "${hash_line##* }" = "$2"
}
if test -e /tmp/disable_hook || test -e /mnt/disable_hook ||
   test -e /mnt/disable.txt || test -e /mnt/A6400/DISABLE ||
   test -e "$RAM/a6400-film.DISABLE" || test -e "$RAM/a6400-film-capture.stop"; then
    echo FILM_STAGE_DISABLED
    exit 10
fi
test -d "$ROOT" && test ! -L "$ROOT" || fail source_directory
# Check every source before copying even the first executable.
while read -r name dest expected mode; do
    hash_is "$ROOT/$name" "$expected" || fail "source_hash-$name"
    test ! -e "$RAM/$dest" && test ! -L "$RAM/$dest" || fail "existing-$dest"
done <<'FILM_ARTIFACTS'
@@ROWS@@
FILM_ARTIFACTS
test ! -e "$RAM/a6400-film-staged" && test ! -L "$RAM/a6400-film-staged" || fail existing_receipt
"$BB" mkdir "$STAGE" || fail existing_stage
{ echo "phase=copying"; echo "pid=$$"; } > "$STAGE/manifest" || fail stage_manifest
while read -r name dest expected mode; do
    "$BB" cp "$ROOT/$name" "$RAM/$dest" || fail "copy-$dest"
    "$BB" chmod "$mode" "$RAM/$dest" || fail "mode-$dest"
    hash_is "$RAM/$dest" "$expected" || fail "copied_hash-$dest"
done <<'FILM_ARTIFACTS'
@@ROWS@@
FILM_ARTIFACTS
echo "FILM_STAGE_PREPARED package=@@PACKAGE_ID@@ initial_enabled=0" > "$RAM/a6400-film-staged" || fail receipt
{ echo "phase=prepared"; echo "pid=$$"; } > "$STAGE/manifest" || fail stage_manifest
echo FILM_STAGE_PREPARED
