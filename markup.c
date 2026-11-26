/* markup.c - removing HTML-style tags from subtitle text */
#include "markup.h"

#include <ctype.h>
#include <string.h>

static int name_matches(const char *tok, size_t toklen, const char *name, size_t namelen) {
    if (toklen != namelen) return 0;
    for (size_t i = 0; i < namelen; i++)
        if (tolower((unsigned char)tok[i]) != tolower((unsigned char)name[i])) return 0;
    return 1;
}

static int tag_wanted(const char *tags, const char *name, size_t namelen) {
    const char *p = tags;
    while (*p) {
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (name_matches(p, len, "all", 3) || name_matches(p, len, name, namelen)) return 1;
        if (!end) break;
        p = end + 1;
    }
    return 0;
}

/* If a tag starts at s (which points at '<'), returns the index just past its '>'.
 * Returns 0 if it is not a tag. *name / *namelen describe the tag name. */
static size_t scan_tag(const char *s, const char **name, size_t *namelen) {
    size_t i = 1;
    if (s[i] == '/') i++;
    if (!isalpha((unsigned char)s[i])) return 0;
    size_t start = i;
    while (isalnum((unsigned char)s[i])) i++;
    *name = s + start;
    *namelen = i - start;
    if (s[i] != '>' && s[i] != '/' && !isspace((unsigned char)s[i])) return 0;

    char quote = 0;                          /* '>' inside a quoted attribute does not end the tag */
    for (; s[i]; i++) {
        if (quote) { if (s[i] == quote) quote = 0; }
        else if (s[i] == '"' || s[i] == '\'') quote = s[i];
        else if (s[i] == '>') return i + 1;
    }
    return 0;                                /* no closing '>' on this line */
}

size_t markup_tag_length(const char *s) {
    const char *name;
    size_t namelen;
    return s[0] == '<' ? scan_tag(s, &name, &namelen) : 0;
}

int markup_strip_tags(const char *line, const char *tags, StrBuf *out) {
    int removed = 0;
    const char *p = line;
    while (*p) {
        if (*p == '<') {
            const char *name;
            size_t namelen;
            size_t n = scan_tag(p, &name, &namelen);
            if (n && tag_wanted(tags, name, namelen)) {
                p += n;
                removed++;
                continue;
            }
        }
        sb_putn(out, p, 1);
        p++;
    }
    return removed;
}
