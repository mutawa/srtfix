/* srt.h - loading, indexing and writing .srt files */
#ifndef SRT_H
#define SRT_H

#include "brackets.h"
#include "textfix.h"

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
 * the cues, sequence lines are copied as-is (or renumbered), and every other
 * line goes through the text fixes selected in opt. stats may be NULL. */
SrtStatus srt_write(const SrtFile *s, const char *path, const TextOptions *opt, TextStats *stats);

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

/* The text lines of cue number `ci` (an index into cues): lines first .. first+count-1
 * of SrtFile.lines, from just after the timestamp up to the first blank line.
 * Returns count (0 if the cue has no text). */
int srt_cue_text_range(const SrtFile *s, int ci, int *first, int *count);

#define BRACKET_MAX_LISTED 5

typedef struct {
    int         count;                          /* cues with unbalanced brackets */
    int         listed;                         /* how many are recorded below */
    int         index[BRACKET_MAX_LISTED];      /* the first few, as indexes into cues */
    BracketInfo info[BRACKET_MAX_LISTED];       /* what is unmatched in each */
} BracketReport;

/* Checks every cue's text (all its lines together, so a bracket may open on one
 * line and close on the next). Returns the number of cues with unbalanced
 * brackets; rep may be NULL. */
int srt_check_brackets(const SrtFile *s, BracketReport *rep);

/* Parses "hh:mm:ss,fff" (or with '.') into milliseconds. Returns 1 on success. */
int srt_parse_time(const char *text, long *ms);

#endif
