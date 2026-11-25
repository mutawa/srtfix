/* timing.c - shifting and stretching cue timestamps */
#include "timing.h"

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
