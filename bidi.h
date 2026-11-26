/* bidi.h - keeping punctuation in place in right-to-left (Arabic) subtitle lines */
#ifndef BIDI_H
#define BIDI_H

#include "strbuf.h"

typedef enum {
    BIDI_NONE = 0,
    BIDI_TATWEEL,        /* insert the Arabic extension letter U+0640 (visible stroke) */
    BIDI_RLM             /* insert the invisible right-to-left mark U+200F             */
} BidiMode;

/* Appends the UTF-8 line to out. Where a run of punctuation/brackets follows
 * Arabic text and is NOT followed by more Arabic text (it ends the line, or is
 * followed by Latin text), the chosen mark is inserted after the run's last
 * punctuation character. Lines that already contain explicit direction
 * controls, and runs that are already followed by Arabic text or a mark, are
 * left alone, so running it twice changes nothing. Returns the number of marks
 * inserted. The line must be valid UTF-8; otherwise it is copied unchanged. */
int bidi_fix_line(const char *line, BidiMode mode, StrBuf *out);

#endif
