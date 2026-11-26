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

#define OVERLAP_MAX_LISTED 5

typedef struct {
    int    count;                           /* new overlaps found */
    int    listed;                          /* how many are recorded below */
    int    index[OVERLAP_MAX_LISTED];       /* earlier cue of each pair (index into SrtFile.cues) */
    double amount_ms[OVERLAP_MAX_LISTED];   /* how far it runs into the next cue, before any fix */
    double worst_ms;                        /* the largest overlap found */
    int    shortest_index;                  /* after a fix: the trimmed cue that ended up shortest */
    double shortest_ms;                     /*   and its resulting duration (shortest_index < 0: none) */
} OverlapInfo;

/* flags[i] is 1 if cue i is still on screen when cue i+1 starts (compared at
 * millisecond precision, as written to the file). Returns a malloc'd array of
 * ncues entries, or NULL if memory ran out. The caller frees it. */
unsigned char *timing_overlap_flags(const SrtFile *s);

/* Finds overlaps between consecutive cues that are NOT flagged in `before`
 * (the flags taken before the stretch), i.e. the ones the stretch caused.
 * Overlaps that were already in the input are intentional and left alone.
 * If fix is set, each such cue is ended when the next one starts. Returns the
 * number found; info may be NULL. */
int timing_new_overlaps(SrtFile *s, const unsigned char *before, int fix, OverlapInfo *info);

/* Counts cues whose start or end time is negative (would print below
 * 00:00:00,000). Returns the count; info may be NULL. */
int timing_find_negative(const SrtFile *s, NegativeInfo *info);

#endif
