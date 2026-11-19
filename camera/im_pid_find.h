#ifndef A6400_IM_PID_FIND_H
#define A6400_IM_PID_FIND_H

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static inline pid_t a6400_find_im_pid(void) {
    DIR *dir = opendir("/proc");
    struct dirent *entry;
    if (!dir) return -1;
    while ((entry = readdir(dir))) {
        char *end = NULL;
        long pid = strtol(entry->d_name, &end, 10);
        if (!*entry->d_name || *end || pid <= 0) continue;
        char path[64], comm[32];
        snprintf(path, sizeof(path), "/proc/%ld/comm", pid);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        int ok = fgets(comm, sizeof(comm), f) &&
                 (!strcmp(comm, "im.elf\n") || !strcmp(comm, "im.elf"));
        fclose(f);
        if (ok) {
            closedir(dir);
            return (pid_t)pid;
        }
    }
    closedir(dir);
    return -1;
}

static inline int a6400_im_proc_path(char *buf, size_t n, const char *suffix) {
    pid_t pid = a6400_find_im_pid();
    if (pid <= 0) return -1;
    if (suffix && *suffix)
        snprintf(buf, n, "/proc/%ld/%s", (long)pid, suffix);
    else
        snprintf(buf, n, "/proc/%ld", (long)pid);
    return 0;
}

#endif
