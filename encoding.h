/* encoding.h - fixing the text encoding of subtitle lines (-> UTF-8) */
#ifndef ENCODING_H
#define ENCODING_H

#include <stdio.h>
#include "strbuf.h"

typedef enum {
    ENC_UNCHANGED = 0,   /* ASCII, or already correct UTF-8: copied as-is        */
    ENC_CONVERTED,       /* genuine windows-1256 bytes converted to UTF-8        */
    ENC_REPAIRED         /* mojibake (1256 read as 1252, saved as UTF-8) repaired */
} EncKind;

typedef struct {
    int unchanged, converted, repaired;
} EncStats;

/* Appends one line of subtitle text to out as correct UTF-8 (no line ending).
 * Returns what had to be done to the line. */
EncKind enc_convert_line(const char *line, StrBuf *out);

/* Same, but writes straight to a stream (used for messages). */
EncKind enc_write_line(FILE *out, const char *line);

void enc_stats_add(EncStats *st, EncKind kind);

/* Strict decode of one 1-3 byte UTF-8 sequence.
 * Returns its length, or 0 if the bytes are not valid UTF-8. */
int enc_utf8_decode(const unsigned char *s, unsigned *cp);

#endif
