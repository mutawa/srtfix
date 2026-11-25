/* srt.c - loading, indexing and writing .srt files */
#include "srt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *srt_strerror(SrtStatus st) {
    switch (st) {
        case SRT_OK:          return "ok";
        case SRT_ERR_READ:    return "cannot read file";
        case SRT_ERR_MEMORY:  return "out of memory";
        case SRT_ERR_NO_CUES: return "no subtitles found";
        case SRT_ERR_WRITE:   return "cannot write file";
    }
    return "unknown error";
}

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    *len = fread(buf, 1, (size_t)n, f);
    buf[*len] = '\0';
    fclose(f);
    return buf;
}

int srt_parse_time(const char *text, long *ms) {
    int h, m, sec, f;
    if (sscanf(text, "%d:%d:%d%*[,.]%d", &h, &m, &sec, &f) != 4) return 0;
    *ms = h * 3600000L + m * 60000L + sec * 1000L + f;
    return 1;
}

static void put_time(FILE *out, double x) {
    if (x < 0) x = 0;                        /* never emit negative times */
    long ms = (long)(x + 0.5);
    fprintf(out, "%02ld:%02ld:%02ld,%03ld",
            ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
}

const char *srt_cue_text(const SrtFile *s, const Cue *c) {
    int i = c->time_line + 1;
    return i < s->nlines ? s->lines[i] : "";
}

int srt_check_numbering(const SrtFile *s, NumberingInfo *info) {
    int count = 0;
    long prev = 0;
    for (int i = 0; i < s->ncues; i++) {
        if (s->cues[i].seq != prev + 1) {
            if (count == 0 && info) {
                info->first_pos  = i;
                info->first_seq  = s->cues[i].seq;
                info->first_prev = prev;
            }
            count++;
        }
        prev = s->cues[i].seq;
    }
    if (info) info->count = count;
    return count;
}

int srt_renumber(SrtFile *s) {
    int changed = 0;
    for (int i = 0; i < s->ncues; i++) {
        if (s->cues[i].seq != i + 1) changed++;
        s->cues[i].seq = i + 1;
    }
    s->renumbered = 1;
    return changed;
}

void srt_free(SrtFile *s) {
    free(s->cues);
    free(s->lines);
    free(s->buffer);
    memset(s, 0, sizeof *s);
}

SrtStatus srt_load(SrtFile *s, const char *path) {
    memset(s, 0, sizeof *s);

    size_t n;
    s->buffer = read_file(path, &n);
    if (!s->buffer) return SRT_ERR_READ;

    char *text = s->buffer;
    if (n >= 3 && (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        text += 3; n -= 3;
        s->had_bom = 1;
    }

    /* split into lines (handles \r\n, \n, \r) */
    s->lines = malloc((n + 2) * sizeof *s->lines);
    if (!s->lines) { srt_free(s); return SRT_ERR_MEMORY; }
    for (size_t p = 0; p < n; ) {
        s->lines[s->nlines++] = text + p;
        while (p < n && text[p] != '\n' && text[p] != '\r') p++;
        if (p < n) {
            if (text[p] == '\r' && p + 1 < n && text[p + 1] == '\n') {
                text[p] = '\0'; p += 2;
            } else {
                text[p] = '\0'; p++;
            }
        }
    }

    /* index the cues: a timestamp line, with its sequence number just before it */
    s->cues = malloc(((size_t)s->nlines + 1) * sizeof *s->cues);
    if (!s->cues) { srt_free(s); return SRT_ERR_MEMORY; }
    for (int i = 1; i < s->nlines; i++) {
        int h1, m1, s1, f1, h2, m2, s2, f2, used = 0;
        if (sscanf(s->lines[i], "%d:%d:%d%*[,.]%d --> %d:%d:%d%*[,.]%d%n",
                   &h1, &m1, &s1, &f1, &h2, &m2, &s2, &f2, &used) < 8 || !used)
            continue;
        Cue *c = &s->cues[s->ncues++];
        c->seq       = strtol(s->lines[i - 1], NULL, 10);
        c->start     = h1 * 3600000.0 + m1 * 60000.0 + s1 * 1000.0 + f1;
        c->end       = h2 * 3600000.0 + m2 * 60000.0 + s2 * 1000.0 + f2;
        c->extra     = s->lines[i] + used;
        c->time_line = i;
    }
    if (s->ncues == 0) { srt_free(s); return SRT_ERR_NO_CUES; }
    return SRT_OK;
}

SrtStatus srt_write(const SrtFile *s, const char *path, int fix_encoding, EncStats *stats) {
    FILE *out = fopen(path, "wb");
    if (!out) return SRT_ERR_WRITE;

    if (fix_encoding || s->had_bom)          /* we produce (or kept) UTF-8 */
        fputs("\xEF\xBB\xBF", out);

    int ci = 0;                              /* next cue whose lines we haven't reached */
    for (int i = 0; i < s->nlines; i++) {
        const Cue *c = ci < s->ncues ? &s->cues[ci] : NULL;

        if (c && i == c->time_line) {        /* timestamp: regenerated from the numbers */
            put_time(out, c->start);
            fputs(" --> ", out);
            put_time(out, c->end);
            fputs(c->extra, out);
            ci++;
        } else if (c && i == c->time_line - 1) {
            if (s->renumbered) fprintf(out, "%ld", c->seq);   /* new number */
            else               fputs(s->lines[i], out);       /* copied as-is */
        } else if (fix_encoding) {           /* subtitle text (and blank lines) */
            EncKind k = enc_write_line(out, s->lines[i]);
            if (stats && s->lines[i][0]) enc_stats_add(stats, k);
        } else {
            fputs(s->lines[i], out);
        }
        fputs("\r\n", out);
    }

    int bad = ferror(out);
    if (fclose(out) != 0) bad = 1;
    return bad ? SRT_ERR_WRITE : SRT_OK;
}
