#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Fail or log fsync of a directory fd. File fsyncs and other directories
 * pass through, so Qt's startup caches are not broken. EINVAL and EIO are
 * selected with AIOFFICE_FSYNC_FAIL_ERRNO. Root still honors LD_PRELOAD. */

static int (*real_fsync)(int) = NULL;

static void init_real(void)
{
    if (!real_fsync)
        real_fsync = dlsym(RTLD_NEXT, "fsync");
}

static int fd_path(int fd, char *out, size_t n)
{
    char linkpath[64];
    ssize_t len;
    snprintf(linkpath, sizeof(linkpath), "/proc/self/fd/%d", fd);
    len = readlink(linkpath, out, n - 1);
    if (len < 0)
        return -1;
    out[len] = '\0';
    return 0;
}

int fsync(int fd)
{
    struct stat st;
    char path[4096];
    char resolved[4096];
    const char *log;
    const char *fail_dir;
    const char *fail_err;
    char want[4096];

    init_real();
    if (fstat(fd, &st) != 0 || !S_ISDIR(st.st_mode))
        return real_fsync(fd);
    if (fd_path(fd, path, sizeof(path)) != 0 || !realpath(path, resolved))
        return real_fsync(fd);

    log = getenv("AIOFFICE_FSYNC_LOG");
    if (log && log[0]) {
        FILE *f = fopen(log, "a");
        if (f) {
            fprintf(f, "%s\n", resolved);
            fclose(f);
        }
    }

    fail_dir = getenv("AIOFFICE_FSYNC_FAIL_DIR");
    fail_err = getenv("AIOFFICE_FSYNC_FAIL_ERRNO");
    if (fail_dir && fail_dir[0] && realpath(fail_dir, want) && strcmp(resolved, want) == 0) {
        if (fail_err && strcmp(fail_err, "EINVAL") == 0) {
            errno = EINVAL;
            return -1;
        }
        if (fail_err && strcmp(fail_err, "EIO") == 0) {
            errno = EIO;
            return -1;
        }
    }
    return real_fsync(fd);
}
