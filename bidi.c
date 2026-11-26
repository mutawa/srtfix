/* bidi.c - keeping punctuation in place in right-to-left (Arabic) subtitle lines
 *
 * Why this is needed: punctuation and brackets are "neutral" for the Unicode
 * bidirectional algorithm. A neutral between two right-to-left characters
 * becomes right-to-left too, but a neutral at the END of the line, or before
 * left-to-right text, takes the paragraph's base direction. Players that use a
 * left-to-right base then draw it on the wrong side. A strong right-to-left
 * character placed right after the run (the tatweel, or the invisible RLM)
 * makes the run "sit between two RTL characters", so it stays put. */
#include "bidi.h"
#include "encoding.h"
#include "markup.h"

#include <stdlib.h>
#include <string.h>

enum { C_R, C_L, C_DIGIT, C_NEUTRAL, C_SPACE, C_SKIP, C_CTRL };

static int classify(unsigned c) {
    if (c < 0x80) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return C_L;
        if (c >= '0' && c <= '9') return C_DIGIT;
        if (c == ' ' || c == '\t') return C_SPACE;
        return C_NEUTRAL;                                   /* punctuation and symbols */
    }
    if (c < 0xA0) return C_SKIP;                            /* C1 controls */
    if (c == 0xA0) return C_SPACE;
    if (c < 0x100) {
        if (c == 0xAA || c == 0xB5 || c == 0xBA || (c >= 0xC0 && c != 0xD7 && c != 0xF7)) return C_L;
        return C_NEUTRAL;                                   /* « » ¡ ¿ and other Latin-1 symbols */
    }
    if (c < 0x250) return C_L;                              /* Latin extended */
    if (c >= 0x0590 && c <= 0x05FF) return C_R;             /* Hebrew */
    if (c == 0x060C) return C_NEUTRAL;                      /* Arabic comma */
    if (c == 0x061B || c == 0x061C || c == 0x061F) return C_R;   /* ؛ ALM ؟ are strong */
    if ((c >= 0x0660 && c <= 0x0669) || (c >= 0x06F0 && c <= 0x06F9)) return C_DIGIT;
    if (c >= 0x066A && c <= 0x066C) return C_NEUTRAL;
    if ((c >= 0x064B && c <= 0x065F) || c == 0x0670 ||
        (c >= 0x06D6 && c <= 0x06DC) || (c >= 0x06DF && c <= 0x06E4) ||
        c == 0x06E7 || c == 0x06E8 || (c >= 0x06EA && c <= 0x06ED)) return C_SKIP;  /* harakat */
    if (c >= 0x0600 && c <= 0x08FF) return C_R;             /* Arabic, Syriac, Thaana, ... */
    if (c == 0x200E) return C_L;                            /* LRM */
    if (c == 0x200F) return C_R;                            /* RLM */
    if ((c >= 0x200B && c <= 0x200D) || c == 0x2060 || c == 0xFEFF) return C_SKIP;
    if (c >= 0x2000 && c <= 0x200A) return C_SPACE;
    if ((c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069)) return C_CTRL;
    if ((c >= 0x2010 && c <= 0x2027) || (c >= 0x2030 && c <= 0x205E)) return C_NEUTRAL;
    if (c >= 0x20A0 && c <= 0x2BFF) return C_NEUTRAL;       /* currency, arrows, symbols */
    if ((c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFC)) return C_R;
    if (c == 0x3000) return C_SPACE;
    return C_L;
}

int bidi_fix_line(const char *line, BidiMode mode, StrBuf *out) {
    size_t n = strlen(line);
    unsigned *cp  = malloc((n + 1) * sizeof *cp);
    size_t   *off = malloc((n + 2) * sizeof *off);
    unsigned char *flag = calloc(n + 2, 1);
    unsigned char *hidden = calloc(n + 2, 1);   /* part of a tag or style block: not displayed */
    int inserted = 0;
    if (!cp || !off || !flag || !hidden) { sb_puts(out, line); goto done; }   /* memory: leave the line alone */

    /* decode into code points; anything that is not valid UTF-8 is left unchanged */
    size_t m = 0;
    for (size_t i = 0; i < n; ) {
        int len = enc_utf8_decode((const unsigned char *)line + i, &cp[m]);
        if (!len) { sb_puts(out, line); goto done; }
        off[m++] = i;
        i += (size_t)len;
    }
    off[m] = n;

    for (size_t k = 0; k < m; k++)                           /* explicit controls: author handled it */
        if (classify(cp[k]) == C_CTRL) { sb_puts(out, line); goto done; }

    /* tags like <i>, </font> and style blocks like {\an8} are not displayed: skip them entirely */
    for (size_t k = 0; k < m; k++) {
        size_t last = 0;
        if (cp[k] == '<') {
            size_t len = markup_tag_length(line + off[k]);
            if (!len) continue;
            while (last + 1 < m - k && off[k + last + 1] < off[k] + len) last++;
        } else if (cp[k] == '{' && k + 1 < m && cp[k + 1] == '\\') {
            size_t j = k + 2;
            while (j < m && cp[j] != '}') j++;
            if (j >= m) continue;
            last = j - k;
        } else {
            continue;
        }
        for (size_t j = k; j <= k + last; j++) hidden[j] = 1;
        k += last;
    }

    /* pass 1: find the runs that need a mark */
    const size_t NONE = (size_t)-1;
    char ctx = 'N', ctx_before = 'N';                        /* direction of the text so far: N/R/L */
    int seen_arabic = 0, in_run = 0;
    size_t last_punct = NONE;
    for (size_t k = 0; k <= m; k++) {
        int c = (k < m) ? (hidden[k] ? C_SKIP : classify(cp[k])) : -1;   /* -1 = end of line */
        if (c == C_NEUTRAL || c == C_SPACE) {
            if (!in_run) { in_run = 1; ctx_before = ctx; last_punct = NONE; }
            if (c == C_NEUTRAL) last_punct = k;
            continue;
        }
        if (c == C_SKIP) continue;                           /* marks, zero-width: transparent */
        if (in_run) {                                        /* the run ends here */
            if (ctx_before == 'R' && last_punct != NONE && (c == -1 || c == C_L))
                flag[last_punct] = 1;
            in_run = 0;
        }
        if (c == C_R) { ctx = 'R'; seen_arabic = 1; }
        else if (c == C_L) ctx = 'L';
        else if (c == C_DIGIT && ctx != 'L' && seen_arabic) ctx = 'R';   /* numbers act as RTL here */
    }

    /* pass 2: copy, inserting the mark after each flagged character */
    for (size_t k = 0; k < m; k++) {
        sb_putn(out, line + off[k], off[k + 1] - off[k]);
        if (flag[k]) {
            sb_put_utf8(out, mode == BIDI_RLM ? 0x200F : 0x0640);
            inserted++;
        }
    }

done:
    free(cp); free(off); free(flag); free(hidden);
    return inserted;
}
