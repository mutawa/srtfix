/* srt.h - loading, indexing and writing .srt files */
#ifndef SRT_H
#define SRT_H

#include "encoding.h"

typedef struct {
    long        seq;         /* sequence number on the line before the timestamp */
    double      start, end;  /* milliseconds */
    const char *extra;       /* rest of the timestamp line after the end time
                                (e.g. position tags); points into the file buffer */
    int         time_line;   /* index of the timestamp line in SrtFile.lines */
} Cue;

typedef struct {
    char  *buffer;           /* owns the raw bytes of the file */
    char **lines;            /* every line of the file, NUL-terminated, in order */
    int    nlines;
    Cue   *cues;             /* one entry per timestamp line */
    int    ncues;
    int    had_bom;          /* input started with a UTF-8 BOM */
    int    renumbered;       /* srt_renumber() was applied: write the new numbers */
} SrtFile;

typedef enum {
    SRT_OK = 0,
    SRT_ERR_READ,
    SRT_ERR_MEMORY,
    SRT_ERR_NO_CUES,
    SRT_ERR_WRITE
} SrtStatus;

const char *srt_strerror(SrtStatus st);

/* Reads the file as raw bytes (no text conversion) and indexes its cues. */
SrtStatus srt_load(SrtFile *s, const char *path);
void      srt_free(SrtFile *s);

/* Writes the file with CRLF line endings. Timestamp lines are regenerated from
 * the cues, sequence lines are copied as-is, and every other line goes through
 * the encoding fixer when fix_encoding is set. stats may be NULL. */
SrtStatus srt_write(const SrtFile *s, const char *path, int fix_encoding, EncStats *stats);

/* First line of subtitle text of a cue ("" if it has none). */
const char *srt_cue_text(const SrtFile *s, const Cue *c);

typedef struct {
    int  count;           /* number of places where the numbering breaks */
    int  first_pos;       /* file position (0-based) of the first break */
    long first_seq;       /* number found there */
    long first_prev;      /* number of the cue before it (0 if it is the first cue) */
} NumberingInfo;

/* Checks that cue numbers run 1, 2, 3, ... in file order. A gap (e.g. after a
 * cue was deleted), a duplicate or a file that does not start at 1 each count
 * as one break. Returns the number of breaks; info may be NULL. */
int srt_check_numbering(const SrtFile *s, NumberingInfo *info);

/* Renumbers the cues 1, 2, 3, ... in file order, whatever the numbers in the
 * file were; srt_write then writes the new numbers. Returns how many cues got a
 * different number than they had. */
int srt_renumber(SrtFile *s);

/* Parses "hh:mm:ss,fff" (or with '.') into milliseconds. Returns 1 on success. */
int srt_parse_time(const char *text, long *ms);

#endif
