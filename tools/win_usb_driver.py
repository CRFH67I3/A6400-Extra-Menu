#!/usr/bin/env python3
"""Windows libusb-win32 driver automation (via wdi-simple.exe) and pnputil restore for Sony A6400/A6100."""
import argparse
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


def build_wdi_simple_cmd(wdi_exe, vid, pid, name, dest_dir=None):
    """Build wdi-simple.exe command using --type 1 (libusb-win32, required by Sony-PMCA-RE)."""
    cmd = [
        str(wdi_exe),
        "--vid", f"0x{vid:04X}",
        "--pid", f"0x{pid:04X}",
        "--type", "1",
        "--name", name,
    ]
    if dest_dir:
        cmd.extend(["--dest", str(dest_dir)])
    return cmd


def parse_pnputil_connected_sony_pids(enum_devices_text):
    """Extract connected Sony USB PIDs (VID_054C&PID_XXXX) from `pnputil /enum-devices /connected`."""
    pids = set()
    for m in re.finditer(r"VID_054C&PID_([0-9A-Fa-f]{4})", enum_devices_text):
        pids.add(int(m.group(1), 16))
    return pids


def parse_pnputil_sony_libusb_infs(enum_drivers_text):
    """Extract OEM .inf names installed for Sony Camera libusb-win32 from `pnputil /enum-drivers`."""
    blocks = re.split(r"\r?\n\s*\r?\n", enum_drivers_text)
    matches = []
    for blk in blocks:
        low = blk.lower()
        is_our_pkg = (
            ("sony camera" in low or "sony_camera" in low or "libusb-win32" in low)
            and ("libusb" in low or "wdi" in low or "sony camera" in low)
        )
        if not is_our_pkg:
            continue
        m = re.search(r"(oem\d+\.inf)", blk, re.IGNORECASE)
        if m:
            inf = m.group(1).lower()
            if inf not in matches:
                matches.append(inf)
    return matches


def find_connected_sony_pids():
    """Detect connected Sony USB PIDs via PnP (works even before libusb-win32 is installed)."""
    pids = set()
    if sys.platform == "win32":
        try:
            res = subprocess.run(
                ["pnputil", "/enum-devices", "/connected"],
                capture_output=True,
                text=True,
                check=False,
            )
            pids.update(parse_pnputil_connected_sony_pids(res.stdout or ""))
        except Exception:
            pass
    try:
        import usb.core
        devs = list(usb.core.find(find_all=True, idVendor=SONY_VID) or [])
        pids.update(int(d.idProduct) for d in devs)
    except Exception:
        pass
    return pids


def run_wdi_install(wdi_exe, pid, name):
    tmpdir = tempfile.mkdtemp(prefix="a6400_wdi_")
    try:
        cmd = build_wdi_simple_cmd(wdi_exe, SONY_VID, pid, name, dest_dir=tmpdir)
        print(f"[*] 正在自動綁定 libusb-win32 驅動 (054C:{pid:04X})...", flush=True)
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
        out = (proc.stdout or "") + ("\n" + proc.stderr if proc.stderr else "")
        if proc.returncode != 0:
            print(f"[!] wdi-simple (054C:{pid:04X}) 返回 rc={proc.returncode}: {out.strip()}", flush=True)
            return False
        print(f"[+] 驅動綁定完成 (054C:{pid:04X})", flush=True)
        return True
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def ensure_mass_storage_driver(wdi_exe):
    if sys.platform != "win32":
        return False
    pids = find_connected_sony_pids()
    if SERVICE_PID in pids and not any(p in pids for p in MASS_STORAGE_PIDS):
        return ensure_service_driver(wdi_exe)
    target_pids = [p for p in MASS_STORAGE_PIDS if p in pids] or list(MASS_STORAGE_PIDS)
    ok = False
    for pid in target_pids:
        label = (
            "Sony Camera A6400 Mass Storage (libusb-win32)"
            if pid == 0x0CA8
            else "Sony Camera A6100 Mass Storage (libusb-win32)"
        )
        if run_wdi_install(wdi_exe, pid, label):
            ok = True
    time.sleep(1.5)
    return ok


def ensure_service_driver(wdi_exe):
    if sys.platform != "win32":
        return False
    ok = run_wdi_install(wdi_exe, SERVICE_PID, "Sony Camera Service Mode (libusb-win32)")
    time.sleep(2.0)
    return ok


def restore_default_drivers():
    """Uninstall wdi-simple/Zadig libusb-win32 OEM drivers so Windows restores native Mass Storage."""
    if sys.platform != "win32":
        print("restore_default_drivers only runs on Windows.", file=sys.stderr)
        return 1
    res = subprocess.run(["pnputil", "/enum-drivers"], capture_output=True, text=True, check=False)
    infs = parse_pnputil_sony_libusb_infs(res.stdout or "")
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
