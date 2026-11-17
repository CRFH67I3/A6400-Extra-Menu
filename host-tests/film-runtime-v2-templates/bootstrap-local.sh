#!/bin/sh
# Stage a fixed local bundle into RAM. No SD access or monitor IPC at startup.
set -uf
umask 077
export PATH=/usr/bin:/bin:/sbin:/usr/sbin
BB=/bin/busybox
RAM=/tmp_bt
SOURCE=/tmp/a6400-film-boot-v15.tar
COPY=$RAM/a6400-film-boot.copy
STAGE=$RAM/a6400-film-boot-stage
LOCK=$RAM/a6400-film-local-boot.lock
BOOTLOG=$RAM/a6400-film-boot.log
BUNDLE_BYTES=@@BUNDLE_BYTES@@
test ! -e "$BOOTLOG" && test ! -L "$BOOTLOG" || exit 1
set -C
exec > "$BOOTLOG" 2>&1 || exit 1
set +C
echo "FILM_LOCAL_BOOT_BEGIN package=@@PACKAGE_ID@@ pid=$$"
fail() { echo "FILM_LOCAL_BOOT_FAIL stage=$1"; exit 1; }
hash_is() {
    test -f "$1" && test ! -L "$1" || return 1
    hash_line=$("$BB" sha256sum "$1") || return 1
    test "${hash_line%% *}" = "$2"
}
disabled() {
    test -e /tmp/disable_hook || test -e "$RAM/a6400-film.DISABLE" ||
        test -e "$RAM/a6400-film-capture.stop"
}
gate() {
    ! disabled && test -r /proc/mounts || return 1
    while read -r source target rest; do
        case "$source" in /dev/mmca*|/dev/mmcblk*) return 1;; esac
    done < /proc/mounts
}
phase() { printf 'phase=%s\npid=%s\npackage=@@PACKAGE_ID@@\n' "$1" "$$" > "$LOCK/manifest"; }
if disabled; then echo FILM_LOCAL_BOOT_DISABLED; exit 0; fi
gate || fail sd_linux_mounted
test -f "$RAM/ae.lock" && test ! -L "$RAM/ae.lock" || fail missing_constructor_lock
for path in "$COPY" "$STAGE" "$RAM/a6400-capture-v1.lock" \
    "$RAM/a6400-film-capture.inflight" "$RAM/a6400-film-job.lock" \
    "$RAM/a6400-film-runtime.lock"; do
    test ! -e "$path" && test ! -L "$path" || fail outstanding_operation_or_copy
done
hash_is "$SOURCE" @@BUNDLE_SHA@@ || fail local_bundle_pin
"$BB" mkdir "$LOCK" || fail existing_boot
phase local_copy || fail phase
# Noclobber prevents replacing a concurrently created file or symlink.
set -C
"$BB" cat "$SOURCE" > "$COPY" || fail local_copy
set +C
hash_is "$COPY" @@BUNDLE_SHA@@ || fail bundle_hash
byte_line=$("$BB" wc -c < "$COPY") || fail bundle_size
set -- $byte_line
test "$#" = 1 && test "$1" = "$BUNDLE_BYTES" || fail bundle_size
gate || fail extract_gate
"$BB" mkdir "$STAGE" || fail stage_directory
"$BB" tar -xf "$COPY" -C "$STAGE" || fail extract
hash_is "$STAGE/prepare.sh" @@PREPARE_SHA@@ || fail prepare_pin
phase prepare || fail phase
/bin/sh "$STAGE/prepare.sh"
prepare_rc=$?
if test "$prepare_rc" = 10; then echo FILM_LOCAL_BOOT_DISABLED; exit 0; fi
test "$prepare_rc" = 0 || fail prepare
gate && hash_is "$RAM/a6400-film-runtime-start.sh" @@RUNTIME_SHA@@ || fail runtime_pin
hash_is "$COPY" @@BUNDLE_SHA@@ && "$BB" rm "$COPY" || fail archive_release
phase runtime || fail phase
echo "FILM_LOCAL_BOOT_STAGED package=@@PACKAGE_ID@@ native_sd_read=0 raw_sd_mount=0"
exec /bin/sh "$RAM/a6400-film-runtime-start.sh"
