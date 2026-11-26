/* brackets.c - detecting unbalanced brackets in subtitle text */
#include "brackets.h"

#include <stdlib.h>
#include <string.h>

int brackets_check(const char *text, BracketInfo *info) {
    size_t n = strlen(text);
    char *stack = malloc(n + 1);
    size_t top = 0;
    int bad_close = 0;

    if (!stack) {                            /* out of memory: report nothing rather than guess */
        if (info) memset(info, 0, sizeof *info);
        return 0;
    }
    for (const char *p = text; *p; p++) {
        switch (*p) {
            case '(': case '[':
                stack[top++] = *p;
                break;
            case ')':
                if (top && stack[top - 1] == '(') top--; else bad_close++;
                break;
            case ']':
                if (top && stack[top - 1] == '[') top--; else bad_close++;
                break;
            default:
                break;
        }
    }
    if (info) {
        info->unmatched_open  = (int)top;
        info->unmatched_close = bad_close;
    }
    free(stack);
    return (int)top + bad_close;
}
