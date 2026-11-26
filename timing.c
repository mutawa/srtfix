/* timing.c - shifting and stretching cue timestamps */
#include "timing.h"

#include <stdlib.h>
#include <string.h>

void timing_shift(SrtFile *s, long ms) {
    for (int i = 0; i < s->ncues; i++) {
        s->cues[i].start += (double)ms;
        s->cues[i].end   += (double)ms;
    }
}

TimingStatus timing_stretch(SrtFile *s, long anchorSeq, long targetSeq,
                            long correctMs, StretchInfo *info) {
    int a = -1, t = -1;
    for (int i = 0; i < s->ncues; i++) {
        if (s->cues[i].seq == anchorSeq) a = i;
        if (s->cues[i].seq == targetSeq) t = i;
    }
    if (a < 0 || t < 0) return TIMING_NOT_FOUND;
    if (s->cues[t].start == s->cues[a].start) return TIMING_SAME_START;

    double a0    = s->cues[a].start;
    double scale = ((double)correctMs - a0) / (s->cues[t].start - a0);
    if (info) info->scale = scale;
    if (scale <= 0) return TIMING_BAD_SCALE;

    double last_before = s->cues[s->ncues - 1].start;

    for (int i = 0; i < s->ncues; i++) {     /* ALL cues, before and after the anchor */
        double dur = s->cues[i].end - s->cues[i].start;
        s->cues[i].start = a0 + (s->cues[i].start - a0) * scale;
        s->cues[i].end   = s->cues[i].start + dur;
    }

    if (info)
        info->end_shift_sec = (s->cues[s->ncues - 1].start - last_before) / 1000.0;
    return TIMING_OK;
}

int timing_find_negative(const SrtFile *s, NegativeInfo *info) {
    int count = 0;
    if (info) info->listed = 0;
    for (int i = 0; i < s->ncues; i++) {
        const Cue *c = &s->cues[i];
        double lowest = c->start < c->end ? c->start : c->end;
        if (lowest < -0.5) {                 /* would round to a negative millisecond */
            if (info && info->listed < NEG_MAX_LISTED)
                info->index[info->listed++] = i;
            count++;
        }
    }
    if (info) info->count = count;
    return count;
}

/* milliseconds as they will be written to the file */
static long rounded_ms(double x) { return x < 0 ? 0 : (long)(x + 0.5); }

unsigned char *timing_overlap_flags(const SrtFile *s) {
    unsigned char *flags = calloc((size_t)s->ncues + 1, 1);
    if (!flags) return NULL;
    for (int i = 0; i + 1 < s->ncues; i++)
        flags[i] = rounded_ms(s->cues[i].end) > rounded_ms(s->cues[i + 1].start);
    return flags;
}

int timing_new_overlaps(SrtFile *s, const unsigned char *before, int fix, OverlapInfo *info) {
    int count = 0;
    if (info) { memset(info, 0, sizeof *info); info->shortest_index = -1; }

    for (int i = 0; i + 1 < s->ncues; i++) {
        long end = rounded_ms(s->cues[i].end), next = rounded_ms(s->cues[i + 1].start);
        if (end <= next || before[i]) continue;     /* no overlap, or it was already in the input */

        double amount = (double)(end - next);
        count++;
        if (info) {
            if (info->listed < OVERLAP_MAX_LISTED) {
                info->index[info->listed] = i;
                info->amount_ms[info->listed] = amount;
                info->listed++;
            }
            if (amount > info->worst_ms) info->worst_ms = amount;
        }
        if (fix) {
            s->cues[i].end = s->cues[i + 1].start;  /* the earlier cue ends when the next starts */
            double dur = s->cues[i].end - s->cues[i].start;
            if (info && (info->shortest_index < 0 || dur < info->shortest_ms)) {
                info->shortest_index = i;
                info->shortest_ms = dur;
            }
        }
    }
    if (info) info->count = count;
    return count;
}
