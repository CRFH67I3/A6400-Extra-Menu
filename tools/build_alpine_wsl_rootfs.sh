#!/usr/bin/env bash
# Build an ultra-lightweight Alpine Linux WSL2 rootfs tarball (~20 MB)
# with Python 3, libusb, Sony-PMCA-RE, pmca_deploy_v3_clite.py, and open-source hook-stub.bin.
set -euo pipefail

OUT_TAR_GZ="${1:?Usage: $0 <output-rootfs.tar.gz> <hook-stub.bin>}"
HOOK_STUB="${2:?Usage: $0 <output-rootfs.tar.gz> <hook-stub.bin>}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ ! -f "$HOOK_STUB" ]; then
    echo "Missing hook-stub.bin: $HOOK_STUB" >&2
    exit 1
fi

CID="a6400_alpine_wsl_build_$$"
cleanup() {
    docker rm -f "$CID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "[*] Creating Alpine 3.20 container ($CID)..."
docker run --name "$CID" \
    -v "$REPO_ROOT/tools:/src-tools:ro" \
    -v "$(realpath "$HOOK_STUB"):/src-hook-stub.bin:ro" \
    alpine:3.20 /bin/sh -euxc '
apk add --no-cache python3 py3-pip libusb usbutils ca-certificates git
apk add --no-cache --virtual .build-deps gcc musl-dev python3-dev libffi-dev

pip install --break-system-packages --no-cache-dir \
    requests asn1crypto "axmlparserpy==0.0.4" certifi pycryptodomex pyusb pyyaml tlslite-ng

git clone --depth 1 https://github.com/ma1co/Sony-PMCA-RE.git /opt/pmca-re
rm -rf /opt/pmca-re/.git

mkdir -p /opt/a6400
cp /src-tools/pmca_deploy_v3_clite.py /opt/a6400/
cp /src-tools/build_autoexec_hook.py /opt/a6400/
cp /src-hook-stub.bin /opt/a6400/hook-stub.bin
chmod 755 /opt/a6400/pmca_deploy_v3_clite.py /opt/a6400/build_autoexec_hook.py

# Clean build toolchain & caches to minimize rootfs size
apk del --no-cache .build-deps py3-pip git
rm -rf /root/.cache /var/cache/apk/* /tmp/*
find /usr/lib/python3* -type d \( -name "__pycache__" -o -name "test" -o -name "tests" -o -name "idlelib" -o -name "tkinter" \) -exec rm -rf {} + 2>/dev/null || true

# On Alpine musl (no ldconfig/gcc in final image), ctypes.util.find_library("usb-1.0") returns None.
# Install a minimal sitecustomize.py shim so pyusb always finds /usr/lib/libusb-1.0.so.0 directly.
ln -sf /usr/lib/libusb-1.0.so.0 /usr/lib/libusb-1.0.so
SITE_PKG="$(python3 -c "import sysconfig; print(sysconfig.get_path(\"purelib\"))")"
cat > "$SITE_PKG/sitecustomize.py" <<EOF
import ctypes.util, glob
_orig_find = ctypes.util.find_library
def _musl_find(name):
    res = _orig_find(name)
    if res:
        return res
    for pat in (f"/usr/lib/lib{name}.so*", f"/lib/lib{name}.so*"):
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[0]
    return None
ctypes.util.find_library = _musl_find
EOF

cat > /etc/wsl.conf <<EOF
[automount]
enabled = true
options = "metadata"
[interop]
enabled = false
appendWindowsPath = false
EOF

# Smoke test inside the Alpine container
python3 - <<PY
import sys
sys.path.insert(0, "/opt/pmca-re")
sys.path.insert(0, "/opt/a6400")
from pmca.commands.usb import senserShellCommand
import usb.core, usb.backend.libusb1
import pmca_deploy_v3_clite, build_autoexec_hook
from pathlib import Path
assert Path("/opt/a6400/hook-stub.bin").is_file()
assert usb.backend.libusb1.get_backend() is not None, "libusb1 backend failed to load"
print("ALPINE_WSL_SMOKE_OK")
PY
'

mkdir -p "$(dirname "$OUT_TAR_GZ")"
echo "[*] Exporting container to $OUT_TAR_GZ ..."
docker export "$CID" | gzip -9 > "$OUT_TAR_GZ"
ls -lh "$OUT_TAR_GZ"
