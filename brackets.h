/* brackets.h - detecting unbalanced brackets in subtitle text */
#ifndef BRACKETS_H
#define BRACKETS_H

typedef struct {
    int unmatched_open;     /* '(' or '[' that is never closed */
    int unmatched_close;    /* ')' or ']' with nothing (of the same kind) to close */
} BracketInfo;

/* Checks the ( ) and [ ] pairs of one piece of text, scanning in logical order.
 * Works on raw bytes, so it can run before any encoding conversion (the bracket
 * characters are plain ASCII in windows-1256 and in UTF-8). Returns the number
 * of unmatched brackets (0 = balanced); info may be NULL. */
int brackets_check(const char *text, BracketInfo *info);

#endif
