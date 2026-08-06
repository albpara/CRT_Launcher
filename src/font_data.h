#ifndef CRT_FONT_DATA_H
#define CRT_FONT_DATA_H

#include <stdint.h>

/*
 * PLACEHOLDER FONT.
 *
 * This is a hand-authored 5x5-pixel block font, drawn glyph-by-glyph as
 * filled SDL_Rects (see render_draw_text in render.c) rather than sampled
 * from a texture. That sidesteps needing any font asset or filtering
 * settings at all for this proof-of-concept -- it is crisp by construction.
 *
 * It only covers A-Z, 0-9, space, '-', ':', '.', '(', ')', '>' --
 * enough for this POC's status text. It is NOT the eventual font pipeline: swap this out for
 * a real BMFont-style (image + glyph descriptor) bitmap font, e.g. a
 * licensed pixel font like "Press Start 2P" (SIL OFL) exported as a sprite
 * sheet, once the real UI work starts.
 *
 * Each glyph is 5 rows, each row is the low 5 bits of a byte, MSB-first
 * (bit 4 = leftmost pixel, bit 0 = rightmost pixel).
 */

#define FONT_GLYPH_WIDTH  5
#define FONT_GLYPH_HEIGHT 5

static const uint8_t FONT_SPACE[FONT_GLYPH_HEIGHT] = {0, 0, 0, 0, 0};

static const uint8_t FONT_A[FONT_GLYPH_HEIGHT] = {14, 17, 31, 17, 17};
static const uint8_t FONT_B[FONT_GLYPH_HEIGHT] = {30, 17, 30, 17, 30};
static const uint8_t FONT_C[FONT_GLYPH_HEIGHT] = {15, 16, 16, 16, 15};
static const uint8_t FONT_D[FONT_GLYPH_HEIGHT] = {30, 17, 17, 17, 30};
static const uint8_t FONT_E[FONT_GLYPH_HEIGHT] = {31, 16, 30, 16, 31};
static const uint8_t FONT_F[FONT_GLYPH_HEIGHT] = {31, 16, 30, 16, 16};
static const uint8_t FONT_G[FONT_GLYPH_HEIGHT] = {15, 16, 18, 17, 15};
static const uint8_t FONT_H[FONT_GLYPH_HEIGHT] = {17, 17, 31, 17, 17};
static const uint8_t FONT_I[FONT_GLYPH_HEIGHT] = {31, 4, 4, 4, 31};
static const uint8_t FONT_J[FONT_GLYPH_HEIGHT] = {7, 2, 2, 18, 12};
static const uint8_t FONT_K[FONT_GLYPH_HEIGHT] = {17, 18, 28, 18, 17};
static const uint8_t FONT_L[FONT_GLYPH_HEIGHT] = {16, 16, 16, 16, 31};
static const uint8_t FONT_M[FONT_GLYPH_HEIGHT] = {17, 27, 21, 17, 17};
static const uint8_t FONT_N[FONT_GLYPH_HEIGHT] = {17, 25, 21, 19, 17};
static const uint8_t FONT_O[FONT_GLYPH_HEIGHT] = {14, 17, 17, 17, 14};
static const uint8_t FONT_P[FONT_GLYPH_HEIGHT] = {30, 17, 30, 16, 16};
static const uint8_t FONT_Q[FONT_GLYPH_HEIGHT] = {14, 17, 17, 18, 15};
static const uint8_t FONT_R[FONT_GLYPH_HEIGHT] = {30, 17, 30, 18, 17};
static const uint8_t FONT_S[FONT_GLYPH_HEIGHT] = {15, 16, 14, 1, 30};
static const uint8_t FONT_T[FONT_GLYPH_HEIGHT] = {31, 4, 4, 4, 4};
static const uint8_t FONT_U[FONT_GLYPH_HEIGHT] = {17, 17, 17, 17, 14};
static const uint8_t FONT_V[FONT_GLYPH_HEIGHT] = {17, 17, 17, 10, 4};
static const uint8_t FONT_W[FONT_GLYPH_HEIGHT] = {17, 17, 21, 21, 10};
static const uint8_t FONT_X[FONT_GLYPH_HEIGHT] = {17, 10, 4, 10, 17};
static const uint8_t FONT_Y[FONT_GLYPH_HEIGHT] = {17, 10, 4, 4, 4};
static const uint8_t FONT_Z[FONT_GLYPH_HEIGHT] = {31, 2, 4, 8, 31};

static const uint8_t FONT_0[FONT_GLYPH_HEIGHT] = {14, 18, 21, 25, 14};
static const uint8_t FONT_1[FONT_GLYPH_HEIGHT] = {4, 12, 4, 4, 14};
static const uint8_t FONT_2[FONT_GLYPH_HEIGHT] = {14, 17, 2, 4, 31};
static const uint8_t FONT_3[FONT_GLYPH_HEIGHT] = {30, 1, 6, 1, 30};
static const uint8_t FONT_4[FONT_GLYPH_HEIGHT] = {2, 6, 10, 31, 2};
static const uint8_t FONT_5[FONT_GLYPH_HEIGHT] = {31, 16, 30, 1, 30};
static const uint8_t FONT_6[FONT_GLYPH_HEIGHT] = {6, 8, 30, 17, 14};
static const uint8_t FONT_7[FONT_GLYPH_HEIGHT] = {31, 1, 2, 4, 4};
static const uint8_t FONT_8[FONT_GLYPH_HEIGHT] = {14, 17, 14, 17, 14};
static const uint8_t FONT_9[FONT_GLYPH_HEIGHT] = {14, 17, 15, 1, 14};

static const uint8_t FONT_HYPHEN[FONT_GLYPH_HEIGHT] = {0, 0, 31, 0, 0};
static const uint8_t FONT_COLON[FONT_GLYPH_HEIGHT] = {0, 4, 0, 4, 0};
static const uint8_t FONT_PERIOD[FONT_GLYPH_HEIGHT] = {0, 0, 0, 0, 4};
static const uint8_t FONT_LPAREN[FONT_GLYPH_HEIGHT] = {2, 4, 4, 4, 2};
static const uint8_t FONT_RPAREN[FONT_GLYPH_HEIGHT] = {8, 4, 4, 4, 8};
static const uint8_t FONT_APOSTROPHE[FONT_GLYPH_HEIGHT] = {8, 8, 0, 0, 0};

/* Placeholder for any character outside this small supported set (accented
   Latin letters, CJK, punctuation we haven't drawn, ...) -- real LaunchBox
   titles have plenty of these (e.g. Japanese-only titles, or ones starting
   with an accented letter). Deliberately a visible mark rather than blank:
   rendering unsupported characters as space made such titles look empty or
   like they had stray leading whitespace, indistinguishable from an
   actually-blank title. */
static const uint8_t FONT_UNKNOWN[FONT_GLYPH_HEIGHT] = {4, 14, 31, 14, 4};

/* Disclosure indicator for game-list rows that have multiple versions to
   pick from (see render.c's version-picker modal) -- not literal
   punctuation, just mapped onto '>' since it's otherwise unneeded and
   safe from colliding with real title text (unlike, say, a letter). */
static const uint8_t FONT_ARROW_RIGHT[FONT_GLYPH_HEIGHT] = {16, 24, 28, 24, 16};

/* Returns the glyph bitmap for `c` (case-insensitive). Anything outside
   this small supported set -- including literal space -- falls through to
   FONT_UNKNOWN, a visible placeholder mark, EXCEPT space itself, which is
   the one character that's supposed to render blank. That split is what
   keeps an actually-blank title distinguishable from one full of
   characters we just can't draw (see FONT_UNKNOWN's comment). Callers
   never need a NULL check either way. */
static inline const uint8_t *font_get_glyph(char c) {
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - ('a' - 'A'));
    }

    switch (c) {
        case 'A': return FONT_A; case 'B': return FONT_B; case 'C': return FONT_C;
        case 'D': return FONT_D; case 'E': return FONT_E; case 'F': return FONT_F;
        case 'G': return FONT_G; case 'H': return FONT_H; case 'I': return FONT_I;
        case 'J': return FONT_J; case 'K': return FONT_K; case 'L': return FONT_L;
        case 'M': return FONT_M; case 'N': return FONT_N; case 'O': return FONT_O;
        case 'P': return FONT_P; case 'Q': return FONT_Q; case 'R': return FONT_R;
        case 'S': return FONT_S; case 'T': return FONT_T; case 'U': return FONT_U;
        case 'V': return FONT_V; case 'W': return FONT_W; case 'X': return FONT_X;
        case 'Y': return FONT_Y; case 'Z': return FONT_Z;
        case '0': return FONT_0; case '1': return FONT_1; case '2': return FONT_2;
        case '3': return FONT_3; case '4': return FONT_4; case '5': return FONT_5;
        case '6': return FONT_6; case '7': return FONT_7; case '8': return FONT_8;
        case '9': return FONT_9;
        case ' ': return FONT_SPACE;
        case '\'': return FONT_APOSTROPHE;
        case '-': return FONT_HYPHEN;
        case ':': return FONT_COLON;
        case '.': return FONT_PERIOD;
        case '(': return FONT_LPAREN;
        case ')': return FONT_RPAREN;
        case '>': return FONT_ARROW_RIGHT;
        default: return FONT_UNKNOWN;
    }
}

#endif /* CRT_FONT_DATA_H */
