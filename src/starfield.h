#ifndef CRT_STARFIELD_H
#define CRT_STARFIELD_H

#include <SDL.h>

/* Galaga starfield (Namco 05XX). Used both as the screensaver -- drifting
   lit pixels protect a CRT better than a static screen -- and, optionally,
   as the launcher background. Stars are always a single pixel, at any
   resolution. */

typedef struct {
    Sint16 x, y;   /* position in the star field */
    Uint8 set;     /* 0-3; which blink group this star belongs to */
    Uint8 color;   /* palette index 0-63 */
} StarfieldStar;

typedef struct {
    StarfieldStar *stars;
    int star_count;
    int field_w;   /* field width the stars were generated for */

    float scroll[2];    /* per-layer scroll offset, in pixels */
    float blink_accum;
    int q3, q4;         /* mirror the hardware blink latch bits */
    Uint32 last_time;

    SDL_Color palette[64];
} Starfield;

void starfield_init(Starfield *s);
void starfield_free(Starfield *s);

/* Advances the animation and draws the stars onto the current render
   target -- no clear, no present, so the caller owns the frame. Generates
   the field on first use and regenerates it if the window width changed
   (e.g. the resolution hotkey). */
void starfield_draw(Starfield *s, SDL_Renderer *renderer, int win_w, int win_h);

#endif /* CRT_STARFIELD_H */
