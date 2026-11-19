#define _GNU_SOURCE
/* Read-only live-menu job selection. No INI, worker execution or camera writes.
 * The build header pins one reviewed UI artifact and its exact state layout.
 */
#include "film_settings_source.h"
#include "film_settings_gate.h"
#include "film_settings_maps.h"
#include "film_menu_job_build.h"
#include "im_pid_find.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static uint32_t u32at(const unsigned char *b, unsigned off) {
    uint32_t v; memcpy(&v, b + off, 4); return v;
}

static void print_diag(const struct film_settings_source *src) {
    char path[256], line[512];
    snprintf(path, sizeof(path), "%s/maps", src->proc);
    FILE *f = fopen(path, "r");
    struct film_settings_map map = {0};
    if (f) {
        while (fgets(line, sizeof(line), f)) film_settings_map_line(&map, line, src->module, src->rva);
        fclose(f);
    }
    unsigned char buf[FILM_UI_SNAPSHOT_BYTES];
    int rd = -1;
    if (map.image_count == 1 && map.state_count == 1) {
        snprintf(path, sizeof(path), "%s/mem", src->proc);
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            if (pread(fd, buf, sizeof(buf), map.state) == sizeof(buf)) rd = 0;
            close(fd);
        }
    }
    uint32_t now = 0;
    film_settings_now(&now);
    printf("MENU_DIAG proc=%s base=0x%x state=0x%x img=%d st=%d mem=%d now=%u\n",
           src->proc, map.base, map.state, map.image_count, map.state_count, rd, now);
    if (!rd) {
        printf("MENU_DIAG head16=%08x:%08x:%08x:%08x sys=0x%x tid=%u started=%u tick=%u age=%u att=%u run=%u\n",
               u32at(buf, 0), u32at(buf, 4), u32at(buf, 8), u32at(buf, 12),
               u32at(buf, 0x68), u32at(buf, 0xc0), u32at(buf, 0xc4), u32at(buf, 0xc8),
               (unsigned)(now - u32at(buf, 0xc8)), u32at(buf, 0x18c), u32at(buf, 0x190));
    }
}

int main(int argc, char **argv) {
    /* Camera _start returns through SYS_exit, not libc exit. A pipe is fully
     * buffered by default, so returning from main must not discard this
     * machine-readable authorization response. */
    if (setvbuf(stdout, NULL, _IONBF, 0)) {
        printf("FILM_UNAVAILABLE source=%d\n", -18);
        (void)fflush(stdout);
        return 11;
    }
    int diag = (argc == 2 && !strcmp(argv[1], "--diag"));
    char proc_dir[64];
    if (a6400_im_proc_path(proc_dir, sizeof(proc_dir), NULL)) {
        printf("FILM_UNAVAILABLE source=%d\n", -10);
        return 11;
    }
    const struct film_settings_source source = {
        proc_dir, FILM_JOB_MODULE, FILM_JOB_MODULE_BYTES,
        FILM_JOB_MODULE_FNV, FILM_JOB_UI_RVA
    };
    struct film_settings settings = {0};
    int status = film_settings_source_get(&settings, &source);
    if (status) {
        if (diag) print_diag(&source);
        printf("FILM_UNAVAILABLE source=%d\n", status);
        return 11;
    }
    struct film_job job;
    int gate = film_settings_job(&job, &settings);
    if (gate < 0) {
        if (diag) print_diag(&source);
        printf("FILM_UNAVAILABLE source=%d\n", gate);
        return 11;
    }
    if (!gate) {
        if (diag) print_diag(&source);
        puts("FILM_OFF");
        return 10;
    }
    if (diag) print_diag(&source);
    printf("FILM_JOB preset=%s grain=%s halation=%s\n",
           job.preset, job.grain, job.halation);
    return 0;
}
