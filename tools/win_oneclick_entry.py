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


def parse_lang_arg(argv):
    """Extract `--lang tc|sc` or `--lang=...` and return (lang, remaining_argv)."""
    lang = ""
    rest = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--lang", "-l") and i + 1 < len(argv):
            lang = argv[i + 1].strip().lower()
            i += 2
            continue
        if a.startswith("--lang="):
            lang = a.split("=", 1)[1].strip().lower()
            i += 1
            continue
        rest.append(a)
        i += 1
    if lang not in ("", "tc", "sc"):
        raise SystemExit(f"Unsupported --lang '{lang}' (use 'tc' or 'sc')")
    return lang, rest


def select_package_dir(root, lang="", interactive=None):
    """Select Traditional Chinese (tc) or Simplified Chinese (sc) package directory."""
    root = Path(root)
    pkg_tc = root / "local-boot-package-tc"
    pkg_sc = root / "local-boot-package-sc"
    pkg_single = root / "local-boot-package"

    if lang == "sc":
        if pkg_sc.is_dir():
            return pkg_sc
        raise FileNotFoundError(f"找不到簡體中文套件目錄：{pkg_sc}")
    if lang == "tc":
        if pkg_tc.is_dir():
            return pkg_tc
        if pkg_single.is_dir():
            return pkg_single
        raise FileNotFoundError(f"找不到繁體中文套件目錄：{pkg_tc}")

    if pkg_tc.is_dir() and pkg_sc.is_dir():
        is_tty = sys.stdin.isatty() if interactive is None else bool(interactive)
        if not is_tty:
            return pkg_tc
        print(
            "\n請選擇要安裝的介面語言版本（務必與相機系統語言一致）：\n"
            "  [1] 繁體中文版（相機系統語言請設為「繁體中文」）\n"
            "  [2] 簡體中文版（相機系統語言請設為「简体中文」）",
            flush=True,
        )
        while True:
            ans = input("請輸入 1 或 2（直接按 Enter 預設 1 繁體中文）：").strip().lower()
            if ans in ("", "1", "tc"):
                print("[*] 已選擇：繁體中文版", flush=True)
                return pkg_tc
            if ans in ("2", "sc"):
                print("[*] 已選擇：簡體中文版", flush=True)
                return pkg_sc
            print("輸入無效，請輸入 1 或 2。", flush=True)

    if pkg_tc.is_dir():
        return pkg_tc
    if pkg_sc.is_dir():
        return pkg_sc
    if pkg_single.is_dir():
        return pkg_single
    raise FileNotFoundError(f"找不到開機套件目錄：{pkg_single}")


def main():
    if "-h" in sys.argv[1:] or "--help" in sys.argv[1:]:
        sys.exit(run_deployer_argv(["pmca_deploy_v3_clite.py", "--help"]))

    root = base_dir()
    meipass = getattr(sys, "_MEIPASS", "")
    extra_paths = [str(root)] + ([str(meipass)] if meipass else [])
    os.environ["PATH"] = os.pathsep.join(extra_paths) + os.pathsep + os.environ.get("PATH", "")

    if "--restore-driver" in sys.argv:
        sys.exit(windrv.restore_default_drivers())

    lang, rem = parse_lang_arg(sys.argv[1:])
    wdi_exe = root / "wdi-simple.exe"
    hook_file = root / "a6400-autoexec.so"
    stub_candidates = [root / "hook-stub.bin"]
    if meipass:
        stub_candidates.append(Path(meipass) / "hook-stub.bin")
    stub_file = next((p for p in stub_candidates if p.is_file()), None)

    passthrough = [a for a in rem if a != "--no-auto-driver"]
    auto_driver = "--no-auto-driver" not in rem and sys.platform == "win32" and wdi_exe.is_file()

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

    try:
        pkg_dir = select_package_dir(root, lang=lang)
    except FileNotFoundError as exc:
        print(f"[!] {exc}", file=sys.stderr)
        sys.exit(1)

    argv = ["pmca_deploy_v3_clite.py", str(pkg_dir), "--safe-serial", "--yes"]
    if hook_file.is_file():
        argv.extend(["--install-hook", str(hook_file)])
    elif stub_file is not None:
        argv.extend(["--auto-hook-stub", str(stub_file)])
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
