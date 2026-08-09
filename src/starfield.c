#include "starfield.h"

#include <math.h>
#include <stdlib.h>

/* Namco 05XX parameters, per MAME's starfield_05xx.cpp: a 16-bit LFSR
   (taps 0/3/5/10) clocked 256 times per line over 256 lines -- exactly
   one full 65535-step period -- so the field repeats every 256 rows and
   scrolling wraps seamlessly there. A star is emitted whenever the LFSR
   matches the hit pattern, which happens for 1 state in 256. */
#define STARFIELD_H 256
#define LFSR_SEED 0x7fff
#define LFSR_HIT_MASK 0xfa14
#define LFSR_HIT_VALUE 0x7800

/* Scroll speeds in px/sec: sets 0-1 form the slow layer, 2-3 the fast
   one. Hardware takes these from the game CPU; we just pick something
   pleasant. */
#define SCROLL_SLOW 30.0f
#define SCROLL_FAST 60.0f
#define BLINK_RATE 2.0f /* toggles per second */

static Uint16 next_lfsr(Uint16 lfsr) {
    int bit = (lfsr ^ (lfsr >> 3) ^ (lfsr >> 5) ^ (lfsr >> 10)) & 1;
    return (Uint16)((lfsr >> 1) | (bit << 15));
}

/* 6-bit R2G2B2, each channel 0/85/170/255. */
static void build_palette(SDL_Color *palette) {
    for (int i = 0; i < 64; i++) {
        palette[i].r = (Uint8)(((i >> 0) & 3) * 85);
        palette[i].g = (Uint8)(((i >> 2) & 3) * 85);
        palette[i].b = (Uint8)(((i >> 4) & 3) * 85);
        palette[i].a = 255;
    }
}

/* Walks the LFSR across a `field_w` x STARFIELD_H field and records every
   hit. Counts first so the array can be sized exactly. Using the live
   window width (rather than stretching a fixed-width field) keeps the
   hardware's per-pixel density: a wider screen just gets more field. */
static void generate_stars(Starfield *s, int field_w) {
    free(s->stars);
    s->stars = NULL;
    s->star_count = 0;
    s->field_w = field_w;

    if (field_w <= 0) {
        return;
    }

    Uint16 lfsr = LFSR_SEED;
    int count = 0;
    for (int i = 0; i < field_w * STARFIELD_H; i++) {
        lfsr = next_lfsr(lfsr);
        if ((lfsr & LFSR_HIT_MASK) == LFSR_HIT_VALUE) {
            count++;
        }
    }
    if (count == 0) {
        return;
    }

    StarfieldStar *stars = (StarfieldStar *)malloc((size_t)count * sizeof(StarfieldStar));
    if (!stars) {
        SDL_Log("[starfield] WARNING: out of memory building starfield");
        return;
    }

    lfsr = LFSR_SEED;
    int n = 0;
    for (int row = 0; row < STARFIELD_H && n < count; row++) {
        for (int col = 0; col < field_w && n < count; col++) {
            lfsr = next_lfsr(lfsr);
            if ((lfsr & LFSR_HIT_MASK) != LFSR_HIT_VALUE) {
                continue;
            }
            /* Set from bits 10 and 8; colour from scattered bits, inverted. */
            int color = ((lfsr >> 5) & 0x7) | ((lfsr << 3) & 0x18) | ((lfsr << 2) & 0x20);
            stars[n].x = (Sint16)col;
            stars[n].y = (Sint16)row;
            stars[n].set = (Uint8)((((lfsr >> 10) & 1) << 1) | ((lfsr >> 8) & 1));
            stars[n].color = (Uint8)((~color) & 0x3f);
            n++;
        }
    }
    s->stars = stars;
    s->star_count = n;
}

void starfield_init(Starfield *s) {
    SDL_zerop(s);
    build_palette(s->palette);
}

void starfield_free(Starfield *s) {
    free(s->stars);
    s->stars = NULL;
    s->star_count = 0;
    s->field_w = 0;
}

void starfield_draw(Starfield *s, SDL_Renderer *renderer, int win_w, int win_h) {
    if (win_w != s->field_w) {
        generate_stars(s, win_w);
    }

    /* Clamped so a stall -- first frame, or returning from a game --
       doesn't jump the field a huge distance at once. */
    Uint32 now = SDL_GetTicks();
    float dt = (float)(now - s->last_time) * 0.001f;
    if (dt < 0.0f || dt > 0.05f) {
        dt = 0.05f;
    }
    s->last_time = now;

    s->scroll[0] = fmodf(s->scroll[0] + SCROLL_SLOW * dt, (float)STARFIELD_H);
    s->scroll[1] = fmodf(s->scroll[1] + SCROLL_FAST * dt, (float)STARFIELD_H);

    /* One low set (q3) and one high set (q4|2) are lit at a time, so half
       the stars blink -- the hardware latches these on vblank. q4 runs
       half a period out of phase with q3. */
    float period = 1.0f / BLINK_RATE;
    s->blink_accum += dt;
    if (s->blink_accum > period * 1024.0f) {
        s->blink_accum -= period * 1024.0f;
    }
    s->q3 = (int)(s->blink_accum / period) & 1;
    s->q4 = (int)((s->blink_accum + period * 0.5f) / period) & 1;

    int set_a = s->q3;
    int set_b = s->q4 | 2;
    int scroll_slow = (int)s->scroll[0];
    int scroll_fast = (int)s->scroll[1];

    for (int i = 0; i < s->star_count; i++) {
        const StarfieldStar *star = &s->stars[i];
        if (star->set != set_a && star->set != set_b) {
            continue;
        }

        int scroll = (star->set & 2) ? scroll_fast : scroll_slow;
        int y = (star->y + scroll) % STARFIELD_H;
        if (y >= win_h) {
            continue;
        }

        const SDL_Color *c = &s->palette[star->color];
        SDL_SetRenderDrawColor(renderer, c->r, c->g, c->b, 255);
        SDL_RenderDrawPoint(renderer, star->x, y);
    }
}
