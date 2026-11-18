#!/usr/bin/env python3
"""
Deploy a built Sony A6400 (FW 2.00) V3.1 CLite local-boot-package via Sony-PMCA-RE.
Supports Windows (libusb), macOS, and Linux (Python 3.8+).
Automatically provisions Sony-PMCA-RE / Python dependencies if missing,
auto-detects BusyBox vs OpenSSL on the camera, and auto-patches /tmp/a6400-autoexec.so
from the camera's own /usr/lib/libAK8975.so on fresh unhooked bodies.
"""
import argparse
import base64
import hashlib
import io
import json
import os
import re
import shutil
import site
import struct
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
import zipfile
from pathlib import Path

if sys.version_info < (3, 8):
    print(
        f"[!] Python 版本過舊 ({sys.version.split()[0]})，本部署工具需要 Python 3.8 以上。",
        file=sys.stderr,
    )
    raise SystemExit(1)

BASE = Path(__file__).resolve().parent
PATCH_ADDR = 0x988
HOOK_STUB = bytes.fromhex(
    "f0402de940d04de2fc008fe20010a0e32170a0e3000000ef000050e33800000a"
    "f8008fe20010a0e32170a0e3000000ef000050e33200000af4008fe20010a0e3"
    "2170a0e3000000ef000050e32c00000af0008fe20010a0e32170a0e3000000ef"
    "000050e32600001aec008fe2c110a0e3692fa0e30570a0e3000000ef000050e3"
    "1f0000ba0670a0e3000000ef0270a0e3000000ef000050e31900001a0270a0e3"
    "000000ef000050e30200000a0000a0e30170a0e3000000ef4270a0e3000000ef"
    "a4008fe27c108fe20020a0e300008de504108de508208de594308fe210308de5"
    "14208de580008fe20d10a0e110208de20b70a0e3000000ef0100a0e30170a0e3"
    "000000ef40d08de2f080bde82f6d6e742f64697361626c655f686f6f6b000000"
    "2f6d6e742f64697361626c652e747874000000002f746d702f64697361626c65"
    "5f686f6f6b0000002f746d702f6175746f657865632e7368000000002f746d70"
    "5f62742f61652e6c6f636b002f62696e2f736800504154483d2f7573722f6269"
    "6e3a2f62696e3a2f7362696e3a2f7573722f7362696e3a2f746d7000"
)


def add_windows_libusb_dll_paths():
    if sys.platform != "win32":
        return
    roots = list(site.getsitepackages()) + [site.getusersitepackages()]
    for r in roots:
        rp = Path(r)
        if not rp.is_dir():
            continue
        for dll in rp.rglob("libusb-1.0.dll"):
            d = str(dll.parent)
            if hasattr(os, "add_dll_directory"):
                try:
                    os.add_dll_directory(d)
                except OSError:
                    pass
            os.environ["PATH"] = d + os.pathsep + os.environ.get("PATH", "")


def ensure_deps(pmca_dir: Path):
    if not (pmca_dir / "pmca").is_dir():
        git = shutil.which("git")
        if git:
            print(f"[*] 正在下載 Sony-PMCA-RE 至 {pmca_dir}...")
            try:
                subprocess.check_call(
                    [git, "clone", "--depth", "1", "https://github.com/ma1co/Sony-PMCA-RE.git", str(pmca_dir)]
                )
            except Exception as exc:
                raise RuntimeError(f"透過 git 下載 Sony-PMCA-RE 失敗 ({exc})。") from exc
        else:
            url = "https://github.com/ma1co/Sony-PMCA-RE/archive/refs/heads/master.zip"
            print(f"[*] 找不到 git，改以內建 Python 下載 Sony-PMCA-RE ZIP ({url})...")
            with tempfile.TemporaryDirectory() as td:
                zip_path = Path(td) / "pmca.zip"
                urllib.request.urlretrieve(url, zip_path)
                with zipfile.ZipFile(zip_path, "r") as zf:
                    zf.extractall(td)
                extracted = Path(td) / "Sony-PMCA-RE-master"
                if not (extracted / "pmca").is_dir():
                    raise RuntimeError(f"解壓內容無效：{extracted}")
                shutil.move(str(extracted), str(pmca_dir))

    need_install = False
    try:
        from axmlparserpy.axmlprinter import AXMLPrinter  # noqa: F401
        import usb.core  # noqa: F401
        import yaml  # noqa: F401
        import Cryptodome  # noqa: F401
        import asn1crypto  # noqa: F401
        import certifi  # noqa: F401
        import tlslite  # noqa: F401
        import libusb_package  # noqa: F401
        if sys.platform == "win32":
            import comtypes  # noqa: F401
            import win32file  # noqa: F401
    except ImportError:
        need_install = True

    if need_install:
        if subprocess.call(
            [sys.executable, "-m", "pip", "--version"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ) != 0:
            subprocess.call([sys.executable, "-m", "ensurepip", "--upgrade"])
        print("[*] 正在安裝 Sony-PMCA-RE 相依套件 (setuptools, axmlparserpy==0.0.4, pyusb, libusb-package 等)...")
        pkgs = [
            "setuptools",
            "axmlparserpy==0.0.4",
            "pyusb",
            "libusb-package",
            "pycryptodomex",
            "pyyaml",
            "asn1crypto",
            "certifi",
            "tlslite-ng",
        ]
        if sys.platform == "win32":
            pkgs += ["comtypes", "pywin32"]
        cmd = [sys.executable, "-m", "pip", "install", *pkgs]
        if subprocess.call(cmd) != 0:
            if subprocess.call([sys.executable, "-m", "pip", "install", "--break-system-packages", *pkgs]) != 0:
                raise RuntimeError("自動安裝 Python 相依套件失敗，請確認網路與 pip 環境。")
        if not os.environ.get("PMCA_DEPLOY_REEXEC"):
            env = os.environ.copy()
            env["PMCA_DEPLOY_REEXEC"] = "1"
            raise SystemExit(subprocess.call([sys.executable, *sys.argv], env=env))

    add_windows_libusb_dll_paths()


def patch_libak8975(src_bytes: bytes) -> bytes:
    so = bytearray(src_bytes)
    if so[:4] != b"\x7fELF":
        raise ValueError("從相機讀取的 libAK8975.so 不是有效的 ELF 檔")
    e_shoff = struct.unpack_from("<I", so, 0x20)[0]
    e_shentsize = struct.unpack_from("<H", so, 0x2E)[0]
    e_shnum = struct.unpack_from("<H", so, 0x30)[0]
    e_shstrndx = struct.unpack_from("<H", so, 0x32)[0]
    shstr_off = struct.unpack_from("<I", so, e_shoff + e_shstrndx * e_shentsize + 0x10)[0]

    text_off = rodata_off = dyn_off = dyn_size = None
    for i in range(e_shnum):
        sh = e_shoff + i * e_shentsize
        name_off = struct.unpack_from("<I", so, sh)[0]
        name_end = so.index(b"\x00", shstr_off + name_off)
        name = so[shstr_off + name_off:name_end].decode("ascii", errors="replace")
        sh_type = struct.unpack_from("<I", so, sh + 0x04)[0]
        sh_offset = struct.unpack_from("<I", so, sh + 0x10)[0]
        sh_size = struct.unpack_from("<I", so, sh + 0x14)[0]
        if name == ".text":
            text_off = sh_offset
        elif name == ".rodata":
            rodata_off = sh_offset
        if sh_type == 6:
            dyn_off, dyn_size = sh_offset, sh_size

    if text_off != PATCH_ADDR:
        raise ValueError(f"libAK8975.so .text={text_off:#x} 與預期 {PATCH_ADDR:#x} 不符，停止修補！")
    if rodata_off is None or PATCH_ADDR + len(HOOK_STUB) >= rodata_off:
        raise ValueError("libAK8975.so .rodata 空間不足，停止修補！")
    if dyn_off is None:
        raise ValueError("libAK8975.so 找不到 .dynamic 區段！")

    so[PATCH_ADDR:PATCH_ADDR + len(HOOK_STUB)] = HOOK_STUB

    null_off = None
    off = dyn_off
    while off < dyn_off + dyn_size:
        if struct.unpack_from("<I", so, off)[0] == 0:
            null_off = off
            break
        off += 8
    if null_off is None or null_off + 16 > dyn_off + dyn_size:
        raise ValueError(".dynamic 沒有足夠的 DT_NULL 空間")
    if so[null_off + 8:null_off + 16] != b"\x00" * 8:
        raise ValueError("DT_NULL 後方欄位非零，停止修補！")

    struct.pack_into("<II", so, null_off, 12, PATCH_ADDR)
    struct.pack_into("<II", so, null_off + 8, 0, 0)
    return bytes(so)


def run_cmd(cam, cmd: str, timeout: float = 15.0, fast: bool = False):
    dev = cam.dev
    dev.writeTerminal(f'{cmd}; _rc=$?; echo; echo "__PMCA_RC:${{_rc}}__"\n'.encode())
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


def must_cmd(cam, cmd: str, label: str, timeout: float = 15.0) -> str:
    out, rc = run_cmd(cam, cmd, timeout=timeout)
    if rc != 0:
        raise RuntimeError(f"{label} failed rc={rc}: {out[-400:]}")
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package_dir", help="Path to local-boot-package directory")
    parser.add_argument(
        "--pmca-dir",
        default="",
        help="Path to Sony-PMCA-RE checkout (auto-downloaded if omitted or missing)",
    )
    parser.add_argument(
        "--install-hook",
        default="",
        help="Optional path to prebuilt a6400-autoexec.so (if omitted on a fresh camera, auto-patched from camera /usr/lib/libAK8975.so)",
    )
    args = parser.parse_args()

    pkg = Path(args.package_dir).resolve()
    manifest = json.loads((pkg / "manifest.json").read_text(encoding="utf-8"))
    boot_sh = pkg / "bootstrap-local.sh"
    boot_tar = pkg / "persistent/a6400-film-boot-v15.tar"
    boot_sh_sha = hashlib.sha256(boot_sh.read_bytes()).hexdigest()
    boot_tar_sha = hashlib.sha256(boot_tar.read_bytes()).hexdigest()
    explicit_hook_path = Path(args.install_hook).resolve() if args.install_hook else None

    pmca_dir = Path(args.pmca_dir).resolve() if args.pmca_dir else (BASE.parent / ".pmca-cache" / "Sony-PMCA-RE")
    ensure_deps(pmca_dir)
    sys.path.insert(0, str(pmca_dir))
    from pmca.commands.usb import senserShellCommand

    entry_shas = []
    with tarfile.open(boot_tar, "r") as tf:
        for m in tf.getmembers():
            if m.isfile():
                data = tf.extractfile(m).read()
                entry_shas.append((m.name, hashlib.sha256(data).hexdigest()))

    deploy_ok = False

    def deploy(cam):
        nonlocal deploy_ok
        cam.setTerminalEnable(True)
        time.sleep(0.6)
        try:
            cam.dev.readTerminal()
        except Exception:
            pass
        try:
            probe = must_cmd(
                cam,
                'BB=/bin/busybox; _s=none; _b=none; _d=none; '
                'if "$BB" sha256sum </dev/null >/dev/null 2>&1; then _s=bb; '
                'elif /usr/bin/openssl dgst -sha256 </dev/null >/dev/null 2>&1; then _s=openssl; fi; '
                'if "$BB" base64 </dev/null >/dev/null 2>&1; then _b=bb; '
                'elif /usr/bin/openssl enc -base64 </dev/null >/dev/null 2>&1; then _b=openssl; fi; '
                '"$BB" rm -rf /tmp_bt/v3c_up /tmp/v3c_up 2>/dev/null; '
                '_has_dd=0; if "$BB" dd if=/dev/zero of=/dev/null bs=1 count=1 >/dev/null 2>&1; then _has_dd=1; fi; '
                'if "$BB" mkdir /tmp_bt/v3c_up 2>/dev/null && '
                '{ if test "$_has_dd" = 1; then "$BB" dd if=/dev/zero of=/tmp_bt/v3c_up/.w bs=1024 count=480 >/dev/null 2>&1; '
                'else : > /tmp_bt/v3c_up/.w 2>/dev/null; fi; }; then '
                '"$BB" rm -f /tmp_bt/v3c_up/.w; _d=/tmp_bt/v3c_up; '
                'else "$BB" rm -rf /tmp_bt/v3c_up 2>/dev/null; '
                'if "$BB" mkdir /tmp/v3c_up 2>/dev/null && : > /tmp/v3c_up/.w 2>/dev/null; then '
                '"$BB" rm -f /tmp/v3c_up/.w; _d=/tmp/v3c_up; fi; fi; '
                'echo "__PROBE_SHA=${_s}__ __PROBE_B64=${_b}__ __PROBE_STAGE=${_d}__"',
                "PROBE_ENV",
            )
            pm = re.search(
                r"__PROBE_SHA=(bb|openssl|none)__\s+__PROBE_B64=(bb|openssl|none)__\s+__PROBE_STAGE=(none|/(?:tmp_bt|tmp)/v3c_up)__",
                probe,
            )
            if not pm:
                raise RuntimeError(f"無法解析相機環境偵測結果: {probe[-300:]}")
            sha_kind, b64_kind, stage_dir = pm.group(1), pm.group(2), pm.group(3)
            if sha_kind == "bb":
                sha_bin = "/bin/busybox sha256sum"
            elif sha_kind == "openssl":
                sha_bin = "/usr/bin/openssl dgst -sha256"
            else:
                raise RuntimeError("相機找不到 sha256sum 或 /usr/bin/openssl，無法安全校驗上傳！")

            if b64_kind == "bb":
                b64_enc = "/bin/busybox base64 <"
                b64_dec = "/bin/busybox base64 -d"
            elif b64_kind == "openssl":
                b64_enc = "/usr/bin/openssl enc -base64 <"
                b64_dec = "/usr/bin/openssl enc -base64 -d"
            else:
                raise RuntimeError("相機找不到 base64 或 /usr/bin/openssl，無法編解碼上傳檔案！")

            if stage_dir == "none":
                raise RuntimeError(f"相機 /tmp_bt 與 /tmp 皆無法寫入，停止安裝: {probe[-200:]}")
            print(f"[*] 相機環境偵測完成 (sha={sha_kind}, b64={b64_kind}, stage={stage_dir})。")

            hook_bytes = None
            hook_sha = ""
            if explicit_hook_path is not None:
                hook_bytes = explicit_hook_path.read_bytes()
                hook_sha = hashlib.sha256(hook_bytes).hexdigest()
            else:
                pre_out = must_cmd(
                    cam,
                    'BB=/bin/busybox; _hk=0; _pl=""; '
                    'if test -r /etc/ld.so.preload && test -s /tmp/a6400-autoexec.so; then '
                    'IFS= read -r _pl < /etc/ld.so.preload 2>/dev/null || :; '
                    'if test "$_pl" = /tmp/a6400-autoexec.so; then _hk=1; fi; fi; '
                    'echo "__HOOK_STATE=${_hk}__"',
                    "CHECK_HOOK",
                )
                hm = re.search(r"__HOOK_STATE=([01])__", pre_out)
                if not hm:
                    raise RuntimeError(f"無法確認相機掛勾狀態: {pre_out[-200:]}")
                if hm.group(1) == "1":
                    print("[*] 偵測到相機已安裝 /etc/ld.so.preload 與 /tmp/a6400-autoexec.so，保留現有掛勾。")
                else:
                    print("[*] 偵測到全新未掛勾機身，正在從相機讀取 /usr/lib/libAK8975.so 並自動修補掛勾...")
                    ak_sha_out = must_cmd(cam, f"{sha_bin} /usr/lib/libAK8975.so", "SHA_LIBAK8975")
                    ak_sha_m = re.search(r"\b([0-9a-f]{64})\b", ak_sha_out.lower())
                    if not ak_sha_m:
                        raise RuntimeError(f"無法取得相機 /usr/lib/libAK8975.so SHA-256: {ak_sha_out[-200:]}")
                    ak_sha = ak_sha_m.group(1)
                    b64_out = must_cmd(
                        cam,
                        f'_p="__AK_"; echo "${{_p}}B64__"; {b64_enc} /usr/lib/libAK8975.so; echo "${{_p}}END__"',
                        "READ_LIBAK8975",
                        timeout=30.0,
                    )
                    in_b64 = False
                    b64_chunks = []
                    for raw_line in b64_out.splitlines():
                        ln = raw_line.strip()
                        if ln == "__AK_B64__":
                            in_b64 = True
                            b64_chunks = []
                            continue
                        if ln == "__AK_END__" and in_b64:
                            in_b64 = False
                            break
                        if in_b64 and re.fullmatch(r"[A-Za-z0-9+/]+={0,2}", ln):
                            b64_chunks.append(ln)
                    if not b64_chunks or in_b64:
                        raise RuntimeError("無法從相機完整讀取 /usr/lib/libAK8975.so（Base64 區塊不完整）")
                    raw_ak = base64.b64decode("".join(b64_chunks), validate=True)
                    got_ak_sha = hashlib.sha256(raw_ak).hexdigest()
                    if got_ak_sha != ak_sha:
                        raise RuntimeError(f"讀取到的 /usr/lib/libAK8975.so SHA-256 不符 ({got_ak_sha} != {ak_sha})")
                    hook_bytes = patch_libak8975(raw_ak)
                    hook_sha = hashlib.sha256(hook_bytes).hexdigest()
                    print(f"[*] 掛勾修補完成 ({len(hook_bytes)} bytes, patched_sha={hook_sha[:12]}...)。")

            must_cmd(
                cam,
                "test ! -f /etc/ld.so.preload || test -f /tmp/ld.so.preload.pre-v3c-backup || /bin/busybox cp -p /etc/ld.so.preload /tmp/ld.so.preload.pre-v3c-backup; "
                "test ! -f /tmp/autoexec.sh || test -f /tmp/autoexec.sh.pre-v3c-backup || /bin/busybox cp -p /tmp/autoexec.sh /tmp/autoexec.sh.pre-v3c-backup; "
                "test ! -f /tmp/a6400-film-boot-v15.tar || test -f /tmp/a6400-film-boot-v15.tar.pre-v3c-backup || /bin/busybox cp -p /tmp/a6400-film-boot-v15.tar /tmp/a6400-film-boot-v15.tar.pre-v3c-backup; "
                "/bin/busybox sync",
                "BACKUP_EXISTING",
            )

            buf = io.BytesIO()
            with tarfile.open(fileobj=buf, mode="w:gz") as tgz:
                tgz.add(boot_sh, arcname="new-autoexec.sh")
                tgz.add(boot_tar, arcname="new-boot.tar")
                if hook_bytes is not None:
                    ti = tarfile.TarInfo("new-autoexec.so")
                    ti.size = len(hook_bytes)
                    ti.mode = 0o755
                    tgz.addfile(ti, io.BytesIO(hook_bytes))
            tgz_bytes = buf.getvalue()
            tgz_sha = hashlib.sha256(tgz_bytes).hexdigest()

            b64 = base64.b64encode(tgz_bytes).decode("ascii")
            b64_lines = [b64[i:i + 64] for i in range(0, len(b64), 64)]
            groups = [b64_lines[i:i + 3] for i in range(0, len(b64_lines), 3)]
            must_cmd(cam, f': > "{stage_dir}/up.b64"', "PREP_UP")
            total = len(groups)
            print(f"[*] 正在寫入開機包 ({len(tgz_bytes)} B, 共 {total} 塊)...", flush=True)
            for idx, grp in enumerate(groups):
                cmds = "; ".join(f"echo {ln}" for ln in grp)
                _, rc = run_cmd(cam, f'{{ {cmds}; }} >> "{stage_dir}/up.b64"', timeout=5.0, fast=True)
                if rc != 0:
                    raise RuntimeError(f"upload chunk {idx}/{total} failed rc={rc}")
                if (idx + 1) % 80 == 0 or idx + 1 == total:
                    print(f"    進度 {idx + 1}/{total}", flush=True)

            dec_out = must_cmd(
                cam,
                f'D="{stage_dir}"; BB=/bin/busybox; {b64_dec} < "$D/up.b64" > "$D/up.tgz" && "$BB" rm -f "$D/up.b64" && {sha_bin} "$D/up.tgz"',
                "DECODE_UPLOAD",
                timeout=20.0,
            )
            if tgz_sha not in dec_out.lower():
                raise RuntimeError("uploaded tgz SHA-256 mismatch")

            unpack = must_cmd(
                cam,
                f'D="{stage_dir}"; BB=/bin/busybox; '
                '("$BB" tar -xzf "$D/up.tgz" -C "$D" 2>/dev/null || "$BB" gzip -dc "$D/up.tgz" | "$BB" tar -xf - -C "$D") && '
                f'"$BB" rm -f "$D/up.tgz" && /bin/sh -n "$D/new-autoexec.sh" && '
                f'{sha_bin} "$D/new-autoexec.sh" && {sha_bin} "$D/new-boot.tar"'
                + (f' && {sha_bin} "$D/new-autoexec.so"' if hook_bytes else ''),
                "UNPACK_UPLOAD",
                timeout=25.0,
            )
            unpack_l = unpack.lower()
            if boot_sh_sha not in unpack_l or boot_tar_sha not in unpack_l:
                raise RuntimeError("uploaded SHA mismatch")
            if hook_bytes and hook_sha not in unpack_l:
                raise RuntimeError("uploaded hook SHA mismatch")

            for name, exp_sha in entry_shas:
                r = must_cmd(
                    cam,
                    f'D="{stage_dir}"; BB=/bin/busybox; "$BB" mkdir "$D/ex" && "$BB" tar -xf "$D/new-boot.tar" -C "$D/ex" "{name}" && {sha_bin} "$D/ex/{name}"; "$BB" rm -rf "$D/ex"',
                    f"VERIFY_{name}",
                )
                if exp_sha not in r.lower():
                    raise RuntimeError(f"entry {name} SHA mismatch")

            install_hook_cmd = ""
            if hook_bytes is not None:
                install_hook_cmd = (
                    '"$BB" cp "$D/new-autoexec.so" /tmp/a6400-autoexec.so.new && '
                    '"$BB" chmod 755 /tmp/a6400-autoexec.so.new && "$BB" sync && '
                    '"$BB" mv /tmp/a6400-autoexec.so.new /tmp/a6400-autoexec.so && '
                )

            must_cmd(
                cam,
                f'BB=/bin/busybox; D="{stage_dir}"; '
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
            if hook_bytes is not None:
                must_cmd(
                    cam,
                    'echo /tmp/a6400-autoexec.so > /etc/ld.so.preload.new && '
                    '/bin/busybox chmod 644 /etc/ld.so.preload.new && /bin/busybox sync && '
                    '/bin/busybox mv /etc/ld.so.preload.new /etc/ld.so.preload && /bin/busybox sync',
                    "INSTALL_PRELOAD",
                )
            final = must_cmd(
                cam,
                f'{sha_bin} /tmp/autoexec.sh && {sha_bin} /tmp/a6400-film-boot-v15.tar && {sha_bin} /tmp/a6400-autoexec.so && '
                '_fok=0; _pl=""; '
                'if test -r /etc/ld.so.preload && test -s /tmp/a6400-autoexec.so; then '
                'IFS= read -r _pl < /etc/ld.so.preload 2>/dev/null || :; '
                'if test "$_pl" = /tmp/a6400-autoexec.so; then _fok=1; fi; fi; '
                'echo "__FINAL_HOOK=${_fok}__"',
                "FINAL_SHA",
            )
            final_l = final.lower()
            if (boot_sh_sha not in final_l or boot_tar_sha not in final_l or
                    (hook_bytes and hook_sha not in final_l) or
                    not re.search(r"__FINAL_HOOK=1__", final)):
                raise RuntimeError("final SHA or preload verification mismatch")
            print("=" * 60)
            print(f"V3_CLITE_DEPLOY_OK package={manifest['package_id']}", flush=True)
            deploy_ok = True
            print("接下來請執行冷開機驗收 4 步：")
            print("  1. 拔除相機 USB 線（若後續要再次連服務模式，請先拆電池再重開）")
            print("  2. 準備 FAT32 記憶卡，在根目錄建立空資料夾：/A6400/TEST/")
            print("  3. 關機 -> 取出電池 10 秒 -> 裝回再開機（冷開機）")
            print("  4. 開機後等 35-40 秒 -> 【必做】輕觸螢幕並放開一次 -> 等約 2 秒 -> 長按 MENU + 放大鏡 2 秒開啟選單")
            print("  ※ 建議在相機選單將「自動檢視 (Auto Review)」設為「關」，拍照時左下角會直接顯示「處理中 -> OK 已存SD」提示條。")
            print("=" * 60)
        finally:
            try:
                cam.setTerminalEnable(False)
            except Exception:
                pass

    def print_usb_help(exc=None):
        if sys.platform == "win32":
            print(
                "\n[!] 尚未完成部署：相機未切為 Mass Storage 模式，或 Windows USB 驅動尚未改為 libusb。\n"
                "請依以下 3 步確認後重跑：\n"
                "  1. 相機保持開機並切為 Mass Storage（大容量儲存）模式接上電腦\n"
                "  2. 以系統管理員執行 Zadig (https://zadig.akeo.ie/) -> Options -> List All Devices -> 選取 ILCE-6400 (或 Sony DSC)\n"
                "  3. 將右側驅動改選為 libusb-win32（若失敗改 WinUSB）-> 點擊 Replace Driver，完成後重跑本腳本！",
                file=sys.stderr,
            )
        else:
            print(
                "\n[!] 尚未完成部署：請確認相機已切為 Mass Storage（大容量儲存）模式並接上 USB（Linux 若無權限請加 sudo）。",
                file=sys.stderr,
            )
        if exc is not None:
            print(f"原始錯誤：{type(exc).__name__}: {exc}", file=sys.stderr)

    try:
        senserShellCommand(driverName="libusb", complete=deploy)
    except Exception as exc:
        print_usb_help(exc)
        sys.exit(1)

    if not deploy_ok:
        print_usb_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
