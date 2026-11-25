/* fsutil.h - paths, folder scanning and renaming */
#ifndef FSUTIL_H
#define FSUTIL_H

#include <stddef.h>

#define PATHBUF 4096

#if defined(__GNUC__)
#define PRINTF_LIKE(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define PRINTF_LIKE(fmt, args)
#endif

/* snprintf that reports truncation: returns 1 if everything fit, else 0. */
int path_fmt(char *dst, size_t cap, const char *fmt, ...) PRINTF_LIKE(3, 4);

/* Folder part of path ("." if there is none). */
void path_dir(const char *path, char *dir, size_t cap);

/* File name without its last extension. */
void path_stem(const char *name, char *stem, size_t cap);

/* "<path without extension>.fixed.srt". Returns 0 if it does not fit. */
int path_fixed_name(const char *input, char *dst, size_t cap);

int path_exists(const char *path);
int path_same_file(const char *a, const char *b);

/* Finds the largest video file (by size) directly inside dir. Stores its
 * name, without the folder, in name. Returns 1 if one was found. */
int find_largest_video(const char *dir, char *name, size_t cap);

/* Renames target out of the way to <dir>/<base>.old.srt, then .old1.srt, ...
 * (never overwriting). The new path is stored in newpath. Returns 1 on success. */
int move_aside(const char *target, const char *dir, const char *base,
               char *newpath, size_t cap);

#endif
