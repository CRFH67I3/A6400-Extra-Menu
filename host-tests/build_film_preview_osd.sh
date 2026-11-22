#!/bin/sh
# Build only: external V4.0 RGBA4444 OSD preview helper linked against camera libc.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:?output directory}
LIBS=${2:?original camera libc directory}
UI_DIR=${3:?built persistent UI directory}
mkdir -p "$OUT"
python3 - "$UI_DIR" "$OUT" <<'PY'
from pathlib import Path
import re, sys
ui_dir, out = map(Path, sys.argv[1:3])
syms = (ui_dir / 'symbols.txt').read_text()
vm = sorted(set(re.findall(
    r'^\s*\d+:\s+([0-9a-f]+)\s+\d+\s+OBJECT\s+GLOBAL\s+DEFAULT\s+\d+\s+a6400_ui_v4_v1$',
    syms, re.M)))
assert len(vm) == 1, f'expected unique a6400_ui_v4_v1 symbol, got {vm}'
rva = int(vm[0], 16)
(out / 'film_preview_osd_build.h').write_text(
    '#define FILM_PREVIEW_UI_MODULE "/tmp_bt/a6400-film-ui-v3-persist.so"\n'
    f'#define FILM_PREVIEW_V4_RVA 0x{rva:x}u\n'
)
print(f'PREVIEW_OSD_V4_RVA 0x{rva:x}')
PY
CC=arm-linux-gnueabihf-gcc
COMMON="-march=armv7-a+fp -mfpu=vfpv3-d16 -mfloat-abi=hard -mthumb -Os -fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -Wall -Wextra -Werror -fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"
$CC $COMMON -std=c11 -fPIE -I"$OUT" -I"$ROOT/camera" -c "$ROOT/camera/film_preview_osd.c" -o "$OUT/preview-osd.o"
$CC $COMMON -c "$ROOT/camera/film_settings_start.S" -o "$OUT/start.o"
$CC -mfloat-abi=hard -nostdlib -pie -Wl,--gc-sections,-s,-e,_start,-z,now,--dynamic-linker=/lib/ld-linux.so.3 \
 "$OUT/start.o" "$OUT/preview-osd.o" -Wl,--no-as-needed "$LIBS/libc-2.11.2.so" -lgcc \
 -o "$OUT/a6400-film-preview.elf"
arm-linux-gnueabihf-readelf -lWdVs "$OUT/a6400-film-preview.elf" > "$OUT/elf-compatibility.txt"
python3 - "$OUT/elf-compatibility.txt" <<'PY'
from pathlib import Path
import re, sys
s = Path(sys.argv[1]).read_text()
versions = set(re.findall(r'Name: GLIBC_([0-9.]+)', s))
assert 'Requesting program interpreter: /lib/ld-linux.so.3' in s
assert '[libc.so.6]' in s and versions
assert all(tuple(map(int, v.split('.'))) <= (2, 11, 2) for v in versions), versions
print('BUILD_ONLY: V4.0 external OSD preview helper; camera libc', sorted(versions))
PY
sha256sum "$OUT/a6400-film-preview.elf"
