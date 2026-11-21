#!/usr/bin/env python3
"""Windows libusb-win32 driver automation (via wdi-simple.exe) and pnputil restore for Sony A6400/A6100."""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

SONY_VID = 0x054C
MASS_STORAGE_PIDS = (0x0CA8, 0x0D12)  # A6400 (0CA8), A6100 (0D12)
SERVICE_PID = 0x0336


def build_wdi_simple_cmd(wdi_exe, vid, pid, name, dest_dir=None, timeout_ms=60000):
    """Build wdi-simple.exe command using --type 1 (libusb-win32, required by Sony-PMCA-RE)."""
    cmd = [
        str(wdi_exe),
        "--vid", f"0x{vid:04X}",
        "--pid", f"0x{pid:04X}",
        "--type", "1",
        "--timeout", str(timeout_ms),
        "--name", name,
    ]
    if dest_dir:
        cmd.extend(["--dest", str(dest_dir)])
    return cmd


def make_ascii_wdi_tmpdir():
    """Create a pure-ASCII temp directory for wdi-simple --dest (avoids CJK username issues in libwdi ANSI argv)."""
    if sys.platform == "win32":
        sys_root = os.environ.get("SystemRoot", r"C:\Windows")
        win_tmp = Path(sys_root) / "Temp"
        try:
            win_tmp.mkdir(parents=True, exist_ok=True)
            cand = tempfile.mkdtemp(prefix="a6400_wdi_", dir=str(win_tmp))
            if cand.isascii():
                return cand
        except Exception:
            pass
    return tempfile.mkdtemp(prefix="a6400_wdi_")


def parse_pnputil_connected_sony_pids(enum_devices_text):
    """Extract connected Sony USB PIDs (VID_054C&PID_XXXX) from `pnputil /enum-devices /connected`."""
    pids = set()
    for m in re.finditer(r"VID_054C&PID_([0-9A-Fa-f]{4})", enum_devices_text):
        pids.add(int(m.group(1), 16))
    return pids


def parse_pnputil_sony_libusb_infs(enum_drivers_text, role=None):
    """Extract OEM .inf names installed for Sony Camera libusb-win32 from `pnputil /enum-drivers`.

    role: None (all Sony libusb-win32 INFs), 'service' (service mode INFs only), or 'mass' (mass storage INFs only).
    """
    blocks = re.split(r"\r?\n\s*\r?\n", enum_drivers_text)
    matches = []
    for blk in blocks:
        low = blk.lower()
        is_our_pkg = (
            (
                "sony camera" in low
                or "sony_camera" in low
                or "ilce-6400" in low
                or "ilce-6100" in low
                or "sony_dsc" in low
                or "sony_a6400" in low
                or "libusb-win32" in low
            )
            and ("libusb" in low or "wdi" in low or "sony camera" in low)
        )
        if not is_our_pkg:
            continue
        is_service = "service" in low or "0336" in low
        if role == "service" and not is_service:
            continue
        if role == "mass" and is_service:
            continue
        m = re.search(r"(oem\d+\.inf)", blk, re.IGNORECASE)
        if m:
            inf = m.group(1).lower()
            if inf not in matches:
                matches.append(inf)
    return matches


def deduplicate_sony_libusb_infs(role=None, keep_latest=1):
    """Delete duplicate accumulated oemXX.inf packages for the given role so Windows never binds a stale INF."""
    if sys.platform != "win32":
        return []
    try:
        res = subprocess.run(["pnputil", "/enum-drivers"], capture_output=True, check=False)
        out_txt = (res.stdout or b"").decode("utf-8", errors="replace")
        infs = parse_pnputil_sony_libusb_infs(out_txt, role=role)
    except Exception:
        return []
    if len(infs) <= keep_latest:
        return []
    # Sort numerically by oem<N>.inf so we keep the highest/newest N if keep_latest > 0.
    def _oem_num(s):
        m = re.search(r"oem(\d+)\.inf", s)
        return int(m.group(1)) if m else 0
    infs_sorted = sorted(infs, key=_oem_num)
    to_remove = infs_sorted if keep_latest <= 0 else infs_sorted[:-keep_latest]
    for inf in to_remove:
        print(f"[*] 正在自動清理舊重複驅動包 {inf} (避免多包衝突導致 Bulk 通道超時)...", flush=True)
        subprocess.run(["pnputil", "/delete-driver", inf, "/uninstall", "/force"], capture_output=True, check=False)
    return to_remove


def is_libusb_win32_ready(pid):
    """Return True if 054C:pid is already bound to libusb-win32 (libusb0.sys) and visible to pyusb libusb0."""
    try:
        import usb.core
        import usb.util
        try:
            import usb.backend.libusb0 as b0
            be = b0.get_backend()
        except Exception:
            be = None
        dev = usb.core.find(idVendor=SONY_VID, idProduct=pid, backend=be) if be is not None else usb.core.find(idVendor=SONY_VID, idProduct=pid)
        if dev is not None:
            try:
                usb.util.dispose_resources(dev)
            except Exception:
                pass
            return True
    except Exception:
        pass
    return False


def kill_stale_wdi_processes():
    """Terminate any orphaned wdi-simple / installer_x64 process holding the Windows PnP install mutex."""
    if sys.platform != "win32":
        return
    for exe in ("installer_x64.exe", "installer_x86.exe", "wdi-simple.exe"):
        try:
            subprocess.run(["taskkill", "/F", "/IM", exe, "/T"], capture_output=True, check=False)
        except Exception:
            pass


def find_connected_sony_pids():
    """Detect connected Sony USB PIDs via PnP (works even before libusb-win32 is installed)."""
    pids = set()
    if sys.platform == "win32":
        try:
            res = subprocess.run(
                ["pnputil", "/enum-devices", "/connected"],
                capture_output=True,
                check=False,
            )
            out_txt = (res.stdout or b"").decode("utf-8", errors="replace")
            pids.update(parse_pnputil_connected_sony_pids(out_txt))
        except Exception:
            pass
    if not pids:
        try:
            import usb.core
            import usb.util
            devs = list(usb.core.find(find_all=True, idVendor=SONY_VID) or [])
            for d in devs:
                pids.add(int(d.idProduct))
                try:
                    usb.util.dispose_resources(d)
                except Exception:
                    pass
        except Exception:
            pass
    return pids


def wait_for_sony_pid(target_pid, wait_sec=10.0):
    """Wait briefly for Windows PnP to enumerate target Sony PID."""
    deadline = time.monotonic() + wait_sec
    while time.monotonic() < deadline:
        if target_pid in find_connected_sony_pids() or is_libusb_win32_ready(target_pid):
            return True
        time.sleep(0.5)
    return target_pid in find_connected_sony_pids() or is_libusb_win32_ready(target_pid)


def _print_wdi_troubleshoot_hint(pid):
    print(
        f"[!] 自動安裝驅動 (054C:{pid:04X}) 未成功，常見排查方式：\n"
        "    1. 暫時退出 360 / 火絨 / 電腦管家等安全軟體（可能攔截 libusb-win32 驅動寫入）\n"
        "    2. Win11 進「Windows 安全性 -> 裝置安全性 -> 核心隔離」確認暫時關閉「記憶體完整性」\n"
        "    3. 改插主機板後方 USB 孔（勿經擴充塢/Hub），相機重插後靜置 8 秒\n"
        "    4. 先執行「一鍵還原預設讀卡機.bat」清理舊驅動，再將相機拔線關機重開後重試",
        flush=True,
    )


def run_wdi_install(wdi_exe, pid, name, force=False):
    role = "service" if pid == SERVICE_PID else "mass"
    if not force and is_libusb_win32_ready(pid):
        deduplicate_sony_libusb_infs(role=role, keep_latest=1)
        print(f"[+] 偵測到 libusb-win32 驅動已就緒 (054C:{pid:04X})，略過重複安裝", flush=True)
        return True
    kill_stale_wdi_processes()
    # Delete old duplicate oemXX.inf packages for this role before installing so only 1 clean INF exists.
    deduplicate_sony_libusb_infs(role=role, keep_latest=0)
    tmpdir = make_ascii_wdi_tmpdir()
    try:
        cmd = build_wdi_simple_cmd(wdi_exe, SONY_VID, pid, name, dest_dir=tmpdir)
        print(f"[*] 正在自動綁定 libusb-win32 驅動 (054C:{pid:04X})，請稍候 10~40 秒...", flush=True)
        try:
            proc = subprocess.run(cmd, timeout=95, check=False)
        except subprocess.TimeoutExpired:
            kill_stale_wdi_processes()
            print(f"[!] wdi-simple (054C:{pid:04X}) 逾時 95 秒，已清理背景殘留程序。", flush=True)
            _print_wdi_troubleshoot_hint(pid)
            return False
        if proc.returncode != 0:
            if is_libusb_win32_ready(pid):
                print(f"[+] libusb-win32 驅動已可用 (054C:{pid:04X})", flush=True)
                return True
            print(f"[!] wdi-simple (054C:{pid:04X}) 返回 rc={proc.returncode}", flush=True)
            _print_wdi_troubleshoot_hint(pid)
            return False
        print(f"[+] 驅動綁定完成 (054C:{pid:04X})", flush=True)
        return True
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def ensure_mass_storage_driver(wdi_exe, force=False):
    if sys.platform != "win32":
        return False
    if not force:
        if is_libusb_win32_ready(SERVICE_PID):
            deduplicate_sony_libusb_infs(role="service", keep_latest=1)
            print(f"[+] 偵測到相機已在服務模式 (054C:{SERVICE_PID:04X}) 且驅動已就緒，略過重複安裝", flush=True)
            return True
        for pid in MASS_STORAGE_PIDS:
            if is_libusb_win32_ready(pid):
                deduplicate_sony_libusb_infs(role="mass", keep_latest=1)
                print(f"[+] 偵測到 libusb-win32 驅動已就緒 (054C:{pid:04X})，略過重複安裝", flush=True)
                return True

    pids = find_connected_sony_pids()
    if not pids:
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline and not pids:
            time.sleep(0.5)
            pids = find_connected_sony_pids()

    if SERVICE_PID in pids and not any(p in pids for p in MASS_STORAGE_PIDS):
        return ensure_service_driver(wdi_exe, force=force)

    target_pids = [p for p in MASS_STORAGE_PIDS if p in pids]
    if not target_pids:
        print(
            "[!] Windows 尚未偵測到相機 USB 裝置 (054C:0CA8 / 0D12 / 0336)。"
            "若相機黑屏或剛才中斷過，請拔線 → 關機重開 → 重新插線靜置 8 秒後再試。",
            flush=True,
        )
        return False

    ok = False
    for pid in target_pids:
        label = (
            "Sony Camera A6400 Mass Storage (libusb-win32)"
            if pid == 0x0CA8
            else "Sony Camera A6100 Mass Storage (libusb-win32)"
        )
        if run_wdi_install(wdi_exe, pid, label, force=force):
            ok = True
    time.sleep(1.0)
    return ok


def ensure_service_driver(wdi_exe, force=False):
    if sys.platform != "win32":
        return False
    if not force and is_libusb_win32_ready(SERVICE_PID):
        deduplicate_sony_libusb_infs(role="service", keep_latest=1)
        print(f"[+] 偵測到服務模式 libusb-win32 驅動已就緒 (054C:{SERVICE_PID:04X})，略過重複安裝", flush=True)
        return True
    if not wait_for_sony_pid(SERVICE_PID, wait_sec=12.0):
        print(
            "[!] 未偵測到服務模式裝置 (054C:0336)。若相機黑屏或卡死，請拔線 → 關機取電池 10 秒 → 裝回開機插線靜置 8 秒後重試。",
            flush=True,
        )
        return False
    # Give Windows PnP up to 4 seconds to finish starting existing libusb0.sys on 054C:0336 before invoking wdi-simple.
    if not force:
        deadline = time.monotonic() + 4.0
        while time.monotonic() < deadline:
            if is_libusb_win32_ready(SERVICE_PID):
                deduplicate_sony_libusb_infs(role="service", keep_latest=1)
                print(f"[+] 偵測到服務模式 libusb-win32 驅動已就緒 (054C:{SERVICE_PID:04X})，略過重複安裝", flush=True)
                return True
            time.sleep(0.4)
    ok = run_wdi_install(wdi_exe, SERVICE_PID, "Sony Camera Service Mode (libusb-win32)", force=force)
    time.sleep(1.5)
    return ok


def restore_default_drivers():
    """Uninstall wdi-simple/Zadig libusb-win32 OEM drivers so Windows restores native Mass Storage."""
    if sys.platform != "win32":
        print("restore_default_drivers only runs on Windows.", file=sys.stderr)
        return 1
    kill_stale_wdi_processes()
    res = subprocess.run(["pnputil", "/enum-drivers"], capture_output=True, check=False)
    out_txt = (res.stdout or b"").decode("utf-8", errors="replace")
    infs = parse_pnputil_sony_libusb_infs(out_txt)
    if not infs:
        print("[*] 未偵測到已安裝的 Sony libusb-win32 自訂驅動（可能已為原廠預設狀態）。", flush=True)
    for inf in infs:
        print(f"[*] 正在卸載自訂驅動 {inf} ...", flush=True)
        subprocess.run(["pnputil", "/delete-driver", inf, "/uninstall", "/force"], check=False)
    subprocess.run(["pnputil", "/scan-devices"], check=False)
    print("[+] 已還原 Windows 預設讀卡機驅動設定，請重新插拔相機 USB。", flush=True)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["install-mass", "install-service", "restore"])
    parser.add_argument("--wdi-exe", default="wdi-simple.exe", help="Path to wdi-simple.exe")
    args = parser.parse_args()

    if args.action == "restore":
        sys.exit(restore_default_drivers())
    wdi = Path(args.wdi_exe)
    if not wdi.is_file():
        print(f"找不到 {wdi}", file=sys.stderr)
        sys.exit(1)
    if args.action == "install-mass":
        ensure_mass_storage_driver(wdi)
    elif args.action == "install-service":
        ensure_service_driver(wdi)


if __name__ == "__main__":
    main()
