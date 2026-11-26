/* textfix.h - the chain of fixes applied to one line of subtitle text */
#ifndef TEXTFIX_H
#define TEXTFIX_H

#include "bidi.h"
#include "encoding.h"
#include "strbuf.h"

typedef struct {
    int         fix_encoding;   /* convert / repair to UTF-8 */
    const char *strip_tags;     /* NULL = keep tags; "all" or a comma-separated list */
    BidiMode    bidi;           /* Arabic line-end fix (needs UTF-8, so implies fix_encoding) */
} TextOptions;

typedef struct {
    EncStats enc;
    int      tags_removed;
    int      bidi_marks;        /* marks inserted */
    int      bidi_lines;        /* lines that got at least one */
} TextStats;

/* Scratch space reused between lines. */
typedef struct { StrBuf a, b; } TextWork;

void textwork_init(TextWork *w);
void textwork_free(TextWork *w);

/* Applies, in order: tag removal, encoding fix, Arabic line-end fix.
 * Returns the resulting line (valid until the next call), or NULL if memory
 * ran out. stats may be NULL. */
const char *textfix_line(const TextOptions *opt, const char *line, TextWork *w, TextStats *stats);

#endif
