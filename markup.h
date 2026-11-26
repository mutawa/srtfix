/* markup.h - removing HTML-style tags from subtitle text */
#ifndef MARKUP_H
#define MARKUP_H

#include <stddef.h>
#include "strbuf.h"

/* Appends line to out with the selected tags removed and their inner text kept.
 * tags is "all" or a comma-separated list of tag names (case-insensitive),
 * e.g. "font" or "font,i,b". Both opening and closing tags are removed
 * (<font color="red">, </font>). Text such as "a < b" is not a tag and is kept.
 * Returns the number of tags removed. */
int markup_strip_tags(const char *line, const char *tags, StrBuf *out);

/* If a tag (<name ...> or </name>) starts at s, returns its length in bytes,
 * otherwise 0. Used to treat tags as invisible by the text fixes. */
size_t markup_tag_length(const char *s);

#endif
