#!/usr/bin/env python3
"""Deploy a built V3 CLite local-boot-package to a Sony A6400 via Sony-PMCA-RE."""
import argparse
import base64
import hashlib
import io
import json
import re
import sys
import tarfile
import time
from pathlib import Path


def run_cmd(cam, cmd, timeout=15.0, fast=False):
    dev = cam.dev
    dev.writeTerminal(f"{cmd}; printf '\\n__PMCA_RC:%s__\\n' $?\n".encode())
    end = time.monotonic() + timeout
    out = bytearray()
    rc = None
    pat = re.compile(rb"__PMCA_RC:(\d+)__")
    while time.monotonic() < end:
        try:
            chunk = dev.readTerminal()
        except Exception:
            break
        if chunk:
            out.extend(chunk)
            m = pat.search(out)
            if m:
                rc = int(m.group(1))
                if not fast:
                    time.sleep(0.03)
                    try:
                        more = dev.readTerminal()
                        if more:
                            out.extend(more)
                    except Exception:
                        pass
                break
        else:
            time.sleep(0.005)
    return out.decode("latin1", errors="replace"), rc


def must_cmd(cam, cmd, label, timeout=15.0):
    out, rc = run_cmd(cam, cmd, timeout=timeout)
    if rc != 0:
        raise RuntimeError(f"{label} failed rc={rc}: {out[-400:]}")
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package_dir", help="Path to local-boot-package directory")
    parser.add_argument("--pmca-dir", default="", help="Path to Sony-PMCA-RE checkout")
    parser.add_argument(
        "--install-hook",
        default="",
        help="Optional path to a6400-autoexec.so for fresh cameras without /etc/ld.so.preload",
    )
    args = parser.parse_args()

    pkg = Path(args.package_dir).resolve()
    manifest = json.loads((pkg / "manifest.json").read_text())
    boot_sh = pkg / "bootstrap-local.sh"
    boot_tar = pkg / "persistent/a6400-film-boot-v15.tar"
    boot_sh_sha = hashlib.sha256(boot_sh.read_bytes()).hexdigest()
    boot_tar_sha = hashlib.sha256(boot_tar.read_bytes()).hexdigest()
    hook_path = Path(args.install_hook).resolve() if args.install_hook else None
    hook_sha = hashlib.sha256(hook_path.read_bytes()).hexdigest() if hook_path else ""

    entry_shas = []
    with tarfile.open(boot_tar, "r") as tf:
        for m in tf.getmembers():
            if m.isfile():
                data = tf.extractfile(m).read()
                entry_shas.append((m.name, hashlib.sha256(data).hexdigest()))

    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tgz:
        tgz.add(boot_sh, arcname="new-autoexec.sh")
        tgz.add(boot_tar, arcname="new-boot.tar")
        if hook_path:
            tgz.add(hook_path, arcname="new-autoexec.so")
    tgz_bytes = buf.getvalue()
    tgz_sha = hashlib.sha256(tgz_bytes).hexdigest()

    if args.pmca_dir:
        sys.path.insert(0, str(Path(args.pmca_dir).resolve()))
    from pmca.commands.usb import senserShellCommand

    def deploy(cam):
        cam.setTerminalEnable(True)
        time.sleep(0.6)
        try:
            cam.dev.readTerminal()
        except Exception:
            pass
        try:
            if not hook_path:
                pre = must_cmd(
                    cam,
                    "/bin/busybox cat /etc/ld.so.preload && /bin/busybox ls -la /tmp/a6400-autoexec.so",
                    "CHECK_PRELOAD",
                )
                if "/tmp/a6400-autoexec.so" not in pre:
                    raise RuntimeError("Existing /etc/ld.so.preload + /tmp/a6400-autoexec.so hook required (or pass --install-hook)")

            must_cmd(
                cam,
                "test ! -f /etc/ld.so.preload || test -f /tmp/ld.so.preload.pre-v3c-backup || /bin/busybox cp -p /etc/ld.so.preload /tmp/ld.so.preload.pre-v3c-backup; "
                "test ! -f /tmp/autoexec.sh || test -f /tmp/autoexec.sh.pre-v3c-backup || /bin/busybox cp -p /tmp/autoexec.sh /tmp/autoexec.sh.pre-v3c-backup; "
                "test ! -f /tmp/a6400-film-boot-v15.tar || test -f /tmp/a6400-film-boot-v15.tar.pre-v3c-backup || /bin/busybox cp -p /tmp/a6400-film-boot-v15.tar /tmp/a6400-film-boot-v15.tar.pre-v3c-backup; "
                "/bin/busybox sync",
                "BACKUP_EXISTING",
            )

            b64 = base64.b64encode(tgz_bytes).decode("ascii")
            must_cmd(cam, "rm -rf /tmp_bt/v3c_up && mkdir -p /tmp_bt/v3c_up && : > /tmp_bt/v3c_up/up.b64", "PREP_UP")
            step = 360
            total = (len(b64) + step - 1) // step
            print(f"Uploading {len(tgz_bytes)} B ({total} chunks)...", flush=True)
            for idx, i in enumerate(range(0, len(b64), step)):
                _, rc = run_cmd(cam, f"printf %s {b64[i:i+step]} >> /tmp_bt/v3c_up/up.b64", timeout=5.0, fast=True)
                if rc != 0:
                    raise RuntimeError(f"upload chunk {idx}/{total} failed rc={rc}")

            unpack = must_cmd(
                cam,
                'D=/tmp_bt/v3c_up; /bin/busybox base64 -d "$D/up.b64" > "$D/up.tgz" && '
                '/bin/busybox sha256sum "$D/up.tgz" && /bin/busybox tar -xzf "$D/up.tgz" -C "$D" && '
                'rm -f "$D/up.b64" "$D/up.tgz" && /bin/sh -n "$D/new-autoexec.sh" && '
                '/bin/busybox sha256sum "$D/new-autoexec.sh" "$D/new-boot.tar"' + (' "$D/new-autoexec.so"' if hook_path else ''),
                "VERIFY_UPLOAD",
                timeout=20.0,
            )
            if tgz_sha not in unpack or boot_sh_sha not in unpack or boot_tar_sha not in unpack:
                raise RuntimeError("uploaded SHA mismatch")
            if hook_path and hook_sha not in unpack:
                raise RuntimeError("uploaded hook SHA mismatch")

            for name, exp_sha in entry_shas:
                r = must_cmd(
                    cam,
                    f'D=/tmp_bt/v3c_up; mkdir -p "$D/ex" && /bin/busybox tar -xf "$D/new-boot.tar" -C "$D/ex" "{name}" && /bin/busybox sha256sum "$D/ex/{name}"; rm -rf "$D/ex"',
                    f"VERIFY_{name}",
                )
                if exp_sha not in r:
                    raise RuntimeError(f"entry {name} SHA mismatch")

            install_hook_cmd = ""
            if hook_path:
                install_hook_cmd = (
                    '"$BB" cp "$D/new-autoexec.so" /tmp/a6400-autoexec.so.new && '
                    '"$BB" chmod 755 /tmp/a6400-autoexec.so.new && "$BB" sync && '
                    '"$BB" mv /tmp/a6400-autoexec.so.new /tmp/a6400-autoexec.so && '
                )

            must_cmd(
                cam,
                'BB=/bin/busybox; D=/tmp_bt/v3c_up; '
                + install_hook_cmd +
                '"$BB" cp "$D/new-boot.tar" /tmp/a6400-film-boot-v15.tar.new && '
                '"$BB" chmod 644 /tmp/a6400-film-boot-v15.tar.new && "$BB" sync && '
                '"$BB" mv /tmp/a6400-film-boot-v15.tar.new /tmp/a6400-film-boot-v15.tar && '
                '"$BB" cp "$D/new-autoexec.sh" /tmp/autoexec.sh.new && '
                '"$BB" chmod 755 /tmp/autoexec.sh.new && "$BB" sync && '
                '"$BB" mv /tmp/autoexec.sh.new /tmp/autoexec.sh && '
                '"$BB" rm -rf "$D" && "$BB" rm -f /tmp/disable_hook && "$BB" sync',
                "ATOMIC_INSTALL",
                timeout=20.0,
            )
            if hook_path:
                must_cmd(
                    cam,
                    'printf "/tmp/a6400-autoexec.so\\n" > /etc/ld.so.preload.new && '
                    '/bin/busybox chmod 644 /etc/ld.so.preload.new && /bin/busybox sync && '
                    '/bin/busybox mv /etc/ld.so.preload.new /etc/ld.so.preload && /bin/busybox sync',
                    "INSTALL_PRELOAD",
                )
            final = must_cmd(
                cam,
                "/bin/busybox sha256sum /tmp/autoexec.sh /tmp/a6400-film-boot-v15.tar" + (" /tmp/a6400-autoexec.so" if hook_path else ""),
                "FINAL_SHA",
            )
            if boot_sh_sha not in final or boot_tar_sha not in final or (hook_path and hook_sha not in final):
                raise RuntimeError("final SHA mismatch")
            print(f"V3_CLITE_DEPLOY_OK package={manifest['package_id']}", flush=True)
        finally:
            try:
                cam.setTerminalEnable(False)
            except Exception:
                pass

    senserShellCommand(driverName="libusb", complete=deploy)


if __name__ == "__main__":
    main()
