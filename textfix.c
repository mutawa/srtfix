/* textfix.c - the chain of fixes applied to one line of subtitle text */
#include "textfix.h"
#include "markup.h"

void textwork_init(TextWork *w) { sb_init(&w->a); sb_init(&w->b); }
void textwork_free(TextWork *w) { sb_free(&w->a); sb_free(&w->b); }

const char *textfix_line(const TextOptions *opt, const char *line, TextWork *w, TextStats *stats) {
    const char *cur = line;
    StrBuf *cur_buf = NULL;                  /* which scratch buffer holds cur (NULL = the input) */
    StrBuf *dst;

#define NEXT_BUFFER() (cur_buf == &w->a ? &w->b : &w->a)

    if (opt->strip_tags) {
        dst = NEXT_BUFFER();
        sb_clear(dst);
        int n = markup_strip_tags(cur, opt->strip_tags, dst);
        if (stats) stats->tags_removed += n;
        cur = sb_str(dst); cur_buf = dst;
    }
    if (opt->fix_encoding) {
        dst = NEXT_BUFFER();
        sb_clear(dst);
        EncKind k = enc_convert_line(cur, dst);
        if (stats && line[0]) enc_stats_add(&stats->enc, k);
        cur = sb_str(dst); cur_buf = dst;
    }
    if (opt->bidi != BIDI_NONE) {
        dst = NEXT_BUFFER();
        sb_clear(dst);
        int n = bidi_fix_line(cur, opt->bidi, dst);
        if (stats && n) { stats->bidi_marks += n; stats->bidi_lines++; }
        cur = sb_str(dst); cur_buf = dst;
    }
#undef NEXT_BUFFER

    if (w->a.failed || w->b.failed) return NULL;
    return cur;
}
