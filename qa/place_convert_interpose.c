#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

/* Forces placeConverted's publish onto AIOFFICE_PLACE_FAIL_DEST to fail, and
 * the first create of export.pdf to fail so native PDF falls through to
 * LibreOffice. A real fsync/rename of any other path is unchanged. Root still
 * honors this (CI runs as root). */

static int (*real_open)(const char *, int, ...) = NULL;
static int (*real_open64)(const char *, int, ...) = NULL;
static int (*real_openat)(int, const char *, int, ...) = NULL;
static int (*real_openat64)(int, const char *, int, ...) = NULL;
static int (*real_link)(const char *, const char *) = NULL;
static int (*real_linkat)(int, const char *, int, const char *, int) = NULL;
static int (*real_rename)(const char *, const char *) = NULL;
static int (*real_renameat)(int, const char *, int, const char *) = NULL;
static int (*real_renameat2)(int, const char *, int, const char *, unsigned int) = NULL;
static int (*real_unlink)(const char *) = NULL;
static int (*real_unlinkat)(int, const char *, int) = NULL;

static int dest_links = 0;

/* Shared across the CLI and the soffice child. The first create of export.pdf
 * is the native writer; later ones are the converter output and must succeed. */
static int next_export_seq(void)
{
    const char *file = getenv("AIOFFICE_EXPORT_PDF_SEQ");
    int fd;
    if (!real_open)
        real_open = dlsym(RTLD_NEXT, "open");
    char buf[32];
    char out[32];
    int cur;
    int next;
    int len;
    ssize_t n;
    if (!file || !file[0] || !real_open)
        return 1;
    fd = real_open(file, O_RDWR | O_CREAT, 0600);
    if (fd < 0)
        return 1;
    if (flock(fd, LOCK_EX) != 0) {
        close(fd);
        return 1;
    }
    memset(buf, 0, sizeof(buf));
    n = read(fd, buf, sizeof(buf) - 1);
    cur = n > 0 ? atoi(buf) : 0;
    next = cur + 1;
    if (ftruncate(fd, 0) == 0 && lseek(fd, 0, SEEK_SET) >= 0) {
        len = snprintf(out, sizeof(out), "%d\n", next);
        if (len > 0 && write(fd, out, (size_t)len) < 0)
            next = cur;
    }
    flock(fd, LOCK_UN);
    close(fd);
    return next;
}

static void log_line(const char *what, const char *path)
{
    const char *log = getenv("AIOFFICE_PLACE_LOG");
    if (!log || !log[0])
        return;
    FILE *f = fopen(log, "a");
    if (!f)
        return;
    fprintf(f, "%s %s\n", what, path ? path : "");
    fclose(f);
}

static int is_export_pdf(const char *path)
{
    const char *base;
    if (!path)
        return 0;
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return strcmp(base, "export.pdf") == 0;
}

static int is_dest(const char *path)
{
    const char *want = getenv("AIOFFICE_PLACE_FAIL_DEST");
    char resolved[4096];
    if (!want || !want[0] || !path)
        return 0;
    if (strcmp(path, want) == 0)
        return 1;
    if (realpath(path, resolved) && strcmp(resolved, want) == 0)
        return 1;
    return 0;
}

static int resolve_at(int dirfd, const char *path, char *out, size_t n)
{
    char linkpath[64];
    char dirpath[4096];
    ssize_t len;
    if (!path)
        return -1;
    if (path[0] == '/') {
        snprintf(out, n, "%s", path);
        return 0;
    }
    snprintf(linkpath, sizeof(linkpath), "/proc/self/fd/%d", dirfd);
    len = readlink(linkpath, dirpath, sizeof(dirpath) - 1);
    if (len < 0)
        return -1;
    dirpath[len] = '\0';
    snprintf(out, n, "%s/%s", dirpath, path);
    return 0;
}

static int fail_first_export_pdf(const char *path, int flags)
{
    const char *on = getenv("AIOFFICE_FAIL_FIRST_EXPORT_PDF");
    if (!on || !on[0] || !is_export_pdf(path))
        return 0;
    if ((flags & O_CREAT) == 0 && (flags & O_WRONLY) == 0 && (flags & O_RDWR) == 0)
        return 0;
    if (next_export_seq() != 1)
        return 0;
    log_line("fail-export-pdf", path);
    errno = EIO;
    return 1;
}

static int fail_dest_write(const char *path, int flags)
{
    if (!is_dest(path))
        return 0;
    if ((flags & O_WRONLY) == 0 && (flags & O_RDWR) == 0 && (flags & O_CREAT) == 0)
        return 0;
    log_line("fail-dest-open", path);
    errno = EIO;
    return 1;
}

static mode_t mode_arg(int flags, va_list ap)
{
    if (flags & O_CREAT)
        return (mode_t)va_arg(ap, int);
    return 0;
}

int open(const char *path, int flags, ...)
{
    va_list ap;
    mode_t mode;
    va_start(ap, flags);
    mode = mode_arg(flags, ap);
    va_end(ap);
    if (!real_open)
        real_open = dlsym(RTLD_NEXT, "open");
    if (fail_first_export_pdf(path, flags) || fail_dest_write(path, flags))
        return -1;
    if (flags & O_CREAT)
        return real_open(path, flags, mode);
    return real_open(path, flags);
}

int open64(const char *path, int flags, ...)
{
    va_list ap;
    mode_t mode;
    va_start(ap, flags);
    mode = mode_arg(flags, ap);
    va_end(ap);
    if (!real_open64)
        real_open64 = dlsym(RTLD_NEXT, "open64");
    if (fail_first_export_pdf(path, flags) || fail_dest_write(path, flags))
        return -1;
    if (flags & O_CREAT)
        return real_open64(path, flags, mode);
    return real_open64(path, flags);
}

int openat(int dirfd, const char *path, int flags, ...)
{
    va_list ap;
    mode_t mode;
    char full[4096];
    const char *check = path;
    va_start(ap, flags);
    mode = mode_arg(flags, ap);
    va_end(ap);
    if (!real_openat)
        real_openat = dlsym(RTLD_NEXT, "openat");
    if (resolve_at(dirfd, path, full, sizeof(full)) == 0)
        check = full;
    if (fail_first_export_pdf(check, flags) || fail_dest_write(check, flags))
        return -1;
    if (flags & O_CREAT)
        return real_openat(dirfd, path, flags, mode);
    return real_openat(dirfd, path, flags);
}

int openat64(int dirfd, const char *path, int flags, ...)
{
    va_list ap;
    mode_t mode;
    char full[4096];
    const char *check = path;
    va_start(ap, flags);
    mode = mode_arg(flags, ap);
    va_end(ap);
    if (!real_openat64)
        real_openat64 = dlsym(RTLD_NEXT, "openat64");
    if (resolve_at(dirfd, path, full, sizeof(full)) == 0)
        check = full;
    if (fail_first_export_pdf(check, flags) || fail_dest_write(check, flags))
        return -1;
    if (flags & O_CREAT)
        return real_openat64(dirfd, path, flags, mode);
    return real_openat64(dirfd, path, flags);
}

int link(const char *old, const char *newpath)
{
    if (!real_link)
        real_link = dlsym(RTLD_NEXT, "link");
    if (is_dest(newpath)) {
        ++dest_links;
        log_line("link-dest", newpath);
        if (dest_links >= 2) {
            errno = EIO;
            return -1;
        }
    }
    return real_link(old, newpath);
}

int linkat(int olddirfd, const char *old, int newdirfd, const char *newpath, int flags)
{
    char full[4096];
    const char *check = newpath;
    if (!real_linkat)
        real_linkat = dlsym(RTLD_NEXT, "linkat");
    if (resolve_at(newdirfd, newpath, full, sizeof(full)) == 0)
        check = full;
    if (is_dest(check) || is_dest(newpath)) {
        ++dest_links;
        log_line("linkat-dest", check);
        if (dest_links >= 2) {
            errno = EIO;
            return -1;
        }
    }
    return real_linkat(olddirfd, old, newdirfd, newpath, flags);
}

int rename(const char *old, const char *newpath)
{
    if (!real_rename)
        real_rename = dlsym(RTLD_NEXT, "rename");
    if (is_dest(newpath)) {
        log_line("fail-rename", newpath);
        errno = EIO;
        return -1;
    }
    return real_rename(old, newpath);
}

int renameat(int olddirfd, const char *old, int newdirfd, const char *newpath)
{
    char full[4096];
    if (!real_renameat)
        real_renameat = dlsym(RTLD_NEXT, "renameat");
    if (resolve_at(newdirfd, newpath, full, sizeof(full)) == 0 && is_dest(full)) {
        log_line("fail-renameat", full);
        errno = EIO;
        return -1;
    }
    if (is_dest(newpath)) {
        log_line("fail-renameat", newpath);
        errno = EIO;
        return -1;
    }
    return real_renameat(olddirfd, old, newdirfd, newpath);
}

int renameat2(int olddirfd, const char *old, int newdirfd, const char *newpath, unsigned int flags)
{
    char full[4096];
    if (!real_renameat2)
        real_renameat2 = dlsym(RTLD_NEXT, "renameat2");
    if (resolve_at(newdirfd, newpath, full, sizeof(full)) == 0 && is_dest(full)) {
        log_line("fail-renameat2", full);
        errno = EIO;
        return -1;
    }
    if (is_dest(newpath)) {
        log_line("fail-renameat2", newpath);
        errno = EIO;
        return -1;
    }
    return real_renameat2(olddirfd, old, newdirfd, newpath, flags);
}

int unlink(const char *path)
{
    if (!real_unlink)
        real_unlink = dlsym(RTLD_NEXT, "unlink");
    if (is_dest(path))
        log_line("unlink-dest", path);
    return real_unlink(path);
}

int unlinkat(int dirfd, const char *path, int flags)
{
    char full[4096];
    if (!real_unlinkat)
        real_unlinkat = dlsym(RTLD_NEXT, "unlinkat");
    if (resolve_at(dirfd, path, full, sizeof(full)) == 0 && is_dest(full))
        log_line("unlinkat-dest", full);
    else if (is_dest(path))
        log_line("unlinkat-dest", path);
    return real_unlinkat(dirfd, path, flags);
}
