/* strbuf.c - small growable byte buffer */
#include "strbuf.h"

#include <stdlib.h>
#include <string.h>

void sb_init(StrBuf *b) { memset(b, 0, sizeof *b); }

void sb_free(StrBuf *b) {
    free(b->data);
    memset(b, 0, sizeof *b);
}

void sb_clear(StrBuf *b) {
    b->len = 0;
    if (b->data) b->data[0] = '\0';
}

static int reserve(StrBuf *b, size_t extra) {
    if (b->failed) return 0;
    size_t need = b->len + extra + 1;
    if (need <= b->cap) return 1;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < need) cap *= 2;
    char *p = realloc(b->data, cap);
    if (!p) { b->failed = 1; return 0; }
    b->data = p;
    b->cap = cap;
    return 1;
}

void sb_putn(StrBuf *b, const char *s, size_t n) {
    if (!reserve(b, n)) return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void sb_puts(StrBuf *b, const char *s) { sb_putn(b, s, strlen(s)); }

void sb_put_utf8(StrBuf *b, unsigned cp) {
    char t[3];
    if (cp < 0x80) {
        t[0] = (char)cp;
        sb_putn(b, t, 1);
    } else if (cp < 0x800) {
        t[0] = (char)(0xC0 | (cp >> 6));
        t[1] = (char)(0x80 | (cp & 0x3F));
        sb_putn(b, t, 2);
    } else {
        t[0] = (char)(0xE0 | (cp >> 12));
        t[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        t[2] = (char)(0x80 | (cp & 0x3F));
        sb_putn(b, t, 3);
    }
}

const char *sb_str(const StrBuf *b) { return b->data ? b->data : ""; }
