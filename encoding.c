/* encoding.c - windows-1256 / mojibake -> UTF-8 conversion of text lines */
#include "encoding.h"

/* windows-1256 0x80..0xFF -> Unicode */
static const unsigned short CP1256[128] = {
    0x20AC,0x067E,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,
    0x02C6,0x2030,0x0679,0x2039,0x0152,0x0686,0x0698,0x0688,
    0x06AF,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,
    0x06A9,0x2122,0x0691,0x203A,0x0153,0x200C,0x200D,0x06BA,
    0x00A0,0x060C,0x00A2,0x00A3,0x00A4,0x00A5,0x00A6,0x00A7,
    0x00A8,0x00A9,0x06BE,0x00AB,0x00AC,0x00AD,0x00AE,0x00AF,
    0x00B0,0x00B1,0x00B2,0x00B3,0x00B4,0x00B5,0x00B6,0x00B7,
    0x00B8,0x00B9,0x061B,0x00BB,0x00BC,0x00BD,0x00BE,0x061F,
    0x06C1,0x0621,0x0622,0x0623,0x0624,0x0625,0x0626,0x0627,
    0x0628,0x0629,0x062A,0x062B,0x062C,0x062D,0x062E,0x062F,
    0x0630,0x0631,0x0632,0x0633,0x0634,0x0635,0x0636,0x00D7,
    0x0637,0x0638,0x0639,0x063A,0x0640,0x0641,0x0642,0x0643,
    0x00E0,0x0644,0x00E2,0x0645,0x0646,0x0647,0x0648,0x00E7,
    0x00E8,0x00E9,0x00EA,0x00EB,0x0649,0x064A,0x00EE,0x00EF,
    0x064B,0x064C,0x064D,0x064E,0x00F4,0x064F,0x0650,0x00F7,
    0x0651,0x00F9,0x0652,0x00FB,0x00FC,0x200E,0x200F,0x06D2
};

/* windows-1252 0x80..0x9F -> Unicode (used to undo the 1252 misreading;
 * 0xA0..0xFF map to the same code points in 1252) */
static const unsigned short CP1252_C1[32] = {
    0x20AC,0x0081,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,
    0x02C6,0x2030,0x0160,0x2039,0x0152,0x008D,0x017D,0x008F,
    0x0090,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,
    0x02DC,0x2122,0x0161,0x203A,0x0153,0x009D,0x017E,0x0178
};

/* Strict decode of one 1-3 byte UTF-8 sequence.
 * Returns its length, or 0 if the bytes are not valid UTF-8. */
int enc_utf8_decode(const unsigned char *s, unsigned *cp) {
    if (s[0] < 0x80) { *cp = s[0]; return 1; }
    if (s[0] >= 0xC2 && s[0] <= 0xDF && (s[1] & 0xC0) == 0x80) {
        *cp = ((s[0] & 0x1Fu) << 6) | (s[1] & 0x3Fu);
        return 2;
    }
    if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *cp = ((s[0] & 0x0Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu);
        return *cp >= 0x800 ? 3 : 0;
    }
    return 0;
}

/* Unicode code point -> the windows-1252 byte that decodes to it, or -1 */
static int cp1252_byte(unsigned cp) {
    if (cp < 0x80) return (int)cp;
    if (cp >= 0xA0 && cp <= 0xFF) return (int)cp;
    for (int i = 0; i < 32; i++)
        if (CP1252_C1[i] == cp) return 0x80 + i;
    return -1;
}

EncKind enc_convert_line(const char *line, StrBuf *out) {
    const unsigned char *s = (const unsigned char *)line, *p;
    unsigned cp;
    int len, ascii = 1, valid_utf8 = 1, all_1252 = 1, has_arabic = 0;

    /* Pass 1: classify the line */
    for (p = s; *p; ) {
        if (*p < 0x80) { p++; continue; }
        ascii = 0;
        len = enc_utf8_decode(p, &cp);
        if (!len) { valid_utf8 = 0; break; }
        int b = cp1252_byte(cp);
        if (b < 0) {
            all_1252 = 0;
        } else if (b >= 0x80) {
            unsigned u = CP1256[b - 0x80];
            if (u >= 0x0600 && u <= 0x06FF) has_arabic = 1;
        }
        p += len;
    }

    if (ascii) { sb_puts(out, line); return ENC_UNCHANGED; }

    if (!valid_utf8) {                       /* genuine windows-1256 bytes */
        for (p = s; *p; p++)
            sb_put_utf8(out, *p < 0x80 ? *p : CP1256[*p - 0x80]);
        return ENC_CONVERTED;
    }

    if (all_1252 && has_arabic) {            /* mojibake: UTF-8 of 1252-decoded 1256 bytes */
        for (p = s; *p; ) {
            len = enc_utf8_decode(p, &cp);
            int b = cp1252_byte(cp);         /* back to the original byte */
            sb_put_utf8(out, b < 0x80 ? (unsigned)b : CP1256[b - 0x80]);
            p += len;
        }
        return ENC_REPAIRED;
    }

    sb_puts(out, line);                      /* already proper UTF-8 */
    return ENC_UNCHANGED;
}

EncKind enc_write_line(FILE *out, const char *line) {
    StrBuf b;
    sb_init(&b);
    EncKind k = enc_convert_line(line, &b);
    fputs(sb_str(&b), out);
    sb_free(&b);
    return k;
}

void enc_stats_add(EncStats *st, EncKind kind) {
    switch (kind) {
        case ENC_CONVERTED: st->converted++; break;
        case ENC_REPAIRED:  st->repaired++;  break;
        default:            st->unchanged++; break;
    }
}
