/* strbuf.h - small growable byte buffer used by the text-processing stages */
#ifndef STRBUF_H
#define STRBUF_H

#include <stddef.h>

typedef struct {
    char  *data;
    size_t len, cap;
    int    failed;          /* set once an allocation failed; later calls do nothing */
} StrBuf;

void sb_init(StrBuf *b);
void sb_free(StrBuf *b);
void sb_clear(StrBuf *b);
void sb_putn(StrBuf *b, const char *s, size_t n);
void sb_puts(StrBuf *b, const char *s);
void sb_put_utf8(StrBuf *b, unsigned cp);   /* encodes cp as 1-3 UTF-8 bytes */

/* NUL-terminated contents ("" if the buffer is empty) */
const char *sb_str(const StrBuf *b);

#endif
