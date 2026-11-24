# srtfix

A tool that is helpful to fix the encoding of subtiles files (srt format).


```
Usage: srtfix <file.srt> [-e] [-s <ms>] [-t <anchorSeq> <targetSeq> <hh:mm:ss,fff>] [-o <out.srt>]
  -e   fix subtitle text encoding -> UTF-8. Handles genuine windows-1256 bytes and
       1256 text that was misread as 1252 and saved as UTF-8 (mojibake)
  -s   shift all timestamps by <ms> (negative allowed)
  -t   keep cue <anchorSeq> fixed; cue <targetSeq> should start at the given time;
       linearly stretch/shrink everything from the anchor to the end of the file
  -o   output file (default: <name>.fixed.srt)
```
