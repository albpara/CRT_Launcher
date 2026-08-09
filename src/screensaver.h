#ifndef CRT_SCREENSAVER_H
#define CRT_SCREENSAVER_H

#include <SDL.h>

/* Galaga starfield (Namco 05XX) drawn instead of a blank screen: the lit
   pixels keep drifting, so it protects a CRT better than solid black.
   Stars are always a single pixel, at any resolution. */

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
} Screensaver;

void screensaver_init(Screensaver *s);
void screensaver_free(Screensaver *s);

/* Restarts scroll/blink and the frame clock -- call when activating. */
void screensaver_reset(Screensaver *s, Uint32 now_ms);

/* Advances and draws a full frame, including Present. Regenerates the
   field if the window width changed (e.g. the resolution hotkey). */
void screensaver_draw(Screensaver *s, SDL_Renderer *renderer, int win_w, int win_h, Uint32 now_ms);

#endif /* CRT_SCREENSAVER_H */
