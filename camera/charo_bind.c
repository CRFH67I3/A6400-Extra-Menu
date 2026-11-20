/* Minimal RAM-only MS_BIND helper for V3.2 custom shutter sound. Never touches Flash. */
#include "rt.h"

#define NR_MOUNT 21
#define MS_BIND 4096

struct charo_bind_pair {
    const char *src;
    const char *dst;
    long expected_bytes;
    int required;
};

static const struct charo_bind_pair PAIRS[] = {
    { "/tmp_bt/a6400-charo-shutter.pcm", "/usr/share/avsystemsound/Dslr_Emnt_WPS_PBC.bin",     29760L, 1 },
    { "/tmp_bt/a6400-charo-connect.pcm", "/usr/share/avsystemsound/Dslr_Emnt_WPS_Connect.bin", 24768L, 0 },
    { "/tmp_bt/a6400-charo-push.pcm",    "/usr/share/avsystemsound/Dslr_Emnt_WPS_Push.bin",    18560L, 0 },
};

static int contains(const char *haystack, const char *needle) {
    size_t n = strlen(needle);
    for (const char *p = haystack; *p; ++p)
        if (strncmp(p, needle, n) == 0) return 1;
    return 0;
}

static long file_size(const char *path) {
    int fd = sys_open(path, O_RDONLY, 0);
    if (fd < 0) return -1;
    long sz = sys_lseek(fd, 0, 2);
    sys_close(fd);
    return sz;
}

static int is_mounted(const char *mounts, const char *dst) {
    char pat[128];
    fmt(pat, sizeof pat, " %s ", dst);
    return contains(mounts, pat);
}

static int apply_custom_pcm(const char *src_path, const char *dst_path, long expected_bytes) {
    static unsigned char buf[29760];
    if (expected_bytes != (long)sizeof(buf)) return 1;
    int sfd = sys_open(src_path, O_RDONLY, 0);
    if (sfd < 0) return 2;
    long total = 0;
    while (total < expected_bytes) {
        long r = sys_read(sfd, (char *)buf + total, (size_t)(expected_bytes - total));
        if (r < 0) { sys_close(sfd); return 2; }
        if (r == 0) break;
        total += r;
    }
    char extra[1];
    long more = sys_read(sfd, extra, 1);
    sys_close(sfd);
    if (total <= 0 || more != 0) {
        printf("CHARO_APPLY_SIZE_INVALID bytes=%d more=%d max=%d\n", (int)total, (int)more, (int)expected_bytes);
        return 2;
    }
    for (long i = total; i < expected_bytes; ++i) buf[i] = 0;
    /* Overwrite in-place so any active MS_BIND inode stays live without unlinking. */
    int dfd = sys_open(dst_path, O_WRONLY, 0);
    if (dfd < 0) return 3;
    long wrote = 0;
    while (wrote < expected_bytes) {
        long w = sys_write(dfd, (const char *)buf + wrote, (size_t)(expected_bytes - wrote));
        if (w <= 0) { sys_close(dfd); return 3; }
        wrote += w;
    }
    sys_close(dfd);
    printf("CHARO_APPLY_OK dst=%s in_bytes=%d padded_bytes=%d\n", dst_path, (int)total, (int)wrote);
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc == 3 && strcmp(argv[1], "--apply") == 0) {
        return apply_custom_pcm(argv[2], PAIRS[0].src, PAIRS[0].expected_bytes);
    }
    static char mounts[32768];
    int len = read_file("/proc/mounts", mounts, sizeof mounts);
    if (len <= 0 || len >= (int)sizeof mounts - 1) return 1;
    mounts[len] = 0;

    int failed = 0;
    for (unsigned i = 0; i < sizeof(PAIRS) / sizeof(PAIRS[0]); ++i) {
        const struct charo_bind_pair *p = &PAIRS[i];
        long src_sz = file_size(p->src);
        if (!p->required && src_sz < 0) continue;
        if (is_mounted(mounts, p->dst)) {
            printf("CHARO_BIND_ALREADY dst=%s bytes=%d\n", p->dst, (int)file_size(p->dst));
            continue;
        }
        long dst_sz = file_size(p->dst);
        if (src_sz != p->expected_bytes || dst_sz != p->expected_bytes) {
            printf("CHARO_BIND_SIZE_MISMATCH dst=%s src=%d dst_sz=%d exp=%d\n",
                   p->dst, (int)src_sz, (int)dst_sz, (int)p->expected_bytes);
            failed = 2;
            continue;
        }
        long rc = sys_call(NR_MOUNT, (long)p->src, (long)p->dst, 0, MS_BIND, 0, 0);
        if (rc) {
            printf("CHARO_BIND_MOUNT_ERR dst=%s rc=%d\n", p->dst, (int)rc);
            failed = 3;
            continue;
        }
        printf("CHARO_BIND_OK dst=%s bytes=%d\n", p->dst, (int)file_size(p->dst));
    }
    return failed;
}
