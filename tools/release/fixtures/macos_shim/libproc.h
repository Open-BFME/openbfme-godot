/* MACPLAY-1 (Sol r3): tests only. Linux stand-ins (from /proc) for the macOS libproc calls tools/release/macos_bundle_swap.c makes, so its
 * process checks (--process-under, --game-running) run in Linux tests too (tools/release/test_macplay.py builds the helper with -I on
 * this folder). Same contracts as Apple's libproc.h: proc_pidinfo returns the bytes written or 0, proc_pidpath the path length or 0,
 * proc_listpids the bytes of pids written. */
#ifndef OPENBFME_LINUX_LIBPROC_SHIM
#define OPENBFME_LINUX_LIBPROC_SHIM
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/fs.h>

/* macOS also supplies these libc functions; older Linux test hosts do not. */
#ifdef __GLIBC__
#if !__GLIBC_PREREQ(2, 38)
static inline size_t openbfme_test_strlcpy(char *dst, const char *src, size_t size) {
    size_t length = strlen(src);
    if (size != 0) {
        size_t copied = length < size ? length : size - 1;
        memcpy(dst, src, copied);
        dst[copied] = '\0';
    }
    return length;
}
#define strlcpy openbfme_test_strlcpy
#endif

#if !__GLIBC_PREREQ(2, 36)
static inline uint32_t openbfme_test_arc4random(void) {
    uint32_t value;
    if (getentropy(&value, sizeof(value)) != 0)
        abort();
    return value;
}
#define arc4random openbfme_test_arc4random
#endif
#endif

#define PROC_ALL_PIDS 1
#define PROC_PIDTBSDINFO 3
#define PROC_PIDPATHINFO_MAXSIZE 4096
#define SZOMB 5
#define RENAME_SWAP 2
#define RENAME_EXCL 4

struct proc_bsdinfo {
    uint32_t pbi_status;
    uid_t pbi_uid;
    uint64_t pbi_start_tvsec;
    uint64_t pbi_start_tvusec;
};

static inline int proc_listpids(uint32_t type, uint32_t info, void *buffer, int size) {
    (void)type;
    (void)info;
    DIR *d = opendir("/proc");
    if (d == NULL)
        return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        char *end;
        long pid = strtol(e->d_name, &end, 10);
        if (*end != '\0' || pid <= 0)
            continue;
        if (buffer != NULL) {
            if ((n + 1) * (int)sizeof(pid_t) > size)
                break;
            ((pid_t *)buffer)[n] = (pid_t)pid;
        }
        ++n;
    }
    closedir(d);
    return n * (int)sizeof(pid_t);
}

static inline int proc_pidinfo(int pid, int flavor, uint64_t arg, void *buffer, int size) {
    (void)arg;
    if (flavor != PROC_PIDTBSDINFO || size != (int)sizeof(struct proc_bsdinfo))
        return 0;
    char path[64];
    struct stat st;
    snprintf(path, sizeof(path), "/proc/%d", pid);
    if (stat(path, &st) != 0)
        return 0;
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return 0;
    char line[4096];
    size_t got = fread(line, 1, sizeof(line) - 1, f);
    fclose(f);
    line[got] = '\0';
    char *close_paren = strrchr(line, ')');
    if (close_paren == NULL)
        return 0;
    char state = close_paren[2];
    unsigned long long start = 0;
    char *field = close_paren + 2;
    for (int i = 3; i < 22 && field != NULL; ++i) {  /* field 3 is the state; field 22 the start time in clock ticks */
        field = strchr(field, ' ');
        if (field != NULL)
            ++field;
    }
    if (field == NULL || sscanf(field, "%llu", &start) != 1)
        return 0;
    long ticks = sysconf(_SC_CLK_TCK);
    struct proc_bsdinfo *info = buffer;
    info->pbi_status = state == 'Z' ? SZOMB : 2;
    info->pbi_uid = st.st_uid;
    info->pbi_start_tvsec = start / (unsigned long long)ticks;
    info->pbi_start_tvusec = (start % (unsigned long long)ticks) * 1000000ULL / (unsigned long long)ticks;
    return size;
}

static inline int proc_pidpath(int pid, void *buffer, uint32_t size) {
    char link[64];
    snprintf(link, sizeof(link), "/proc/%d/exe", pid);
    ssize_t n = readlink(link, buffer, size - 1);
    if (n <= 0)
        return 0;
    ((char *)buffer)[n] = '\0';
    return (int)n;
}

static inline int renameatx_np(int from_dir, const char *from, int to_dir, const char *to, unsigned int flags) {
    unsigned int linux_flags = flags == RENAME_SWAP ? RENAME_EXCHANGE : flags == RENAME_EXCL ? RENAME_NOREPLACE : 0;
    return (int)syscall(SYS_renameat2, from_dir, from, to_dir, to, linux_flags);
}
#endif
