#ifndef CRT_FONT_DATA_H
#define CRT_FONT_DATA_H

#include <stdint.h>

/* Two selectable bitmap fonts, drawn as filled rects (see render.c). A
   glyph row is the low `width` bits of a byte, bit (width-1) = leftmost.
   Both tables are indexed by font_glyph_index(), so they must stay in the
   same slot order. */

enum {
    FONT_SLOT_A = 0,    /* A-Z occupy 0..25 */
    FONT_SLOT_0 = 26,   /* 0-9 occupy 26..35 */
    FONT_SLOT_SPACE = 36,
    FONT_SLOT_APOSTROPHE,
    FONT_SLOT_HYPHEN,
    FONT_SLOT_COLON,
    FONT_SLOT_PERIOD,
    FONT_SLOT_LPAREN,
    FONT_SLOT_RPAREN,
    FONT_SLOT_ARROW_RIGHT,
    FONT_SLOT_UNKNOWN,
    FONT_SLOT_COUNT
};

struct BitmapFont {
    int width;
    int height;
    const uint8_t *const *glyphs;  /* FONT_SLOT_COUNT entries, in slot order */
};
typedef struct BitmapFont BitmapFont;

/* --- compact: the original hand-drawn 5x5 set ------------------------- */

static const uint8_t COMPACT_A[5] = {14, 17, 31, 17, 17};
static const uint8_t COMPACT_B[5] = {30, 17, 30, 17, 30};
static const uint8_t COMPACT_C[5] = {15, 16, 16, 16, 15};
static const uint8_t COMPACT_D[5] = {30, 17, 17, 17, 30};
static const uint8_t COMPACT_E[5] = {31, 16, 30, 16, 31};
static const uint8_t COMPACT_F[5] = {31, 16, 30, 16, 16};
static const uint8_t COMPACT_G[5] = {15, 16, 18, 17, 15};
static const uint8_t COMPACT_H[5] = {17, 17, 31, 17, 17};
static const uint8_t COMPACT_I[5] = {31, 4, 4, 4, 31};
static const uint8_t COMPACT_J[5] = {7, 2, 2, 18, 12};
static const uint8_t COMPACT_K[5] = {17, 18, 28, 18, 17};
static const uint8_t COMPACT_L[5] = {16, 16, 16, 16, 31};
static const uint8_t COMPACT_M[5] = {17, 27, 21, 17, 17};
static const uint8_t COMPACT_N[5] = {17, 25, 21, 19, 17};
static const uint8_t COMPACT_O[5] = {14, 17, 17, 17, 14};
static const uint8_t COMPACT_P[5] = {30, 17, 30, 16, 16};
static const uint8_t COMPACT_Q[5] = {14, 17, 17, 18, 15};
static const uint8_t COMPACT_R[5] = {30, 17, 30, 18, 17};
static const uint8_t COMPACT_S[5] = {15, 16, 14, 1, 30};
static const uint8_t COMPACT_T[5] = {31, 4, 4, 4, 4};
static const uint8_t COMPACT_U[5] = {17, 17, 17, 17, 14};
static const uint8_t COMPACT_V[5] = {17, 17, 17, 10, 4};
static const uint8_t COMPACT_W[5] = {17, 17, 21, 21, 10};
static const uint8_t COMPACT_X[5] = {17, 10, 4, 10, 17};
static const uint8_t COMPACT_Y[5] = {17, 10, 4, 4, 4};
static const uint8_t COMPACT_Z[5] = {31, 2, 4, 8, 31};

static const uint8_t COMPACT_0[5] = {14, 18, 21, 25, 14};
static const uint8_t COMPACT_1[5] = {4, 12, 4, 4, 14};
static const uint8_t COMPACT_2[5] = {14, 17, 2, 4, 31};
static const uint8_t COMPACT_3[5] = {30, 1, 6, 1, 30};
static const uint8_t COMPACT_4[5] = {2, 6, 10, 31, 2};
static const uint8_t COMPACT_5[5] = {31, 16, 30, 1, 30};
static const uint8_t COMPACT_6[5] = {6, 8, 30, 17, 14};
static const uint8_t COMPACT_7[5] = {31, 1, 2, 4, 4};
static const uint8_t COMPACT_8[5] = {14, 17, 14, 17, 14};
static const uint8_t COMPACT_9[5] = {14, 17, 15, 1, 14};

static const uint8_t COMPACT_SPACE[5] = {0, 0, 0, 0, 0};
static const uint8_t COMPACT_APOSTROPHE[5] = {8, 8, 0, 0, 0};
static const uint8_t COMPACT_HYPHEN[5] = {0, 0, 31, 0, 0};
static const uint8_t COMPACT_COLON[5] = {0, 4, 0, 4, 0};
static const uint8_t COMPACT_PERIOD[5] = {0, 0, 0, 0, 4};
static const uint8_t COMPACT_LPAREN[5] = {2, 4, 4, 4, 2};
static const uint8_t COMPACT_RPAREN[5] = {8, 4, 4, 4, 8};
/* '>' doubles as the multi-version disclosure arrow in the game list. */
static const uint8_t COMPACT_ARROW_RIGHT[5] = {16, 24, 28, 24, 16};
/* Visible diamond for unsupported characters (accents, CJK, ...) -- blank
   would be indistinguishable from an actually-empty title. */
static const uint8_t COMPACT_UNKNOWN[5] = {4, 14, 31, 14, 4};

static const uint8_t *const COMPACT_GLYPHS[FONT_SLOT_COUNT] = {
    COMPACT_A, COMPACT_B, COMPACT_C, COMPACT_D, COMPACT_E, COMPACT_F,
    COMPACT_G, COMPACT_H, COMPACT_I, COMPACT_J, COMPACT_K, COMPACT_L,
    COMPACT_M, COMPACT_N, COMPACT_O, COMPACT_P, COMPACT_Q, COMPACT_R,
    COMPACT_S, COMPACT_T, COMPACT_U, COMPACT_V, COMPACT_W, COMPACT_X,
    COMPACT_Y, COMPACT_Z,
    COMPACT_0, COMPACT_1, COMPACT_2, COMPACT_3, COMPACT_4,
    COMPACT_5, COMPACT_6, COMPACT_7, COMPACT_8, COMPACT_9,
    COMPACT_SPACE, COMPACT_APOSTROPHE, COMPACT_HYPHEN, COMPACT_COLON,
    COMPACT_PERIOD, COMPACT_LPAREN, COMPACT_RPAREN, COMPACT_ARROW_RIGHT,
    COMPACT_UNKNOWN,
};

/* --- galaga88: A-Z 0-9 lifted from the Galaga '88 (PC Engine) ROM at
   offset 0x1C000; see the README. ------------------------------------- */

static const uint8_t GALAGA_A[7] = {62, 34, 34, 127, 97, 97, 97};
static const uint8_t GALAGA_B[7] = {126, 66, 66, 127, 97, 97, 127};
static const uint8_t GALAGA_C[7] = {127, 65, 64, 96, 96, 97, 127};
static const uint8_t GALAGA_D[7] = {126, 67, 65, 97, 97, 99, 126};
static const uint8_t GALAGA_E[7] = {127, 64, 64, 126, 96, 96, 127};
static const uint8_t GALAGA_F[7] = {127, 64, 64, 126, 96, 96, 96};
static const uint8_t GALAGA_G[7] = {127, 65, 64, 103, 97, 97, 127};
static const uint8_t GALAGA_H[7] = {65, 65, 65, 127, 97, 97, 97};
static const uint8_t GALAGA_I[7] = {8, 8, 8, 12, 12, 12, 12};
static const uint8_t GALAGA_J[7] = {2, 2, 2, 3, 3, 35, 63};
static const uint8_t GALAGA_K[7] = {66, 66, 66, 127, 97, 97, 97};
static const uint8_t GALAGA_L[7] = {32, 32, 32, 48, 48, 48, 63};
static const uint8_t GALAGA_M[7] = {127, 73, 73, 105, 105, 105, 105};
static const uint8_t GALAGA_N[7] = {63, 33, 33, 49, 49, 49, 49};
static const uint8_t GALAGA_O[7] = {127, 67, 67, 65, 65, 65, 127};
static const uint8_t GALAGA_P[7] = {127, 65, 65, 127, 96, 96, 96};
static const uint8_t GALAGA_Q[7] = {127, 65, 65, 65, 65, 79, 127};
static const uint8_t GALAGA_R[7] = {126, 66, 66, 127, 97, 97, 97};
static const uint8_t GALAGA_S[7] = {127, 65, 64, 127, 3, 67, 127};
static const uint8_t GALAGA_T[7] = {127, 8, 8, 12, 12, 12, 12};
static const uint8_t GALAGA_U[7] = {65, 65, 65, 97, 97, 97, 127};
static const uint8_t GALAGA_V[7] = {97, 97, 97, 97, 35, 34, 62};
static const uint8_t GALAGA_W[7] = {73, 73, 73, 105, 105, 105, 127};
static const uint8_t GALAGA_X[7] = {65, 65, 99, 62, 99, 97, 97};
static const uint8_t GALAGA_Y[7] = {33, 33, 33, 63, 12, 12, 12};
static const uint8_t GALAGA_Z[7] = {127, 65, 1, 127, 96, 97, 127};

static const uint8_t GALAGA_0[7] = {127, 65, 65, 67, 67, 67, 127};
static const uint8_t GALAGA_1[7] = {8, 8, 8, 24, 24, 24, 24};
static const uint8_t GALAGA_2[7] = {63, 1, 1, 63, 48, 48, 63};
static const uint8_t GALAGA_3[7] = {126, 66, 2, 63, 3, 67, 127};
static const uint8_t GALAGA_4[7] = {62, 98, 66, 70, 70, 127, 6};
static const uint8_t GALAGA_5[7] = {127, 64, 64, 127, 3, 67, 127};
static const uint8_t GALAGA_6[7] = {127, 65, 64, 127, 67, 67, 127};
static const uint8_t GALAGA_7[7] = {126, 2, 2, 6, 6, 6, 6};
static const uint8_t GALAGA_8[7] = {62, 34, 34, 127, 67, 67, 127};
static const uint8_t GALAGA_9[7] = {126, 66, 66, 126, 6, 6, 6};

static const uint8_t GALAGA_SPACE[7] = {0, 0, 0, 0, 0, 0, 0};

/* TODO revisit: the ROM has no punctuation, so everything below is a
   stand-in scaled up from the compact set -- redraw to match the Galaga
   weighting (1px horizontals, 2px verticals below the midline). */
static const uint8_t GALAGA_APOSTROPHE[7] = {24, 24, 0, 0, 0, 0, 0};
static const uint8_t GALAGA_HYPHEN[7] = {0, 0, 0, 62, 0, 0, 0};
static const uint8_t GALAGA_COLON[7] = {0, 24, 24, 0, 24, 24, 0};
static const uint8_t GALAGA_PERIOD[7] = {0, 0, 0, 0, 0, 24, 24};
static const uint8_t GALAGA_LPAREN[7] = {12, 24, 24, 24, 24, 24, 12};
static const uint8_t GALAGA_RPAREN[7] = {24, 12, 12, 12, 12, 12, 24};
static const uint8_t GALAGA_ARROW_RIGHT[7] = {48, 56, 60, 62, 60, 56, 48};
static const uint8_t GALAGA_UNKNOWN[7] = {8, 28, 62, 127, 62, 28, 8};

static const uint8_t *const GALAGA_GLYPHS[FONT_SLOT_COUNT] = {
    GALAGA_A, GALAGA_B, GALAGA_C, GALAGA_D, GALAGA_E, GALAGA_F,
    GALAGA_G, GALAGA_H, GALAGA_I, GALAGA_J, GALAGA_K, GALAGA_L,
    GALAGA_M, GALAGA_N, GALAGA_O, GALAGA_P, GALAGA_Q, GALAGA_R,
    GALAGA_S, GALAGA_T, GALAGA_U, GALAGA_V, GALAGA_W, GALAGA_X,
    GALAGA_Y, GALAGA_Z,
    GALAGA_0, GALAGA_1, GALAGA_2, GALAGA_3, GALAGA_4,
    GALAGA_5, GALAGA_6, GALAGA_7, GALAGA_8, GALAGA_9,
    GALAGA_SPACE, GALAGA_APOSTROPHE, GALAGA_HYPHEN, GALAGA_COLON,
    GALAGA_PERIOD, GALAGA_LPAREN, GALAGA_RPAREN, GALAGA_ARROW_RIGHT,
    GALAGA_UNKNOWN,
};

static const BitmapFont FONT_COMPACT = {5, 5, COMPACT_GLYPHS};
static const BitmapFont FONT_GALAGA88 = {7, 7, GALAGA_GLYPHS};

/* Slot for `c`, case-insensitive; unsupported characters get
   FONT_SLOT_UNKNOWN. Always in range. */
static inline int font_glyph_index(char c) {
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - ('a' - 'A'));
    }
    if (c >= 'A' && c <= 'Z') {
        return FONT_SLOT_A + (c - 'A');
    }
    if (c >= '0' && c <= '9') {
        return FONT_SLOT_0 + (c - '0');
    }

    switch (c) {
        case ' ':  return FONT_SLOT_SPACE;
        case '\'': return FONT_SLOT_APOSTROPHE;
        case '-':  return FONT_SLOT_HYPHEN;
        case ':':  return FONT_SLOT_COLON;
        case '.':  return FONT_SLOT_PERIOD;
        case '(':  return FONT_SLOT_LPAREN;
        case ')':  return FONT_SLOT_RPAREN;
        case '>':  return FONT_SLOT_ARROW_RIGHT;
        default:   return FONT_SLOT_UNKNOWN;
    }
}

/* Never returns NULL. */
static inline const uint8_t *font_glyph(const BitmapFont *f, char c) {
    return f->glyphs[font_glyph_index(c)];
}

#endif /* CRT_FONT_DATA_H */
