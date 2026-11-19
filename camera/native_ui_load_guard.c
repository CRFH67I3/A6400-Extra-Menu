/* Dynamic-PID, dynamic-base, ELF-verified RX/RW page-tail guard. */
#define _DEFAULT_SOURCE
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>
#include "im_pid_find.h"
#include "native_freeze.h"

#define STATE_SIZE 1184u
#define STATE_REGION_BYTES 1760u
#define CODE_REGION_BYTES 2296u
#define RX_MOD_COUNT 5u
#define RW_MOD_COUNT 7u
#define MAX_MODS 8

static const unsigned char original[12] = {
    0x2d,0xe9,0xf0,0x47,0x88,0xb0,0x4f,0xf0,0x14,0x08,0x01,0x22
};
static const uint32_t hook_offsets[2] = { 0x5f1d48u, 0x5f1dc0u };
static unsigned char payload[CODE_REGION_BYTES], zeros[CODE_REGION_BYTES], buffer[CODE_REGION_BYTES];

struct mod_cand {
    char path[256];
    uint32_t base;
    uint32_t rx_lo, rx_hi;
    uint32_t rw_hi;
};

static const char *const rx_suffixes[RX_MOD_COUNT] = {
    "/libObj.so", "/libptp.so", "/libmprctrl.so",
    "/libInfraMscServer.so", "/libInfraFsys.so"
};
static const char *const rw_suffixes[RW_MOD_COUNT] = {
    "/libObj.so", "/libcompassutil.so", "/libBizFw.so",
    "/libMWF.so", "/viewUnified6.so", "/libInfraNfc.so",
    "/viewUnified4.so"
};

static struct mod_cand rx_mods[RX_MOD_COUNT];
static struct mod_cand rw_mods[RW_MOD_COUNT];

static pid_t target_pid;
static uint32_t obj_rx_lo_addr, obj_rx_hi_addr;
static uint32_t addr_code, addr_state, addr_hook, addr_dlopen, addr_dlsym;
static uint32_t code_cands[MAX_MODS], state_cands[MAX_MODS];
static unsigned code_cand_count, state_cand_count;

static int readmem(int fd, uint32_t a, void *b, unsigned n) {
    return pread(fd, b, n, a) == (int)n ? 0 : -1;
}

static int poke(uint32_t a, const void *b, unsigned n) {
    if ((a | n) & 3) return -1;
    for (unsigned i = 0; i < n; i += 4) {
        uint32_t word;
        memcpy(&word, (const unsigned char *)b + i, 4);
        if (ptrace(PTRACE_POKETEXT, target_pid, (void *)(uintptr_t)(a + i), (void *)(uintptr_t)word))
            return -1;
    }
    return 0;
}

static int resolve_elf_sym(const char *path, uint32_t map_lo, const char *sym_name, uint32_t *out) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    Elf32_Ehdr eh;
    if (pread(fd, &eh, sizeof(eh), 0) != sizeof(eh) || memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0) {
        close(fd);
        return -1;
    }
    uint32_t first_load_vaddr = 0;
    int got_load = 0;
    for (unsigned i = 0; i < eh.e_phnum; i++) {
        Elf32_Phdr ph;
        if (pread(fd, &ph, sizeof(ph), eh.e_phoff + i * eh.e_phentsize) != sizeof(ph)) break;
        if (ph.p_type == PT_LOAD) {
            first_load_vaddr = ph.p_vaddr;
            got_load = 1;
            break;
        }
    }
    if (!got_load || !eh.e_shnum || eh.e_shentsize != sizeof(Elf32_Shdr)) {
        close(fd);
        return -1;
    }
    Elf32_Shdr *shdrs = (Elf32_Shdr *)calloc(eh.e_shnum, sizeof(Elf32_Shdr));
    if (!shdrs) { close(fd); return -1; }
    if (pread(fd, shdrs, eh.e_shnum * sizeof(Elf32_Shdr), eh.e_shoff) != (ssize_t)(eh.e_shnum * sizeof(Elf32_Shdr))) {
        free(shdrs); close(fd); return -1;
    }
    int found = -1;
    for (unsigned i = 0; i < eh.e_shnum; i++) {
        if (shdrs[i].sh_type != SHT_DYNSYM) continue;
        if (shdrs[i].sh_link >= eh.e_shnum) continue;
        Elf32_Shdr *strsh = &shdrs[shdrs[i].sh_link];
        char *strs = (char *)malloc(strsh->sh_size + 1);
        Elf32_Sym *syms = (Elf32_Sym *)malloc(shdrs[i].sh_size);
        if (!strs || !syms) { free(strs); free(syms); continue; }
        if (pread(fd, strs, strsh->sh_size, strsh->sh_offset) == (ssize_t)strsh->sh_size &&
            pread(fd, syms, shdrs[i].sh_size, shdrs[i].sh_offset) == (ssize_t)shdrs[i].sh_size) {
            strs[strsh->sh_size] = '\0';
            unsigned count = shdrs[i].sh_size / sizeof(Elf32_Sym);
            for (unsigned j = 0; j < count; j++) {
                if (syms[j].st_name < strsh->sh_size &&
                    strcmp(strs + syms[j].st_name, sym_name) == 0 &&
                    syms[j].st_value != 0) {
                    *out = map_lo + (syms[j].st_value - first_load_vaddr);
                    found = 0;
                    break;
                }
            }
        }
        free(strs);
        free(syms);
        if (found == 0) break;
    }
    free(shdrs);
    close(fd);
    return found;
}

static int elf_tail_fits(const char *path, uint32_t seg_base, uint32_t seg_hi, int is_rx, uint32_t need) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    Elf32_Ehdr eh;
    if (pread(fd, &eh, sizeof(eh), 0) != sizeof(eh) ||
        memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_phentsize != sizeof(Elf32_Phdr) || eh.e_phnum == 0 || eh.e_phnum > 32) {
        close(fd);
        return 0;
    }
    Elf32_Phdr loads[8];
    unsigned nload = 0;
    for (unsigned i = 0; i < eh.e_phnum && nload < 8; i++) {
        Elf32_Phdr ph;
        if (pread(fd, &ph, sizeof(ph), eh.e_phoff + i * eh.e_phentsize) != sizeof(ph)) break;
        if (ph.p_type == PT_LOAD) loads[nload++] = ph;
    }
    int ok = 0;
    if (nload >= 1) {
        uint32_t load_bias = seg_base - loads[0].p_vaddr;
        if (is_rx) {
            uint32_t rx_end = load_bias + loads[0].p_vaddr + loads[0].p_memsz;
            if (seg_hi >= rx_end + need) {
                uint32_t map_len = seg_hi - seg_base;
                uint32_t disk_off = loads[0].p_offset + map_len - need;
                unsigned char disk_tail[CODE_REGION_BYTES];
                if (need <= sizeof(disk_tail) &&
                    pread(fd, disk_tail, need, disk_off) == (ssize_t)need &&
                    memcmp(disk_tail, zeros, need) == 0) {
                    ok = 1;
                }
            }
        } else {
            Elf32_Phdr *lw = &loads[nload - 1];
            uint32_t bss_end = load_bias + lw->p_vaddr + lw->p_memsz;
            if (seg_hi >= bss_end + need) ok = 1;
        }
    }
    close(fd);
    return ok;
}

static int resolve_maps(void) {
    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%ld/maps", (long)target_pid);
    FILE *f = fopen(maps_path, "r");
    if (!f) return 0;
    char line[512], dl_path[256] = {0};
    uint32_t dl_rx_lo = 0, last_rw_hi = 0;
    int last_rw_idx = -1;
    obj_rx_lo_addr = obj_rx_hi_addr = 0;
    code_cand_count = state_cand_count = 0;
    memset(rx_mods, 0, sizeof(rx_mods));
    memset(rw_mods, 0, sizeof(rw_mods));

    while (fgets(line, sizeof(line), f)) {
        unsigned lo = 0, hi = 0;
        char perms[5] = {0}, dev[16] = {0}, path[256] = {0};
        unsigned off = 0, inode = 0;
        int n = sscanf(line, "%x-%x %4s %x %15s %u %255s", &lo, &hi, perms, &off, dev, &inode, path);
        if (n < 6) continue;
        if (n == 7 && !strcmp(perms, "r-xp")) {
            last_rw_idx = -1;
            if ((strstr(path, "/libdl-") || strstr(path, "/libdl.so")) && !dl_rx_lo) {
                dl_rx_lo = lo;
                snprintf(dl_path, sizeof(dl_path), "%s", path);
            }
            for (unsigned i = 0; i < RX_MOD_COUNT; i++) {
                if (strstr(path, rx_suffixes[i]) && !rx_mods[i].rx_lo) {
                    rx_mods[i].rx_lo = lo;
                    rx_mods[i].rx_hi = hi;
                    snprintf(rx_mods[i].path, sizeof(rx_mods[i].path), "%s", path);
                    if (i == 0) { obj_rx_lo_addr = lo; obj_rx_hi_addr = hi; }
                }
            }
            for (unsigned i = 0; i < RW_MOD_COUNT; i++) {
                if (strstr(path, rw_suffixes[i]) && !rw_mods[i].base) {
                    rw_mods[i].base = lo;
                    snprintf(rw_mods[i].path, sizeof(rw_mods[i].path), "%s", path);
                }
            }
        } else if (!strcmp(perms, "rw-p")) {
            if (n == 7) {
                last_rw_idx = -1;
                for (unsigned i = 0; i < RW_MOD_COUNT; i++) {
                    if (strstr(path, rw_suffixes[i])) {
                        rw_mods[i].rw_hi = hi;
                        last_rw_idx = (int)i;
                        last_rw_hi = hi;
                        break;
                    }
                }
            } else if (n == 6 && last_rw_idx >= 0 && lo == last_rw_hi && inode == 0) {
                rw_mods[last_rw_idx].rw_hi = hi;
                last_rw_hi = hi;
            } else {
                last_rw_idx = -1;
            }
        } else {
            last_rw_idx = -1;
        }
    }
    fclose(f);

    if (!obj_rx_lo_addr || !dl_rx_lo || !dl_path[0]) return 0;

    for (unsigned i = 0; i < RX_MOD_COUNT; i++) {
        if (rx_mods[i].rx_lo && rx_mods[i].rx_hi > rx_mods[i].rx_lo + CODE_REGION_BYTES &&
            elf_tail_fits(rx_mods[i].path, rx_mods[i].rx_lo, rx_mods[i].rx_hi, 1, CODE_REGION_BYTES)) {
            code_cands[code_cand_count++] = rx_mods[i].rx_hi - CODE_REGION_BYTES;
        }
    }
    for (unsigned i = 0; i < RW_MOD_COUNT; i++) {
        if (rw_mods[i].base && rw_mods[i].rw_hi > STATE_REGION_BYTES &&
            elf_tail_fits(rw_mods[i].path, rw_mods[i].base, rw_mods[i].rw_hi, 0, STATE_REGION_BYTES)) {
            state_cands[state_cand_count++] = rw_mods[i].rw_hi - STATE_REGION_BYTES;
        }
    }
    if (!code_cand_count || !state_cand_count) return 0;
    addr_code = code_cands[0];
    addr_state = state_cands[0];

    if (resolve_elf_sym(dl_path, dl_rx_lo, "dlopen", &addr_dlopen) != 0) return 0;
    if (resolve_elf_sym(dl_path, dl_rx_lo, "dlsym", &addr_dlsym) != 0) return 0;
    addr_hook = obj_rx_lo_addr + hook_offsets[0];
    return 1;
}

static int is_branch_stub(const unsigned char *buf) {
    return buf[0] == 0xdf && buf[1] == 0xf8 && buf[2] == 0x00 && buf[3] == 0xf0 &&
           buf[8] == 0x00 && buf[9] == 0xbf && buf[10] == 0x00 && buf[11] == 0xbf;
}

static int probe_hook(int fd, uint32_t obj_rx_lo, uint32_t obj_rx_hi, int op) {
    for (unsigned i = 0; i < 2; i++) {
        uint32_t cand = obj_rx_lo + hook_offsets[i];
        if (cand + 12u > obj_rx_hi || readmem(fd, cand, buffer, 12)) continue;
        if (op == 2) {
            if (is_branch_stub(buffer)) { addr_hook = cand; return 0; }
        } else if (memcmp(buffer, original, 12) == 0) {
            addr_hook = cand;
            return 0;
        }
    }
    uint32_t scan_lo = obj_rx_lo + 0x580000u;
    uint32_t scan_hi = obj_rx_lo + 0x680000u;
    if (scan_hi > obj_rx_hi) scan_hi = obj_rx_hi;
    unsigned char chunk[1024 + 12];
    uint32_t found = 0;
    unsigned matches = 0;
    for (uint32_t base = scan_lo; base + 12u <= scan_hi; base += 1024u) {
        uint32_t rem = scan_hi - base;
        unsigned want = rem > sizeof(chunk) ? (unsigned)sizeof(chunk) : (unsigned)rem;
        if (pread(fd, chunk, want, base) != (int)want) continue;
        unsigned limit = (want >= 12u) ? (want - 11u) : 0u;
        if (limit > 1024u) limit = 1024u;
        for (unsigned j = 0; j < limit; j += 2u) {
            int hit = (op == 2) ? is_branch_stub(chunk + j) : (memcmp(chunk + j, original, 12u) == 0);
            if (hit) {
                found = base + j;
                if (++matches > 1u) return -1;
            }
        }
    }
    if (matches == 1u) {
        addr_hook = found;
        return 0;
    }
    return -1;
}

int main(void) {
    uint32_t h[8];
    FILE *f = fopen("/tmp_bt/a6400-ui-load.req", "rb");
    if (!f) return 2;
    if (fread(h, 1, sizeof(h), f) != sizeof(h) || h[0] != 0x4d544852 || h[1] != 1 || h[2] > 2 ||
        !h[3] || h[3] > sizeof(payload) || (h[3] % 4) || h[6] != STATE_SIZE ||
        fread(payload, 1, h[3], f) != h[3] || fgetc(f) != EOF) {
        fclose(f);
        return 3;
    }
    fclose(f);
    uint32_t sum = 2166136261u;
    for (unsigned i = 0; i < h[3]; i++) sum = (sum ^ payload[i]) * 16777619u;
    if (sum != h[5]) return 4;

    target_pid = a6400_find_im_pid();
    if (target_pid <= 0) return 5;
    if (!resolve_maps()) return 5;

    unsigned long long t0 = 0, t1 = 0, t2 = 0, t3 = 0, t4 = 0;
    int result = -1, fd = -1, changed = 0;
    for (unsigned attempt = 0; attempt < 6; attempt++) {
        errno = 0; t0 = freeze_ms(); result = freeze_process(); t1 = freeze_ms();
        if (!result) { errno = 0; break; }
        if (result != -95 && result != -97 && result != -98) break;
        usleep(500000);
    }
    if (result) goto end;

    char mem_path[64];
    snprintf(mem_path, sizeof(mem_path), "/proc/%ld/mem", (long)target_pid);
    fd = open(mem_path, h[2] ? O_RDWR : O_RDONLY);
    if (fd < 0) { printf("GUARD_FAIL reason=open_mem errno=%d\n", errno); result = -10; goto done; }

    if (probe_hook(fd, obj_rx_lo_addr, obj_rx_hi_addr, (int)h[2])) {
        printf("GUARD_FAIL reason=probe_hook rx=%08x-%08x\n", obj_rx_lo_addr, obj_rx_hi_addr);
        result = -12; goto done;
    }

    int code_found = 0, state_found = 0;
    if (h[2] == 2) {
        uint32_t installed_entry = 0;
        if (!readmem(fd, addr_hook + 4, &installed_entry, 4)) {
            uint32_t cand_code = installed_entry - 1u;
            for (unsigned ci = 0; ci < code_cand_count; ci++) {
                if (code_cands[ci] == cand_code) {
                    addr_code = cand_code;
                    code_found = 1;
                    break;
                }
            }
        }
        for (unsigned ci = 0; ci < state_cand_count; ci++) {
            uint32_t st[8];
            if (!readmem(fd, state_cands[ci], st, sizeof(st)) &&
                st[0] == 0x4d544c47 && st[1] == 1 && st[5] == 24) {
                addr_state = state_cands[ci];
                state_found = 1;
                break;
            }
        }
        if (!code_found || !state_found) { result = -15; goto done; }
    } else {
        for (unsigned ci = 0; ci < code_cand_count; ci++) {
            if (!readmem(fd, code_cands[ci], buffer, CODE_REGION_BYTES) &&
                !memcmp(buffer, zeros, CODE_REGION_BYTES)) {
                addr_code = code_cands[ci];
                code_found = 1;
                break;
            }
        }
        for (unsigned ci = 0; ci < state_cand_count; ci++) {
            if (!readmem(fd, state_cands[ci], buffer, STATE_REGION_BYTES) &&
                !memcmp(buffer, zeros, STATE_REGION_BYTES)) {
                addr_state = state_cands[ci];
                state_found = 1;
                break;
            }
        }
    }

    uint32_t entry_addr = addr_code + 1u;
    unsigned char branch[12] = {0xdf,0xf8,0,0xf0, 0,0,0,0, 0,0xbf,0,0xbf};
    memcpy(branch + 4, &entry_addr, 4);
    uint32_t cfg[5] = { addr_state, 0u, addr_dlopen, addr_dlsym, addr_hook + 13u };
    memcpy(payload + 4, cfg, sizeof(cfg));

    printf("GUARD_ADDRS op=%u pid=%ld code=%08x(%d) state=%08x(%d) hook=%08x dlopen=%08x dlsym=%08x\n",
           h[2], (long)target_pid, addr_code, code_found, addr_state, state_found, addr_hook, addr_dlopen, addr_dlsym);

    for (unsigned i = 0; i < freeze_count; i++) {
        uint32_t regs[18];
        if (ptrace(PTRACE_GETREGS, freeze_tids[i], 0, regs)) { result = -13; goto done; }
        uint32_t pc = regs[15] & ~1u;
        if ((addr_hook <= pc && pc < addr_hook + 12) || (addr_code <= pc && pc < addr_code + h[3])) {
            result = -14;
            goto done;
        }
    }
    t2 = freeze_ms();

    if (h[2] == 2) {
        uint32_t state[8];
        if (readmem(fd, addr_state, state, sizeof(state)) || state[0] != 0x4d544c47 ||
            state[1] != 1 || state[5] != 24) { result = -15; goto done; }
        uint32_t disabled = 0;
        if (pwrite(fd, &disabled, 4, addr_state + 8) != 4) { result = -16; goto done; }
        if (state[4]) { result = -17; goto done; }
        if (readmem(fd, addr_code, buffer, h[3]) || memcmp(buffer, payload, h[3])) { result = -18; goto done; }
        if (poke(addr_hook, original, 12) || poke(addr_code, zeros, h[3]) ||
            pwrite(fd, zeros, STATE_SIZE, addr_state) != STATE_SIZE) { result = -19; goto done; }
        t3 = freeze_ms();
        if (readmem(fd, addr_hook, buffer, 12) || memcmp(buffer, original, 12) ||
            readmem(fd, addr_code, buffer, h[3]) || memcmp(buffer, zeros, h[3]) ||
            readmem(fd, addr_state, buffer, STATE_SIZE) || memcmp(buffer, zeros, STATE_SIZE)) {
            result = -20;
            goto done;
        }
        printf("REMOVED hook=%08x code_restored=1 state_restored=1\n", addr_hook);
    } else {
        if (!code_found) {
            printf("GUARD_PREFLIGHT_FAIL reason=nonzero_code addr=%08x cands=%u\n", addr_code, code_cand_count);
            result = -21; goto done;
        }
        if (!state_found) {
            printf("GUARD_PREFLIGHT_FAIL reason=nonzero_state addr=%08x cands=%u\n", addr_state, state_cand_count);
            result = -21; goto done;
        }
        printf("CHECK op=%u pid=%ld entry=%08x hook=%08x state=%08x dlopen=%08x dlsym=%08x code_bytes=%u owned_zero_padding=1 pc_clear=1\n",
               h[2], (long)target_pid, entry_addr, addr_hook, addr_state, addr_dlopen, addr_dlsym, h[3]);
        if (h[2] == 1) {
            uint32_t state[8] = { 0x4d544c47, 1, 1, 0, 0, 24, 0, addr_hook };
            changed = 1;
            if (pwrite(fd, state, sizeof(state), addr_state) != sizeof(state) ||
                poke(addr_code, payload, h[3]) ||
                poke(addr_hook, branch, 12)) {
                result = -22;
                goto rollback;
            }
            t3 = freeze_ms();
            if (readmem(fd, addr_hook, buffer, 12) || memcmp(buffer, branch, 12) ||
                readmem(fd, addr_code, buffer, h[3]) || memcmp(buffer, payload, h[3]) ||
                readmem(fd, addr_state, buffer, sizeof(state)) || memcmp(buffer, state, sizeof(state))) {
                result = -23;
                goto rollback;
            }
            printf("INSTALLED selection=0 capacity=24 code_readback=1 hook_readback=1 state_readback=1\n");
        }
    }
    goto done;
rollback:
    if (changed) {
        int ok = !poke(addr_hook, original, 12) && !poke(addr_code, zeros, h[3]) &&
                 pwrite(fd, zeros, STATE_SIZE, addr_state) == STATE_SIZE;
        printf("ROLLBACK result=%d\n", ok ? 0 : -1);
        if (!ok) result = -99;
    }
done:
    t4 = freeze_ms();
    if (fd >= 0) close(fd);
    if (freeze_resume()) result = -99;
end:
    printf("PHASE freeze_ms=%llu preflight_ms=%llu write_ms=%llu verify_ms=%llu resume_ms=%llu total_ms=%llu\n",
           t1 - t0, t2 ? t2 - t1 : 0, t3 ? t3 - t2 : 0, t4 && t3 ? t4 - t3 : 0,
           t4 ? freeze_ms() - t4 : 0, freeze_ms() - t0);
    printf("END result=%d op=%u errno=%d\n", result, h[2], errno);
    fflush(stdout);
    return result ? 6 : 0;
}
