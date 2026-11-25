/* main.c - srtfix: argument parsing and orchestration only */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "encoding.h"
#include "fsutil.h"
#include "srt.h"
#include "timing.h"

typedef struct {
    const char *input;
    const char *output;          /* -o, or NULL */
    int  fix_encoding;           /* -e */
    int  rename_to_video;        /* -r */
    int  fix_numbering;          /* -n */
    int  stretch;                /* -t was given */
    long anchor_seq, target_seq, correct_ms;
    long shift_ms;               /* -s */
} Options;

static void usage(void) {
    fprintf(stderr,
        "Usage: srtfix <file.srt> [-e] [-s <ms>] [-t <anchorSeq> <targetSeq> <hh:mm:ss,fff>] [-n] [-r] [-o <out.srt>]\n"
        "  -e   fix subtitle text encoding -> UTF-8. Handles genuine windows-1256 bytes and\n"
        "       1256 text that was misread as 1252 and saved as UTF-8 (mojibake)\n"
        "  -s   shift all timestamps by <ms> (negative allowed)\n"
        "  -t   keep the start of cue <anchorSeq> fixed; cue <targetSeq> should start at the\n"
        "       given time; stretch/shrink all cues linearly around the anchor\n"
        "  -n   renumber the cues 1, 2, 3, ... in the output (-t still uses the numbers\n"
        "       as written in the input file)\n"
        "  -r   name the result after the largest video file in the same folder\n"
        "       (an existing <video>.srt is renamed to <video>.old.srt first)\n"
        "  -o   output file (default: <name>.fixed.srt; cannot be combined with -r)\n");
}

static int parse_args(int argc, char **argv, Options *o) {
    memset(o, 0, sizeof *o);
    if (argc < 2) return 0;
    o->input = argv[1];

    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-e")) {
            o->fix_encoding = 1;
        } else if (!strcmp(argv[i], "-n")) {
            o->fix_numbering = 1;
        } else if (!strcmp(argv[i], "-r")) {
            o->rename_to_video = 1;
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            o->shift_ms = atol(argv[++i]);
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

int main(int argc, char **argv) {
    Options opt;
    if (!parse_args(argc, argv, &opt)) { usage(); return 1; }
    if (opt.rename_to_video && opt.output) {
        fprintf(stderr, "-r and -o cannot be combined (-r chooses the output name).\n");
        return 1;
    }

    char dir[PATHBUF] = "", base[PATHBUF] = "", vname[PATHBUF] = "";
    char target[PATHBUF] = "", aside[PATHBUF] = "", defname[PATHBUF] = "";
    const char *output = opt.output;
    int process = opt.fix_encoding || opt.stretch || opt.shift_ms != 0 || opt.fix_numbering;

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

    int rc = 1, moved = 0;
    EncStats stats = {0, 0, 0};

    StretchInfo info = {0, 0};

    if (opt.stretch) {
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
            fprintf(stderr, "Aborted: the stretch would give %d cue(s) a negative timestamp. "
                            "Nothing was written.\n", neg.count);
            for (int k = 0; k < neg.listed; k++) {
                const Cue *c = &srt.cues[neg.index[k]];
                double lowest = c->start < c->end ? c->start : c->end;
                const char *txt = srt_cue_text(&srt, c);
                fprintf(stderr, "  cue %-5ld would start at %.3f s  ", c->seq, lowest / 1000.0);
                if (*txt) {
                    fputc('"', stderr);
                    enc_write_line(stderr, txt);    /* shown as readable UTF-8 */
                    fputs("\"\n", stderr);
                } else {
                    fputs("(no text)\n", stderr);
                }
            }
            if (neg.count > neg.listed)
                fprintf(stderr, "  ... and %d more\n", neg.count - neg.listed);
            fprintf(stderr,
                "Check whether these are real subtitles (a welcome message or author credit can be\n"
                "removed). You can also use a later correct time, or add a positive -s shift.\n"
                "If you remove cues and retry, the remaining cues keep their numbers, so give -t\n"
                "the numbers as they appear in the file. Add -n to renumber the output from 1.\n");
            goto done;
        }
        printf("Scale factor: %.8f\n", info.scale);
        if (info.end_shift_sec > -0.0005 && info.end_shift_sec < 0.0005)
            printf("No change noticed\n");
        else
            printf("Sync moved the end of the file by %+.3f seconds (%s)\n",
                   info.end_shift_sec, info.end_shift_sec > 0 ? "later" : "earlier");
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

    st = srt_write(&srt, output, opt.fix_encoding, &stats);
    if (st != SRT_OK) {
        fprintf(stderr, "%s: %s\n", output, srt_strerror(st));
        if (moved) rename(aside, output);    /* put the old file back */
        goto done;
    }

    if (opt.fix_encoding)
        printf("Text lines: %d unchanged, %d converted from windows-1256, %d repaired (mojibake)\n",
               stats.unchanged, stats.converted, stats.repaired);
    if (opt.fix_numbering) {
        if (renumber_changed)
            printf("Renumbered the cues 1..%d (%d number(s) changed)\n", srt.ncues, renumber_changed);
        else
            printf("Cue numbers already run 1..%d; nothing to renumber\n", srt.ncues);
    }
    printf("Written: %s\n", output);
    rc = 0;

done:
    srt_free(&srt);
    return rc;
}
