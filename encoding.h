/* encoding.h - fixing the text encoding of subtitle lines (-> UTF-8) */
#ifndef ENCODING_H
#define ENCODING_H

#include <stdio.h>

typedef enum {
    ENC_UNCHANGED = 0,   /* ASCII, or already correct UTF-8: copied as-is        */
    ENC_CONVERTED,       /* genuine windows-1256 bytes converted to UTF-8        */
    ENC_REPAIRED         /* mojibake (1256 read as 1252, saved as UTF-8) repaired */
} EncKind;

typedef struct {
    int unchanged, converted, repaired;
} EncStats;

/* Writes one line of subtitle text to out as correct UTF-8 (no line ending).
 * Returns what had to be done to the line. */
EncKind enc_write_line(FILE *out, const char *line);

void enc_stats_add(EncStats *st, EncKind kind);

#endif
