/* main.c - srtfix: argument parsing and orchestration only */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "encoding.h"
#include "fsutil.h"
#include "srt.h"
#include "textfix.h"
#include "timing.h"

typedef struct {
    const char *input;
    const char *output;          /* -o, or NULL */
    int  fix_encoding;           /* -e (also implied by -b) */
    int  rename_to_video;        /* -r */
    int  fix_numbering;          /* -n */
    int  fix_overlaps;           /* -f */
    int  stretch;                /* -t was given */
    long anchor_seq, target_seq, correct_ms;
    long shift_ms;               /* -s */
    const char *strip_tags;      /* -x, or NULL */
    BidiMode bidi;               /* -b */
} Options;

static void usage(void) {
    fprintf(stderr,
        "Usage: srtfix <file.srt> [options]\n"
        "  -e                 fix subtitle text encoding -> UTF-8. Handles genuine windows-1256 bytes\n"
        "                     and 1256 text that was misread as 1252 and saved as UTF-8 (mojibake)\n"
        "  -s <ms>            shift all timestamps by <ms> (negative allowed)\n"
        "  -t <a> <b> <time>  keep the start of cue <a> fixed; cue <b> should start at <time>\n"
        "                     (hh:mm:ss,fff); stretch/shrink all cues linearly around the anchor.\n"
        "                     Aborts if that would give any cue a negative time, and warns if it\n"
        "                     makes cues overlap the next one\n"
        "  -f                 with -t: end the cues that the stretch made overlap the next cue when\n"
        "                     that cue starts (overlaps already in the input are left alone)\n"
        "  -x <tags>          remove HTML tags but keep the text inside: 'all', or a comma-separated\n"
        "                     list such as font or font,i,b\n"
        "  -b <tatweel|rlm>   Arabic: after punctuation/brackets that end the line (or precede Latin\n"
        "                     text) insert the tatweel letter or the invisible RLM mark, so players\n"
        "                     do not move them. Implies -e\n"
        "  -n                 renumber the cues 1, 2, 3, ... in the output (-t still uses the numbers\n"
        "                     as written in the input file)\n"
        "  -r                 name the result after the largest video file in the same folder\n"
        "                     (an existing <video>.srt is renamed to <video>.old.srt first)\n"
        "  -o <file>          output file (default: <name>.fixed.srt; cannot be combined with -r)\n");
}

static int parse_args(int argc, char **argv, Options *o) {
    memset(o, 0, sizeof *o);
    if (argc < 2) return 0;
    o->input = argv[1];

    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-e")) {
            o->fix_encoding = 1;
        } else if (!strcmp(argv[i], "-r")) {
            o->rename_to_video = 1;
        } else if (!strcmp(argv[i], "-n")) {
            o->fix_numbering = 1;
        } else if (!strcmp(argv[i], "-f")) {
            o->fix_overlaps = 1;
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            o->shift_ms = atol(argv[++i]);
        } else if (!strcmp(argv[i], "-x") && i + 1 < argc) {
            o->strip_tags = argv[++i];
            if (!*o->strip_tags) { fprintf(stderr, "-x needs 'all' or a list of tag names\n"); return 0; }
        } else if (!strcmp(argv[i], "-b") && i + 1 < argc) {
            ++i;
            if (!strcmp(argv[i], "tatweel"))  o->bidi = BIDI_TATWEEL;
            else if (!strcmp(argv[i], "rlm")) o->bidi = BIDI_RLM;
            else { fprintf(stderr, "Bad -b mode '%s' (use tatweel or rlm)\n", argv[i]); return 0; }
        } else if (!strcmp(argv[i], "-t") && i + 3 < argc) {
            o->stretch = 1;
            o->anchor_seq = atol(argv[++i]);
            o->target_seq = atol(argv[++i]);
            if (!srt_parse_time(argv[++i], &o->correct_ms)) {
                fprintf(stderr, "Bad time format (use hh:mm:ss,fff)\n");
                return 0;
            }
        } else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            o->output = argv[++i];
        } else {
            fprintf(stderr, "Invalid argument: %s\n", argv[i]);
            return 0;
        }
    }
    if (o->bidi != BIDI_NONE) o->fix_encoding = 1;   /* the line-end fix works on UTF-8 text */
    return 1;
}

/* -r with nothing else to do: just rename the input file. */
static int rename_only(const char *input, const char *target, const char *dir, const char *base) {
    char aside[PATHBUF];

    if (path_same_file(input, target)) {
        printf("Already named after the video: %s\n", target);
        return 0;
    }
    if (path_exists(target)) {
        if (!move_aside(target, dir, base, aside, sizeof aside)) {
            fprintf(stderr, "Cannot rename existing %s out of the way\n", target);
            return 1;
        }
        printf("Existing %s renamed to %s\n", target, aside);
    }
    if (rename(input, target) != 0) { perror("rename"); return 1; }
    printf("Renamed: %s -> %s\n", input, target);
    return 0;
}

/* Lists the cues a stretch would push below 00:00:00,000. */
static void report_negative(const SrtFile *srt, const NegativeInfo *neg) {
    fprintf(stderr, "Aborted: the stretch would give %d cue(s) a negative timestamp. "
                    "Nothing was written.\n", neg->count);
    for (int k = 0; k < neg->listed; k++) {
        const Cue *c = &srt->cues[neg->index[k]];
        double lowest = c->start < c->end ? c->start : c->end;
        const char *txt = srt_cue_text(srt, c);
        fprintf(stderr, "  cue %-5ld would start at %.3f s  ", c->seq, lowest / 1000.0);
        if (*txt) {
            fputc('"', stderr);
            enc_write_line(stderr, txt);         /* shown as readable UTF-8 */
            fputs("\"\n", stderr);
        } else {
            fputs("(no text)\n", stderr);
        }
    }
    if (neg->count > neg->listed)
        fprintf(stderr, "  ... and %d more\n", neg->count - neg->listed);
    fprintf(stderr,
        "Check whether these are real subtitles (a welcome message or author credit can be\n"
        "removed). You can also use a later correct time, or add a positive -s shift.\n"
        "If you remove cues and retry, the remaining cues keep their numbers, so give -t\n"
        "the numbers as they appear in the file. Add -n to renumber the output from 1.\n");
}

/* Reports (and, with -f, has already fixed) overlaps the stretch caused. */
static void report_overlaps(const SrtFile *srt, const OverlapInfo *ov, int fixed) {
    if (fixed) {
        printf("Fixed %d overlap(s) caused by the stretch: the earlier cue now ends when the next one starts\n",
               ov->count);
        if (ov->shortest_index >= 0)
            printf("  (shortest resulting cue: cue %ld, %.0f ms)\n",
                   srt->cues[ov->shortest_index].seq, ov->shortest_ms);
        return;
    }
    fprintf(stderr, "Warning: the stretch made %d cue(s) overlap the next cue; worst overlap %.3f s.\n"
                    "         (Overlaps that were already in the input are not counted.)\n",
            ov->count, ov->worst_ms / 1000.0);
    for (int k = 0; k < ov->listed; k++) {
        const Cue *a = &srt->cues[ov->index[k]];
        const Cue *b = &srt->cues[ov->index[k] + 1];
        fprintf(stderr, "  cue %ld is still on screen %.3f s after cue %ld starts\n",
                a->seq, ov->amount_ms[k] / 1000.0, b->seq);
    }
    if (ov->count > ov->listed)
        fprintf(stderr, "  ... and %d more\n", ov->count - ov->listed);
    fprintf(stderr, "  Hint: add -f to end those cues when the next one starts.\n");
}

/* Lists cues whose brackets do not pair up (e.g. two '(' and no ')'). */
static void report_brackets(const SrtFile *srt, const BracketReport *br) {
    fprintf(stderr, "Warning: %d cue(s) have unbalanced brackets:\n", br->count);
    for (int k = 0; k < br->listed; k++) {
        int ci = br->index[k], first, n;
        srt_cue_text_range(srt, ci, &first, &n);
        fprintf(stderr, "  cue %-5ld unmatched: %d opening, %d closing   \"",
                srt->cues[ci].seq, br->info[k].unmatched_open, br->info[k].unmatched_close);
        for (int j = 0; j < n; j++) {
            if (j) fputs(" | ", stderr);
            enc_write_line(stderr, srt->lines[first + j]);   /* shown as readable UTF-8 */
        }
        fputs("\"\n", stderr);
    }
    if (br->count > br->listed)
        fprintf(stderr, "  ... and %d more\n", br->count - br->listed);
    fprintf(stderr, "         (Only ( ) and [ ] are checked; list markers like \"1)\" and smileys like \":)\"\n"
                    "         are reported too.)\n");
}

int main(int argc, char **argv) {
    Options opt;
    if (!parse_args(argc, argv, &opt)) { usage(); return 1; }
    if (opt.rename_to_video && opt.output) {
        fprintf(stderr, "-r and -o cannot be combined (-r chooses the output name).\n");
        return 1;
    }
    if (opt.fix_overlaps && !opt.stretch)
        fprintf(stderr, "Note: -f only applies together with -t (only a stretch can create overlaps).\n");

    char dir[PATHBUF] = "", base[PATHBUF] = "", vname[PATHBUF] = "";
    char target[PATHBUF] = "", aside[PATHBUF] = "", defname[PATHBUF] = "";
    const char *output = opt.output;
    int process = opt.fix_encoding || opt.stretch || opt.shift_ms != 0 ||
                  opt.fix_numbering || opt.strip_tags || opt.bidi != BIDI_NONE;

    /* -r: work out the video-based name before touching anything */
    if (opt.rename_to_video) {
        path_dir(opt.input, dir, sizeof dir);
        if (!find_largest_video(dir, vname, sizeof vname)) {
            fprintf(stderr, "No video file found in %s\n", dir);
            return 1;
        }
        path_stem(vname, base, sizeof base);
        if (!path_fmt(target, sizeof target, "%s/%s.srt", dir, base)) {
            fprintf(stderr, "Path too long: %s/%s.srt\n", dir, base);
            return 1;
        }
        printf("Largest video: %s\n", vname);

        if (!process) return rename_only(opt.input, target, dir, base);
        output = target;                     /* the processed result takes the video's name */
    }

    SrtFile srt;
    SrtStatus st = srt_load(&srt, opt.input);
    if (st != SRT_OK) {
        fprintf(stderr, "%s: %s\n", opt.input, srt_strerror(st));
        return 1;
    }

    NumberingInfo num;
    if (!opt.fix_numbering && srt_check_numbering(&srt, &num)) {
        fprintf(stderr, "Warning: cue numbers are not sequential (%d break(s)); first: ", num.count);
        if (num.first_pos == 0)
            fprintf(stderr, "the file starts at cue %ld instead of 1.\n", num.first_seq);
        else
            fprintf(stderr, "cue %ld follows cue %ld (expected %ld).\n",
                    num.first_seq, num.first_prev, num.first_prev + 1);
        fprintf(stderr, "         Numbers are kept as written in the file, and -t refers to them as written.\n");
        fprintf(stderr, "         Hint: add -n to renumber the cues from 1.\n");
    }

    BracketReport brackets;
    if (srt_check_brackets(&srt, &brackets))
        report_brackets(&srt, &brackets);

    int rc = 1, moved = 0;
    TextOptions topt = { opt.fix_encoding, opt.strip_tags, opt.bidi };
    TextStats tstats;
    memset(&tstats, 0, sizeof tstats);
    StretchInfo info = {0, 0};
    unsigned char *overlaps_before = NULL;   /* which cues overlapped the next one in the input */

    if (opt.stretch) {
        overlaps_before = timing_overlap_flags(&srt);
        if (!overlaps_before) { fprintf(stderr, "Out of memory\n"); goto done; }

        switch (timing_stretch(&srt, opt.anchor_seq, opt.target_seq, opt.correct_ms, &info)) {
            case TIMING_NOT_FOUND:
                fprintf(stderr, "Anchor or target sequence number not found in the file.\n");
                goto done;
            case TIMING_SAME_START:
                fprintf(stderr, "Anchor and target start at the same time; cannot compute a scale.\n");
                goto done;
            case TIMING_BAD_SCALE:
                fprintf(stderr, "The corrected time lies on the opposite side of the anchor from the "
                                "target cue (scale %.4f); that would reverse or collapse the cues.\n",
                        info.scale);
                goto done;
            case TIMING_OK:
                break;
        }
    }
    if (opt.shift_ms) timing_shift(&srt, opt.shift_ms);

    /* A stretch must never lose data: abort instead of clamping negative times to zero. */
    if (opt.stretch) {
        NegativeInfo neg;
        if (timing_find_negative(&srt, &neg)) {
            report_negative(&srt, &neg);
            goto done;
        }
        printf("Scale factor: %.8f\n", info.scale);
        if (info.end_shift_sec > -0.0005 && info.end_shift_sec < 0.0005)
            printf("No change noticed\n");
        else
            printf("Sync moved the end of the file by %+.3f seconds (%s)\n",
                   info.end_shift_sec, info.end_shift_sec > 0 ? "later" : "earlier");

        OverlapInfo ov;
        if (timing_new_overlaps(&srt, overlaps_before, opt.fix_overlaps, &ov))
            report_overlaps(&srt, &ov, opt.fix_overlaps);
    }

    int renumber_changed = 0;
    if (opt.fix_numbering) renumber_changed = srt_renumber(&srt);   /* after -t, which used the file's numbers */

    if (!output) {
        if (!path_fixed_name(opt.input, defname, sizeof defname)) {
            fprintf(stderr, "Output path too long\n");
            goto done;
        }
        output = defname;
    }

    /* -r: an existing <video>.srt is renamed out of the way before we write */
    if (opt.rename_to_video && path_exists(output)) {
        if (!move_aside(output, dir, base, aside, sizeof aside)) {
            fprintf(stderr, "Cannot rename existing %s out of the way\n", output);
            goto done;
        }
        moved = 1;
        printf("Existing %s renamed to %s\n", output, aside);
    }

    st = srt_write(&srt, output, &topt, &tstats);
    if (st != SRT_OK) {
        fprintf(stderr, "%s: %s\n", output, srt_strerror(st));
        if (moved) rename(aside, output);    /* put the old file back */
        goto done;
    }

    if (opt.fix_encoding)
        printf("Text lines: %d unchanged, %d converted from windows-1256, %d repaired (mojibake)\n",
               tstats.enc.unchanged, tstats.enc.converted, tstats.enc.repaired);
    if (opt.strip_tags)
        printf("Removed %d tag(s)\n", tstats.tags_removed);
    if (opt.bidi != BIDI_NONE)
        printf("Arabic line-end fix: inserted %d %s in %d line(s)\n", tstats.bidi_marks,
               opt.bidi == BIDI_RLM ? "RLM mark(s)" : "tatweel(s)", tstats.bidi_lines);
    if (opt.fix_numbering) {
        if (renumber_changed)
            printf("Renumbered the cues 1..%d (%d number(s) changed)\n", srt.ncues, renumber_changed);
        else
            printf("Cue numbers already run 1..%d; nothing to renumber\n", srt.ncues);
    }
    printf("Written: %s\n", output);
    rc = 0;

done:
    free(overlaps_before);
    srt_free(&srt);
    return rc;
}
