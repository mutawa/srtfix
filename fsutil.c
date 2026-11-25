/* fsutil.c - paths, folder scanning and renaming (POSIX) */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64              /* video files over 2 GB */

#include "fsutil.h"

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *VIDEO_EXT[] = {
    "mp4", "mkv", "avi", "mov", "wmv", "m4v", "flv", "webm", "mpg", "mpeg", "ts", "m2ts", NULL
};

int path_fmt(char *dst, size_t cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(dst, cap, fmt, ap);
    va_end(ap);
    return r >= 0 && (size_t)r < cap;
}

static const char *last_sep(const char *p) {
    const char *s = strrchr(p, '/');
#ifdef _WIN32
    const char *b = strrchr(p, '\\');
    if (b && (!s || b > s)) s = b;
#endif
    return s;
}

void path_dir(const char *path, char *dir, size_t cap) {
    const char *sep = last_sep(path);
    if (!sep)             path_fmt(dir, cap, ".");
    else if (sep == path) path_fmt(dir, cap, "/");
    else                  path_fmt(dir, cap, "%.*s", (int)(sep - path), path);
}

void path_stem(const char *name, char *stem, size_t cap) {
    const char *dot = strrchr(name, '.');
    int len = (dot && dot != name) ? (int)(dot - name) : (int)strlen(name);
    path_fmt(stem, cap, "%.*s", len, name);
}

int path_fixed_name(const char *input, char *dst, size_t cap) {
    const char *dot = strrchr(input, '.');
    const char *sep = last_sep(input);
    int stem = (dot && (!sep || dot > sep)) ? (int)(dot - input) : (int)strlen(input);
    return path_fmt(dst, cap, "%.*s.fixed.srt", stem, input);
}

int path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int path_same_file(const char *a, const char *b) {
    struct stat sa, sb;
    return stat(a, &sa) == 0 && stat(b, &sb) == 0 &&
           sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static int is_video(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return 0;
    for (int i = 0; VIDEO_EXT[i]; i++)
        if (ieq(dot + 1, VIDEO_EXT[i])) return 1;
    return 0;
}

int find_largest_video(const char *dir, char *name, size_t cap) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    off_t best = -1;
    char path[PATHBUF];
    while ((e = readdir(d)) != NULL) {
        if (!is_video(e->d_name)) continue;
        if (!path_fmt(path, sizeof path, "%s/%s", dir, e->d_name)) continue;  /* too long: skip */
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (st.st_size > best) {
            best = st.st_size;
            path_fmt(name, cap, "%s", e->d_name);
        }
    }
    closedir(d);
    return best >= 0;
}

int move_aside(const char *target, const char *dir, const char *base,
               char *newpath, size_t cap) {
    for (int k = 0; k < 1000; k++) {
        int ok = (k == 0)
            ? path_fmt(newpath, cap, "%s/%s.old.srt", dir, base)
            : path_fmt(newpath, cap, "%s/%s.old%d.srt", dir, base, k);
        if (!ok) return 0;
        if (!path_exists(newpath))
            return rename(target, newpath) == 0;
    }
    return 0;
}
