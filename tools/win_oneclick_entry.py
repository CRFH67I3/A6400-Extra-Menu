#!/usr/bin/env python3
"""Bundled Windows one-click entry point for A6400/A6100 deployment without WSL2 or Zadig GUI."""
import os
import sys
import time
from pathlib import Path

import pmca_deploy_v3_clite as deployer
import win_usb_driver as windrv


def base_dir():
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent


def run_deployer_argv(argv):
    old_argv = sys.argv
    sys.argv = argv
    try:
        deployer.main()
        return 0
    except SystemExit as exc:
        return int(exc.code) if isinstance(exc.code, int) else 1
    finally:
        sys.argv = old_argv


def main():
    root = base_dir()
    os.environ["PATH"] = str(root) + os.pathsep + os.environ.get("PATH", "")

    if "--restore-driver" in sys.argv:
        sys.exit(windrv.restore_default_drivers())

    wdi_exe = root / "wdi-simple.exe"
    pkg_dir = root / "local-boot-package"
    hook_file = root / "a6400-autoexec.so"

    passthrough = [a for a in sys.argv[1:] if a != "--no-auto-driver"]
    auto_driver = "--no-auto-driver" not in sys.argv and sys.platform == "win32" and wdi_exe.is_file()

    if any(a in ("--diag", "--rollback") for a in passthrough):
        argv = ["pmca_deploy_v3_clite.py", *passthrough, "--safe-serial", "--yes"]
        if auto_driver:
            windrv.ensure_mass_storage_driver(wdi_exe)
        rc = run_deployer_argv(argv)
        if rc != 0 and auto_driver:
            windrv.ensure_service_driver(wdi_exe)
            time.sleep(2.0)
            rc = run_deployer_argv(argv)
        sys.exit(rc)

    if not pkg_dir.is_dir():
        print(f"[!] 找不到開機套件目錄：{pkg_dir}", file=sys.stderr)
        sys.exit(1)

    argv = ["pmca_deploy_v3_clite.py", str(pkg_dir), "--safe-serial", "--yes"]
    if hook_file.is_file():
        argv.extend(["--install-hook", str(hook_file)])
    argv.extend(passthrough)

    if auto_driver:
        windrv.ensure_mass_storage_driver(wdi_exe)

    rc = run_deployer_argv(argv)
    if rc != 0 and auto_driver:
        print("[*] 偵測到切換服務模式後可能需要補綁 054C:0336 驅動，正在自動處理...", flush=True)
        windrv.ensure_service_driver(wdi_exe)
        time.sleep(3.0)
        rc = run_deployer_argv(argv)
    sys.exit(rc)


if __name__ == "__main__":
    main()
