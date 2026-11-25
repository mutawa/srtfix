/* timing.h - shifting and stretching cue timestamps */
#ifndef TIMING_H
#define TIMING_H

#include "srt.h"

typedef enum {
    TIMING_OK = 0,
    TIMING_NOT_FOUND,       /* anchor or target sequence number not in the file */
    TIMING_SAME_START,      /* anchor and target start at the same time */
    TIMING_BAD_SCALE        /* scale <= 0: the cues would be reversed or collapsed */
} TimingStatus;

typedef struct {
    double scale;           /* factor applied around the anchor's start time */
    double end_shift_sec;   /* how far the last cue's start moved, in seconds */
} StretchInfo;

#define NEG_MAX_LISTED 5

typedef struct {
    int count;                      /* number of cues with a negative start or end time */
    int listed;                     /* how many of them are recorded in index[] */
    int index[NEG_MAX_LISTED];      /* the first few, as indexes into SrtFile.cues */
} NegativeInfo;

/* Adds ms (may be negative) to every start and end time. */
void timing_shift(SrtFile *s, long ms);

/* Keeps the start of cue anchorSeq fixed and scales every cue's start time
 * (before and after the anchor, up to the last cue) linearly, so that cue
 * targetSeq starts at correctMs. Each cue keeps its duration. The cues are
 * left untouched unless TIMING_OK is returned. */
TimingStatus timing_stretch(SrtFile *s, long anchorSeq, long targetSeq,
                            long correctMs, StretchInfo *info);

/* Counts cues whose start or end time is negative (would print below
 * 00:00:00,000). Returns the count; info may be NULL. */
int timing_find_negative(const SrtFile *s, NegativeInfo *info);

#endif
