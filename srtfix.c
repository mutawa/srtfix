#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64   /* video files over 2 GB */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdarg.h>

#define PATHBUF 4096

enum { LINE_TEXT = 0, LINE_SEQ = 1, LINE_TIME = 2 };
enum { TXT_SAME = 0, TXT_CONVERTED = 1, TXT_REPAIRED = 2 };

typedef struct {
    int    line;      /* index of the timestamp line */
    long   seq;       /* sequence number from the line before it */
    double start, end;
    char  *tail;      /* anything after the end time (e.g. position tags) */
} Cue;

/* windows-1256 0x80..0xFF -> Unicode */
static const unsigned short CP1256[128] = {
    0x20AC,0x067E,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,
    0x02C6,0x2030,0x0679,0x2039,0x0152,0x0686,0x0698,0x0688,
    0x06AF,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,
    0x06A9,0x2122,0x0691,0x203A,0x0153,0x200C,0x200D,0x06BA,
    0x00A0,0x060C,0x00A2,0x00A3,0x00A4,0x00A5,0x00A6,0x00A7,
    0x00A8,0x00A9,0x06BE,0x00AB,0x00AC,0x00AD,0x00AE,0x00AF,
    0x00B0,0x00B1,0x00B2,0x00B3,0x00B4,0x00B5,0x00B6,0x00B7,
    0x00B8,0x00B9,0x061B,0x00BB,0x00BC,0x00BD,0x00BE,0x061F,
    0x06C1,0x0621,0x0622,0x0623,0x0624,0x0625,0x0626,0x0627,
    0x0628,0x0629,0x062A,0x062B,0x062C,0x062D,0x062E,0x062F,
    0x0630,0x0631,0x0632,0x0633,0x0634,0x0635,0x0636,0x00D7,
    0x0637,0x0638,0x0639,0x063A,0x0640,0x0641,0x0642,0x0643,
    0x00E0,0x0644,0x00E2,0x0645,0x0646,0x0647,0x0648,0x00E7,
    0x00E8,0x00E9,0x00EA,0x00EB,0x0649,0x064A,0x00EE,0x00EF,
    0x064B,0x064C,0x064D,0x064E,0x00F4,0x064F,0x0650,0x00F7,
    0x0651,0x00F9,0x0652,0x00FB,0x00FC,0x200E,0x200F,0x06D2
};

/* windows-1252 0x80..0x9F -> Unicode (used to undo the 1252 misreading) */
static const unsigned short CP1252[32] = {
    0x20AC,0x0081,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,
    0x02C6,0x2030,0x0160,0x2039,0x0152,0x008D,0x017D,0x008F,
    0x0090,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,
    0x02DC,0x2122,0x0161,0x203A,0x0153,0x009D,0x017E,0x0178
};

static const char *VIDEO_EXT[] = {
   "mp4", "mkv", "avi", "mov", "wmv", "m4v", "flv", "webm", "mpg", "mpeg", "ts", "mts", NULL
};


static void usage(void) {
    fprintf(stderr,
        "Usage: srtfix <file.srt> [-e] [-s <ms>] [-t <anchorSeq> <targetSeq> <hh:mm:ss,fff>] [-r] [-o <out.srt>]\n"
        "  -e   fix subtitle text encoding -> UTF-8. Handles genuine windows-1256 bytes and\n"
        "       1256 text that was misread as 1252 and saved as UTF-8 (mojibake)\n"
        "  -s   shift all timestamps by <ms> (negative allowed)\n"
        "  -t   keep cue <anchorSeq> fixed; cue <targetSeq> should start at the given time;\n"
        "       linearly stretch/shrink everything from the anchor to the end of the file\n"
	"  -r   name the result after the largest video file in the same folder\n"
	"       (an existing <video>.srt is renamed to <video>.old.srt first)\n"
        "  -o   output file (default: <name>.fixed.srt)\n");
}

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    *len = fread(buf, 1, (size_t)n, f);
    buf[*len] = '\0';
    fclose(f);
    return buf;
}

/* ---------- file / folder helpers ---------- */

static const char *last_sep(const char *p) {
    const char *s = strrchr(p, '/');
#ifdef _WIN32
    const char *b = strrchr(p, '\\');
    if (b && (!s || b > s)) s = b;
#endif
    return s;
}

static void dir_of(const char *path, char *dir, size_t cap) {
    const char *sep = last_sep(path);
    if (!sep)              snprintf(dir, cap, ".");
    else if (sep == path)  snprintf(dir, cap, "/");
    else                   snprintf(dir, cap, "%.*s", (int)(sep - path), path);
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

static int exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int same_file(const char *a, const char *b) {
    struct stat sa, sb;
    return stat(a, &sa) == 0 && stat(b, &sb) == 0 &&
           sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

/* snprintf that reports truncation: returns 1 if everything fit, else 0 */
static int path_fmt(char *dst, size_t cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(dst, cap, fmt, ap);
    va_end(ap);
    return r >= 0 && (size_t)r < cap;
}

/* Finds the largest video file in dir. Returns 1 and the file name (no folder) in name. */
static int find_largest_video(const char *dir, char *name, size_t cap) {
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
            snprintf(name, cap, "%s", e->d_name);
        }
    }
    closedir(d);
    return best >= 0;
}

/* Renames target out of the way: <base>.old.srt, then <base>.old1.srt, ... */
static int move_aside(const char *target, const char *dir, const char *base,
                      char *newpath, size_t cap) {
    for (int k = 0; k < 1000; k++) {
        int ok = (k == 0)
            ? path_fmt(newpath, cap, "%s/%s.old.srt", dir, base)
            : path_fmt(newpath, cap, "%s/%s.old%d.srt", dir, base, k);
        if (!ok) return 0;
        if (!exists(newpath))
            return rename(target, newpath) == 0;
    }
    return 0;
}
/* ---------- encoding helpers ---------- */

/* Strict decode of one 1-3 byte UTF-8 sequence. Returns its length, or 0 if invalid. */
static int utf8_decode(const unsigned char *s, unsigned *cp) {
    if (s[0] < 0x80) { *cp = s[0]; return 1; }
    if (s[0] >= 0xC2 && s[0] <= 0xDF && (s[1] & 0xC0) == 0x80) {
        *cp = ((s[0] & 0x1Fu) << 6) | (s[1] & 0x3Fu);
        return 2;
    }
    if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *cp = ((s[0] & 0x0Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu);
        return *cp >= 0x800 ? 3 : 0;
    }
    return 0;
}

static void put_utf8(FILE *out, unsigned cp) {
    if (cp < 0x80) {
        fputc(cp, out);
    } else if (cp < 0x800) {
        fputc(0xC0 | (cp >> 6), out);
        fputc(0x80 | (cp & 0x3F), out);
    } else {
        fputc(0xE0 | (cp >> 12), out);
        fputc(0x80 | ((cp >> 6) & 0x3F), out);
        fputc(0x80 | (cp & 0x3F), out);
    }
}

/* Unicode code point -> the windows-1252 byte that decodes to it, or -1 */
static int cp1252_byte(unsigned cp) {
    if (cp < 0x80) return (int)cp;
    if (cp >= 0xA0 && cp <= 0xFF) return (int)cp;
    for (int i = 0; i < 32; i++)
        if (CP1252[i] == cp) return 0x80 + i;
    return -1;
}

/* Writes one text line, fixing its encoding if convert is set. */
static int put_text(FILE *out, const char *line, int convert) {
    if (!convert) { fputs(line, out); return TXT_SAME; }

    const unsigned char *s = (const unsigned char *)line, *p;
    unsigned cp;
    int len, ascii = 1, valid_utf8 = 1, all_1252 = 1, has_arabic = 0;

    /* Pass 1: classify the line */
    for (p = s; *p; ) {
        if (*p < 0x80) { p++; continue; }
        ascii = 0;
        len = utf8_decode(p, &cp);
        if (!len) { valid_utf8 = 0; break; }
        int b = cp1252_byte(cp);
        if (b < 0) {
            all_1252 = 0;
        } else if (b >= 0x80) {
            unsigned u = CP1256[b - 0x80];
            if (u >= 0x0600 && u <= 0x06FF) has_arabic = 1;
        }
        p += len;
    }

    if (ascii) { fputs(line, out); return TXT_SAME; }

    if (!valid_utf8) {                       /* genuine windows-1256 bytes */
        for (p = s; *p; p++)
            put_utf8(out, *p < 0x80 ? *p : CP1256[*p - 0x80]);
        return TXT_CONVERTED;
    }

    if (all_1252 && has_arabic) {            /* mojibake: UTF-8 of 1252-decoded 1256 bytes */
        for (p = s; *p; ) {
            len = utf8_decode(p, &cp);
            int b = cp1252_byte(cp);         /* back to the original byte */
            put_utf8(out, b < 0x80 ? (unsigned)b : CP1256[b - 0x80]);
            p += len;
        }
        return TXT_REPAIRED;
    }

    fputs(line, out);                        /* already proper UTF-8 */
    return TXT_SAME;
}

/* ---------- time helpers ---------- */

static int parse_time(const char *s, long *ms) {
    int h, m, sec, f;
    if (sscanf(s, "%d:%d:%d%*[,.]%d", &h, &m, &sec, &f) != 4) return 0;
    *ms = h * 3600000L + m * 60000L + sec * 1000L + f;
    return 1;
}

static void fmt_time(FILE *out, double x) {
    if (x < 0) x = 0;                        /* never emit negative times */
    long ms = (long)(x + 0.5);
    fprintf(out, "%02ld:%02ld:%02ld,%03ld",
            ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 1; }

    const char *input = argv[1];
    char *output = NULL;
    int convert = 0, stretch = 0, rename_to_video = 0;
    long shift = 0, anchorSeq = 0, targetSeq = 0, correct = 0;
    char dir[PATHBUF], vname[PATHBUF], base[PATHBUF], target[PATHBUF], aside[PATHBUF];

    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-e")) {
            convert = 1;
	} else if (!strcmp(argv[i], "-r")) {
	    rename_to_video = 1;
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            shift = atol(argv[++i]);
        } else if (!strcmp(argv[i], "-t") && i + 3 < argc) {
            stretch = 1;
            anchorSeq = atol(argv[++i]);
            targetSeq = atol(argv[++i]);
            if (!parse_time(argv[++i], &correct)) {
                fprintf(stderr, "Bad time format (use hh:mm:ss,fff)\n");
                return 1;
            }
        } else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            output = argv[++i];
        } else {
            fprintf(stderr, "Invalid argument: %s\n", argv[i]);
            usage();
            return 1;
        }
    }

    if (rename_to_video && output) {
	fprintf(stderr, "-r and -o cannot be combined (-r chooses the output name).\n");
	return 1;
    }

    /* ---- -r: work out the video-based name before touching anything ---- */
    int process = convert || stretch || shift != 0;
    if (rename_to_video) {
        dir_of(input, dir, sizeof dir);
        if (!find_largest_video(dir, vname, sizeof vname)) {
            fprintf(stderr, "No video file found in %s\n", dir);
            return 1;
        }
        snprintf(base, sizeof base, "%s", vname);
        char *dot = strrchr(base, '.');
        if (dot) *dot = '\0';                /* name without extension */
        

        if (!path_fmt(target, sizeof target, "%s/%s.srt", dir, base)) {
            fprintf(stderr, "Path too long: %s/%s.srt\n", dir, base);
            return 1;
        }

        printf("Largest video: %s\n", vname);

        if (!process) {                      /* pure rename: no rewrite needed */
            if (same_file(input, target)) {
                printf("Already named after the video: %s\n", target);
                return 0;
            }
            if (exists(target)) {
                if (!move_aside(target, dir, base, aside, sizeof aside)) {
                    fprintf(stderr, "Cannot rename existing %s out of the way\n", target);
                    return 1;
                }
                printf("Existing %s renamed to %s\n", target, aside);
            }
            if (rename(input, target) != 0) { perror("rename"); return 1; }
            printf("Renamed: %s -> %s\n", input, target);
            return 0;
        }
        output = target;                     /* processed result takes the video's name */
    }

    /* ---- read raw bytes ---- */
    size_t n;
    char *buf = read_file(input, &n);
    if (!buf) { fprintf(stderr, "Cannot read %s\n", input); return 1; }

    char *text = buf;
    if (n >= 3 && (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        text += 3; n -= 3;                   /* skip BOM; we write our own */
    }

    /* ---- split into lines (handles \r\n, \n, \r) ---- */
    char **lines = malloc((n + 2) * sizeof(char *));
    int nl = 0;
    for (size_t p = 0; p < n; ) {
        lines[nl++] = text + p;
        while (p < n && text[p] != '\n' && text[p] != '\r') p++;
        if (p < n) {
            if (text[p] == '\r' && p + 1 < n && text[p + 1] == '\n') {
                text[p] = '\0'; p += 2;
            } else {
                text[p] = '\0'; p++;
            }
        }
    }

    /* ---- classify lines and index cues ---- */
    char *type = calloc(nl + 1, 1);          /* all LINE_TEXT by default */
    Cue *cues = malloc((nl + 1) * sizeof(Cue));
    int nc = 0;
    for (int i = 1; i < nl; i++) {
        int h1, m1, s1, f1, h2, m2, s2, f2, used = 0;
        if (sscanf(lines[i], "%d:%d:%d%*[,.]%d --> %d:%d:%d%*[,.]%d%n",
                   &h1, &m1, &s1, &f1, &h2, &m2, &s2, &f2, &used) < 8 || !used)
            continue;
        type[i] = LINE_TIME;
        type[i - 1] = LINE_SEQ;
        Cue *c = &cues[nc++];
        c->line  = i;
        c->seq   = strtol(lines[i - 1], NULL, 10);
        c->start = h1 * 3600000.0 + m1 * 60000.0 + s1 * 1000.0 + f1;
        c->end   = h2 * 3600000.0 + m2 * 60000.0 + s2 * 1000.0 + f2;
        c->tail  = lines[i] + used;
    }
    if (nc == 0) { fprintf(stderr, "No subtitles found.\n"); return 1; }

    /* ---- stretch / shrink ---- */
    if (stretch) {
        int a = -1, t = -1;
        for (int i = 0; i < nc; i++) {
            if (cues[i].seq == anchorSeq) a = i;
            if (cues[i].seq == targetSeq) t = i;
        }
        if (a < 0 || t < 0 || cues[t].start == cues[a].start) {
            fprintf(stderr, "Anchor/target not found, or both have the same start time.\n");
            return 1;
        }
        double a0 = cues[a].start;
        double scale = (correct - a0) / (cues[t].start - a0);
        double before = cues[nc - 1].start;  /* last cue, before the sync */
        printf("Scale factor: %.8f\n", scale);

        for (int i = 0; i < nc; i++) {       /* ALL cues, before and after the anchor */
            double dur = cues[i].end - cues[i].start;
            cues[i].start = a0 + (cues[i].start - a0) * scale;
            cues[i].end   = cues[i].start + dur;   /* keep each cue's duration */
        }

        double diff = (cues[nc - 1].start - before) / 1000.0;   /* seconds */
        if (diff > -0.0005 && diff < 0.0005)
            printf("No change noticed\n");
        else
            printf("Sync moved the end of the file by %+.3f seconds (%s)\n",
                   diff, diff > 0 ? "later" : "earlier");
    }

    /* ---- constant shift ---- */
    for (int i = 0; i < nc; i++) {
        cues[i].start += shift;
        cues[i].end   += shift;
    }

    /* ---- output path ---- */
    char defname[PATHBUF];
    if (!output) {
        const char *dot = strrchr(input, '.');
        const char *sep = last_sep(input);
        int stem = (dot && (!sep || dot > sep)) ? (int)(dot - input) : (int)strlen(input);
        if (!path_fmt(defname, sizeof defname, "%.*s.fixed.srt", stem, input)) {
            fprintf(stderr, "Output path too long\n");
            return 1;
        }
        output = defname;
    }

    /* -r: an existing <video>.srt is renamed out of the way before we write */
    int moved = 0;
    if (rename_to_video && exists(output)) {
        if (!move_aside(output, dir, base, aside, sizeof aside)) {
            fprintf(stderr, "Cannot rename existing %s out of the way\n", output);
            return 1;
        }
        moved = 1;
        printf("Existing %s renamed to %s\n", output, aside);
    }

    FILE *out = fopen(output, "wb");
    if (!out) {
        fprintf(stderr, "Cannot write %s\n", output);
        if (moved) rename(aside, output);    /* put it back */
        return 1;
    }

    /* ---- write ---- */
    int same = 0, converted = 0, repaired = 0;
    if (convert) fputs("\xEF\xBB\xBF", out); /* BOM only when we produce UTF-8 */
    int ci = 0;
    for (int i = 0; i < nl; i++) {
        if (type[i] == LINE_TIME) {
            fmt_time(out, cues[ci].start);
            fputs(" --> ", out);
            fmt_time(out, cues[ci].end);
            fputs(cues[ci].tail, out);
            ci++;
        } else if (type[i] == LINE_SEQ) {
            fputs(lines[i], out);            /* copied as-is */
        } else {
            switch (put_text(out, lines[i], convert)) {
                case TXT_CONVERTED: converted++; break;
                case TXT_REPAIRED:  repaired++;  break;
                default:            same++;      break;
            }
        }
        fputs("\r\n", out);
    }
    fclose(out);

    if (convert)
        printf("Text lines: %d unchanged, %d converted from windows-1256, %d repaired (mojibake)\n",
               same, converted, repaired);
    printf("Written: %s\n", output);

    free(type); free(cues); free(lines); free(buf);
    return 0;
}
