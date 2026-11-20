#!/usr/bin/env python3
"""Build a6400-autoexec.so (installed to /etc/a6400-autoexec.so) from /usr/lib/libAK8975.so.

Uses arm-linux-gnueabihf-as + arm-linux-gnueabihf-objcopy (no keystone needed).
Includes triple killswitch checks (/mnt/disable_hook, /mnt/disable.txt,
/tmp/disable_hook), self-mounted tmpfs fallback for /tmp_bt (2048k) when
Bluetooth is off, volatile lock /tmp_bt/ae.lock, and a 1-line fallback diagnostic
in /tmp/a6400-hook-err.log if lock creation fails.
"""
import argparse
import hashlib
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

PATCH_ADDR = 0x988
LOCK_PATH = "/tmp_bt/ae.lock"
TMP_BT_DIR = "/tmp_bt"
TMPFS_TYPE = "tmpfs"
TMPFS_OPTS = "size=2048k"
AUTOEXEC_SH = "/tmp/autoexec.sh"
HOOK_ERR_PATH = "/tmp/a6400-hook-err.log"
HOOK_ERR_MSG = "HOOK_FAIL ae.lock\n"
HOOK_ERR_ASM = HOOK_ERR_MSG.replace("\n", "\\n")
HOOK_ERR_LEN = len(HOOK_ERR_MSG.encode("ascii"))
ENV_PATH = "PATH=/usr/bin:/bin:/sbin:/usr/sbin:/tmp"

ASM_TEMPLATE = f"""
.syntax unified
.arch armv7-a
.arm
.text
.global _start
_start:
    push {{r4, r5, r6, r7, lr}}
    sub sp, sp, #64

    adr r0, str_mnt_disable_hook
    mov r1, #0
    mov r7, #33               @ sys_access
    svc #0
    cmp r0, #0
    beq exit_clean

    adr r0, str_mnt_disable_txt
    mov r1, #0
    mov r7, #33
    svc #0
    cmp r0, #0
    beq exit_clean

    adr r0, str_tmp_disable_hook
    mov r1, #0
    mov r7, #33
    svc #0
    cmp r0, #0
    beq exit_clean

    adr r0, str_autoexec_sh
    mov r1, #0
    mov r7, #33
    svc #0
    cmp r0, #0
    bne exit_clean

    adr r0, str_autoexec_lock
    mov r1, #193              @ O_WRONLY|O_CREAT|O_EXCL
    mov r2, #420              @ 0644
    mov r7, #5                @ sys_open
    svc #0
    cmp r0, #0
    bge lock_acquired

    cmn r0, #17               @ -EEXIST -> lock already held
    beq exit_clean

    @ /tmp_bt can still be RO rootfs when Bluetooth is off; mount tmpfs and retry.
    adr r0, str_tmpfs
    adr r1, str_tmp_bt
    adr r2, str_tmpfs
    mov r3, #0
    adr r4, str_tmpfs_opts
    mov r7, #21               @ sys_mount
    svc #0

    adr r0, str_autoexec_lock
    mov r1, #193              @ O_WRONLY|O_CREAT|O_EXCL
    mov r2, #420              @ 0644
    mov r7, #5                @ sys_open retry
    svc #0
    cmp r0, #0
    bge lock_acquired

    cmn r0, #17               @ -EEXIST after mount race
    beq exit_clean

    adr r0, str_hook_err
    mov r1, #512              @ O_TRUNC (0x200)
    add r1, r1, #65           @ | O_CREAT | O_WRONLY (total 577)
    mov r2, #420              @ 0644
    mov r7, #5                @ sys_open
    svc #0
    cmp r0, #0
    blt exit_clean
    mov r4, r0
    adr r1, str_hook_err_msg
    mov r2, #{HOOK_ERR_LEN}
    mov r7, #4                @ sys_write
    svc #0
    mov r0, r4
    mov r7, #6                @ sys_close
    svc #0
    b exit_clean

lock_acquired:
    mov r7, #6                @ sys_close
    svc #0

    mov r7, #2                @ sys_fork
    svc #0
    cmp r0, #0
    bne exit_clean

    mov r7, #2                @ double-fork
    svc #0
    cmp r0, #0
    beq in_grandchild
    mov r0, #0
    mov r7, #1                @ sys_exit
    svc #0

in_grandchild:
    mov r7, #66               @ sys_setsid
    svc #0

    adr r0, str_bin_sh
    adr r1, str_autoexec_sh
    mov r2, #0
    str r0, [sp, #0]
    str r1, [sp, #4]
    str r2, [sp, #8]

    adr r3, str_env_path
    str r3, [sp, #16]
    str r2, [sp, #20]

    adr r0, str_bin_sh
    mov r1, sp
    add r2, sp, #16
    mov r7, #11               @ sys_execve
    svc #0

    mov r0, #1
    mov r7, #1
    svc #0

exit_clean:
    add sp, sp, #64
    pop {{r4, r5, r6, r7, pc}}

str_mnt_disable_hook:
    .asciz "/mnt/disable_hook"
    .align 2
str_mnt_disable_txt:
    .asciz "/mnt/disable.txt"
    .align 2
str_tmp_disable_hook:
    .asciz "/tmp/disable_hook"
    .align 2
str_autoexec_sh:
    .asciz "{AUTOEXEC_SH}"
    .align 2
str_autoexec_lock:
    .asciz "{LOCK_PATH}"
    .align 2
str_bin_sh:
    .asciz "/bin/sh"
    .align 2
str_env_path:
    .asciz "{ENV_PATH}"
    .align 2
str_tmpfs:
    .asciz "{TMPFS_TYPE}"
    .align 2
str_tmp_bt:
    .asciz "{TMP_BT_DIR}"
    .align 2
str_tmpfs_opts:
    .asciz "{TMPFS_OPTS}"
    .align 2
str_hook_err:
    .asciz "{HOOK_ERR_PATH}"
    .align 2
str_hook_err_msg:
    .asciz "{HOOK_ERR_ASM}"
    .align 2
"""


def u32(b, off):
    return struct.unpack_from("<I", b, off)[0]


def assemble_arm(asm_text, toolchain_prefix="arm-linux-gnueabihf-"):
    as_bin = shutil.which(f"{toolchain_prefix}as")
    objcopy_bin = shutil.which(f"{toolchain_prefix}objcopy")
    if not as_bin or not objcopy_bin:
        raise RuntimeError(f"Missing {toolchain_prefix}as or {toolchain_prefix}objcopy in PATH")
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        s_file = tmp_path / "hook.s"
        o_file = tmp_path / "hook.o"
        bin_file = tmp_path / "hook.bin"
        s_file.write_text(asm_text)
        subprocess.run([as_bin, str(s_file), "-o", str(o_file)], check=True)
        subprocess.run([objcopy_bin, "-O", "binary", "-j", ".text", str(o_file), str(bin_file)], check=True)
        return bin_file.read_bytes()


def patch_libak8975(src_bytes, code_bytes):
    so = bytearray(src_bytes)
    if so[:4] != b"\x7fELF":
        raise ValueError("Input file is not an ELF binary")
    e_shoff = u32(so, 0x20)
    e_shentsize = struct.unpack_from("<H", so, 0x2E)[0]
    e_shnum = struct.unpack_from("<H", so, 0x30)[0]
    e_shstrndx = struct.unpack_from("<H", so, 0x32)[0]
    shstr_off = u32(so, e_shoff + e_shstrndx * e_shentsize + 0x10)

    text_off = rodata_off = dyn_off = dyn_size = None
    for i in range(e_shnum):
        sh = e_shoff + i * e_shentsize
        name_off = u32(so, sh + 0x00)
        name_end = so.index(b"\x00", shstr_off + name_off)
        name = so[shstr_off + name_off:name_end].decode("ascii", errors="replace")
        sh_type = u32(so, sh + 0x04)
        sh_offset = u32(so, sh + 0x10)
        sh_size = u32(so, sh + 0x14)
        if name == ".text":
            text_off = sh_offset
        elif name == ".rodata":
            rodata_off = sh_offset
        if sh_type == 6:
            dyn_off, dyn_size = sh_offset, sh_size

    if text_off != PATCH_ADDR:
        raise ValueError(f".text at {text_off:#x}, expected {PATCH_ADDR:#x}")
    if rodata_off is None or PATCH_ADDR + len(code_bytes) >= rodata_off:
        raise ValueError("Hook constructor would overrun into .rodata")
    if dyn_off is None:
        raise ValueError("No .dynamic section found")

    so[PATCH_ADDR:PATCH_ADDR + len(code_bytes)] = code_bytes

    null_off = None
    off = dyn_off
    while off < dyn_off + dyn_size:
        if u32(so, off) == 0:
            null_off = off
            break
        off += 8
    if null_off is None or null_off + 16 > dyn_off + dyn_size:
        raise ValueError("No free DT_NULL slot in .dynamic")
    if so[null_off + 8:null_off + 16] != b"\x00" * 8:
        raise ValueError("Slot after DT_NULL is not zero")

    struct.pack_into("<II", so, null_off, 12, PATCH_ADDR)  # DT_INIT
    struct.pack_into("<II", so, null_off + 8, 0, 0)       # DT_NULL
    return bytes(so)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input_libak8975", help="Path to camera-pulled libAK8975.so")
    parser.add_argument("output_so", nargs="?", default="a6400-autoexec.so", help="Output patched .so path")
    args = parser.parse_args()

    src_path = Path(args.input_libak8975).resolve()
    out_path = Path(args.output_so).resolve()
    code = assemble_arm(ASM_TEMPLATE)
    patched = patch_libak8975(src_path.read_bytes(), code)
    out_path.write_bytes(patched)
    out_path.chmod(0o755)
    sha = hashlib.sha256(patched).hexdigest()
    print(f"AUTOEXEC_HOOK_BUILT code_bytes={len(code)} out={out_path} sha256={sha}")


if __name__ == "__main__":
    main()
