/* External RGBA4444 OSD preview blitter for V4.0 Instant Pop-up & Album Preview.
 * Runs as an isolated external process (never inside im.elf), reading the
 * UI-published state in a6400_ui_v4_v1 and blitting RGBA4444 rectangles to
 * im.elf's double-buffered OSD_A / OSD_B surfaces via /proc/<pid>/mem.
 * On dismiss or exit, clears drawn regions back to 0x0000 (transparent).
 */
#define _GNU_SOURCE
#include "film_preview_osd_build.h"
#include "film_preview_fmt.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OSD_W 640u
#define OSD_H 480u
#define OSD_STRIDE (OSD_W * 2u)
#define OSD_FRAME_BYTES (OSD_W * OSD_H * 2u) /* 614400 = 0x96000 */

#ifndef FILM_PREVIEW_STOP_FILE
#define FILM_PREVIEW_STOP_FILE "/tmp_bt/a6400-film-capture.stop"
#endif
#ifndef FILM_PREVIEW_PV_DIR
#define FILM_PREVIEW_PV_DIR A6400_PV_DIR
#endif
#ifndef FILM_PREVIEW_PROC_ROOT
#define FILM_PREVIEW_PROC_ROOT "/proc"
#endif

static volatile sig_atomic_t pv_stopping;
static void pv_on_signal(int sig) { (void)sig; pv_stopping = 1; }
static void pv_sleep_ms(unsigned ms) { (void)poll(NULL, 0, (int)ms); }

struct osd_targets {
    pid_t im_pid;
    uint32_t v4_addr;
    uint32_t osd_a;
    uint32_t osd_b;
    int mem_fd;
};

static int pv_stop_present(void) {
    int fd = open(FILM_PREVIEW_STOP_FILE, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd >= 0) { close(fd); return 1; }
    return 0;
}

static pid_t find_im_pid(void) {
#ifdef FILM_PREVIEW_FIXED_PID
    return (pid_t)FILM_PREVIEW_FIXED_PID;
#else
    DIR *dir = opendir(FILM_PREVIEW_PROC_ROOT);
    if (!dir) return -1;
    struct dirent *ent;
    pid_t found = -1;
    while ((ent = readdir(dir))) {
        char *end = NULL;
        long id = strtol(ent->d_name, &end, 10);
        if (!*ent->d_name || *end || id <= 0) continue;
        char path[128], comm[32];
        snprintf(path, sizeof(path), "%s/%ld/comm", FILM_PREVIEW_PROC_ROOT, id);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        int ok = fgets(comm, sizeof(comm), f) &&
                 (!strcmp(comm, "im.elf\n") || !strcmp(comm, "im.elf"));
        fclose(f);
        if (ok) { found = (pid_t)id; break; }
    }
    closedir(dir);
    return found;
#endif
}

static int parse_maps(struct osd_targets *t) {
    char maps_path[128], line[512];
#ifdef FILM_PREVIEW_MAPS_PATH
    snprintf(maps_path, sizeof(maps_path), "%s", FILM_PREVIEW_MAPS_PATH);
#else
    snprintf(maps_path, sizeof(maps_path), "%s/%ld/maps", FILM_PREVIEW_PROC_ROOT, (long)t->im_pid);
#endif
    FILE *f = fopen(maps_path, "r");
    if (!f) return -1;
    uint32_t v4_addr = 0, osd_a = 0, osd_b = 0;
    uint32_t fb_cands[4] = {0};
    unsigned fb_count = 0, img_cnt = 0, st_cnt = 0;
    size_t mod_len = strlen(FILM_PREVIEW_UI_MODULE);

    while (fgets(line, sizeof(line), f)) {
        unsigned lo = 0, hi = 0, off = 0;
        char perms[5] = {0};
        int at = 0;
        if (sscanf(line, "%x-%x %4s %x %*s %*s %n", &lo, &hi, perms, &off, &at) != 4 ||
            at <= 0 || lo >= hi) continue;
        size_t nl = strcspn(line + at, "\r\n");
        if (!off && !strcmp(perms, "r-xp") && nl == mod_len &&
            !memcmp(line + at, FILM_PREVIEW_UI_MODULE, nl)) {
            v4_addr = lo + FILM_PREVIEW_V4_RVA;
            ++img_cnt;
        }
        if (img_cnt == 1u && !strcmp(perms, "rw-p") &&
            v4_addr >= lo && v4_addr + (uint32_t)sizeof(struct a6400_ui_v4_v1) <= hi) {
            ++st_cnt;
        }
        if (!strcmp(perms, "rw-s") && (hi - lo) == OSD_FRAME_BYTES) {
            if (off == 0x23a80000u) osd_a = lo;
            else if (off == 0x23b16000u) osd_b = lo;
            else if (strstr(line + at, "/dev/stream") && fb_count < 4u) {
                fb_cands[fb_count++] = lo;
            }
        }
    }
    fclose(f);
    if (img_cnt != 1u || st_cnt != 1u) return -1;
    if (!osd_a && !osd_b && fb_count == 2u) {
        osd_a = fb_cands[0];
        osd_b = fb_cands[1];
    }
    if (!osd_a || !osd_b) return -1;
    t->v4_addr = v4_addr;
    t->osd_a = osd_a;
    t->osd_b = osd_b;
    return 0;
}

static int pwrite_all(int fd, const void *buf, size_t len, off_t off) {
    const unsigned char *p = (const unsigned char *)buf;
    size_t left = len;
    while (left) {
        ssize_t w = pwrite(fd, p, left, off);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        p += (size_t)w;
        off += w;
        left -= (size_t)w;
    }
    return 0;
}

static int read_exact_file(const char *path, void *buf, size_t want) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return -1;
    unsigned char *p = (unsigned char *)buf;
    size_t got = 0;
    while (got < want) {
        ssize_t n = read(fd, p + got, want - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); return -1; }
        got += (size_t)n;
    }
    close(fd);
    return 0;
}

static void blit_rect(int mem_fd, uint32_t osd_base, unsigned x, unsigned y,
                      unsigned w, unsigned h, const uint16_t *pixels) {
    if (!osd_base || x + w > OSD_W || y + h > OSD_H) return;
    size_t row_bytes = (size_t)w * 2u;
    for (unsigned r = 0; r < h; ++r) {
        off_t dst_off = (off_t)(osd_base + (y + r) * OSD_STRIDE + x * 2u);
        (void)pwrite_all(mem_fd, pixels + (size_t)r * w, row_bytes, dst_off);
    }
}

static void clear_rect(int mem_fd, uint32_t osd_base, unsigned x, unsigned y,
                       unsigned w, unsigned h) {
    static const uint16_t zero_row[OSD_W];
    if (!osd_base || x + w > OSD_W || y + h > OSD_H) return;
    size_t row_bytes = (size_t)w * 2u;
    for (unsigned r = 0; r < h; ++r) {
        off_t dst_off = (off_t)(osd_base + (y + r) * OSD_STRIDE + x * 2u);
        (void)pwrite_all(mem_fd, zero_row, row_bytes, dst_off);
    }
}

static void clear_band(int mem_fd, uint32_t osd_a, uint32_t osd_b, unsigned y0, unsigned h) {
    clear_rect(mem_fd, osd_a, 0u, y0, OSD_W, h);
    clear_rect(mem_fd, osd_b, 0u, y0, OSD_W, h);
}

static uint16_t popup_buf[A6400_POPUP_W * A6400_POPUP_H];
static uint16_t album_band[OSD_W * A6400_ALBUM_BAND_H];
static unsigned char load_tmp[A6400_PV_ALBUM_FILE_BYTES];

static int load_popup_file(void) {
    char path[160];
    snprintf(path, sizeof(path), "%s/popup.bin", FILM_PREVIEW_PV_DIR);
    if (read_exact_file(path, load_tmp, A6400_PV_POPUP_FILE_BYTES)) return -1;
    struct a6400_preview_hdr hdr;
    memcpy(&hdr, load_tmp, sizeof(hdr));
    if (hdr.magic != A6400_PV_MAGIC || hdr.width != A6400_POPUP_W || hdr.height != A6400_POPUP_H) {
        return -1;
    }
    memcpy(popup_buf, load_tmp + sizeof(hdr), A6400_POPUP_BYTES);
    return 0;
}

static int load_album_slot(unsigned slot) {
    char path[160];
    snprintf(path, sizeof(path), "%s/slot_%02u.bin", FILM_PREVIEW_PV_DIR, slot % A6400_ALBUM_SLOTS);
    if (read_exact_file(path, load_tmp, A6400_PV_ALBUM_FILE_BYTES)) return -1;
    struct a6400_preview_hdr hdr;
    memcpy(&hdr, load_tmp, sizeof(hdr));
    if (hdr.magic != A6400_PV_MAGIC || hdr.width != A6400_ALBUM_W || hdr.height != A6400_ALBUM_H) {
        return -1;
    }
    const uint16_t bg_px = 0x112Fu;
    for (unsigned i = 0; i < OSD_W * A6400_ALBUM_BAND_H; ++i) album_band[i] = bg_px;
    const uint16_t *src = (const uint16_t *)(load_tmp + sizeof(hdr));
    unsigned y_off = A6400_ALBUM_Y - A6400_ALBUM_BAND_Y0;
    for (unsigned r = 0; r < A6400_ALBUM_H; ++r) {
        memcpy(album_band + (size_t)(y_off + r) * OSD_W + A6400_ALBUM_X,
               src + (size_t)r * A6400_ALBUM_W,
               A6400_ALBUM_W * 2u);
    }
    return 0;
}

static void clear_active(int mem_fd, const struct osd_targets *t, uint32_t *drawn_mode) {
    if (*drawn_mode == A6400_DISP_POPUP) {
        clear_rect(mem_fd, t->osd_a, A6400_POPUP_X, A6400_POPUP_Y, A6400_POPUP_W, A6400_POPUP_H);
        clear_rect(mem_fd, t->osd_b, A6400_POPUP_X, A6400_POPUP_Y, A6400_POPUP_W, A6400_POPUP_H);
    } else if (*drawn_mode == A6400_DISP_ALBUM) {
        clear_band(mem_fd, t->osd_a, t->osd_b, A6400_ALBUM_BAND_Y0, A6400_ALBUM_BAND_H);
    }
    *drawn_mode = A6400_DISP_NONE;
}

int main(int argc, char **argv) {
    pid_t parent_pid = 0;
    if (argc == 2 && !strcmp(argv[1], "watch")) {
        parent_pid = 0;
    } else if (argc == 3 && !strcmp(argv[1], "watch")) {
        parent_pid = (pid_t)strtol(argv[2], NULL, 10);
    } else {
        return 2;
    }
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, pv_on_signal);
    signal(SIGTERM, pv_on_signal);

    struct osd_targets t = { .im_pid = -1, .v4_addr = 0, .osd_a = 0, .osd_b = 0, .mem_fd = -1 };
    uint32_t drawn_mode = A6400_DISP_NONE;
    uint32_t loaded_seq = 0u, loaded_slot = 0xffffffffu;
    int popup_ready = 0, album_ready = 0;

    while (!pv_stopping && !pv_stop_present()) {
        if (parent_pid > 1 && getppid() != parent_pid) break;
        if (t.mem_fd < 0) {
            t.im_pid = find_im_pid();
            if (t.im_pid > 0 && parse_maps(&t) == 0) {
                char mem_path[128];
#ifdef FILM_PREVIEW_MEM_PATH
                snprintf(mem_path, sizeof(mem_path), "%s", FILM_PREVIEW_MEM_PATH);
#else
                snprintf(mem_path, sizeof(mem_path), "%s/%ld/mem", FILM_PREVIEW_PROC_ROOT, (long)t.im_pid);
#endif
                t.mem_fd = open(mem_path, O_RDWR);
            }
            if (t.mem_fd < 0) {
                pv_sleep_ms(200u);
                continue;
            }
        }

        uint32_t head[8];
        if (pread(t.mem_fd, head, sizeof(head), (off_t)t.v4_addr) != (ssize_t)sizeof(head) ||
            head[0] != A6400_V4_MAGIC || head[1] != A6400_V4_VERSION) {
            clear_active(t.mem_fd, &t, &drawn_mode);
            close(t.mem_fd);
            t.mem_fd = -1;
            pv_sleep_ms(200u);
            continue;
        }
        uint32_t mode = head[5];
        uint32_t dseq = head[6];
        uint32_t slot = head[7];

        if (mode == A6400_DISP_NONE) {
            if (drawn_mode != A6400_DISP_NONE) clear_active(t.mem_fd, &t, &drawn_mode);
            loaded_seq = dseq;
            pv_sleep_ms(35u);
            continue;
        }
        if (mode == A6400_DISP_POPUP) {
            if (drawn_mode == A6400_DISP_ALBUM) clear_active(t.mem_fd, &t, &drawn_mode);
            if (!popup_ready || loaded_seq != dseq) {
                popup_ready = (load_popup_file() == 0);
                loaded_seq = dseq;
            }
            if (popup_ready) {
                blit_rect(t.mem_fd, t.osd_a, A6400_POPUP_X, A6400_POPUP_Y,
                          A6400_POPUP_W, A6400_POPUP_H, popup_buf);
                blit_rect(t.mem_fd, t.osd_b, A6400_POPUP_X, A6400_POPUP_Y,
                          A6400_POPUP_W, A6400_POPUP_H, popup_buf);
                drawn_mode = A6400_DISP_POPUP;
            }
            pv_sleep_ms(25u);
            continue;
        }
        if (mode == A6400_DISP_ALBUM) {
            if (drawn_mode == A6400_DISP_POPUP) clear_active(t.mem_fd, &t, &drawn_mode);
            if (!album_ready || loaded_seq != dseq || loaded_slot != slot) {
                album_ready = (load_album_slot(slot) == 0);
                loaded_seq = dseq;
                loaded_slot = slot;
            }
            if (album_ready) {
                size_t band_bytes = (size_t)OSD_W * A6400_ALBUM_BAND_H * 2u;
                off_t off_a = (off_t)(t.osd_a + A6400_ALBUM_BAND_Y0 * OSD_STRIDE);
                off_t off_b = (off_t)(t.osd_b + A6400_ALBUM_BAND_Y0 * OSD_STRIDE);
                (void)pwrite_all(t.mem_fd, album_band, band_bytes, off_a);
                (void)pwrite_all(t.mem_fd, album_band, band_bytes, off_b);
                drawn_mode = A6400_DISP_ALBUM;
            }
            pv_sleep_ms(35u);
            continue;
        }
        pv_sleep_ms(50u);
    }

    if (t.mem_fd >= 0) {
        clear_active(t.mem_fd, &t, &drawn_mode);
        close(t.mem_fd);
    }
    return 0;
}
