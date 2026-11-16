#ifndef CRT_DISPLAY_H
#define CRT_DISPLAY_H

#include <SDL.h>

#include "config.h"

typedef enum {
    DISPLAY_MODE_LOWRES,   /* the configured CRT resolution */
    DISPLAY_MODE_DESKTOP   /* the desktop's native resolution */
} DisplayMode;

typedef struct {
    SDL_Window *window;
    AppConfig cfg;

    DisplayMode mode;
    int width;      /* current window client size in pixels */
    int height;

    /* SDL_TRUE if low-res is a real display mode, which we then set
       ourselves; SDL_FALSE when faked with a plain window because the
       display doesn't have it. */
    SDL_bool lowres_is_real;

    /* What display_reclaim() goes back to. */
    DisplayMode released_from;
} DisplayContext;

/* Sets the configured low-res mode under a borderless window (never SDL's
   exclusive fullscreen: that ties the mode to focus). Without that exact
   mode, a plain window of the same pixel size. SDL_FALSE if no window. */
SDL_bool display_init(const AppConfig *cfg, DisplayContext *ctx);

/* Toggles between low-res and the desktop's native resolution. */
void display_toggle(DisplayContext *ctx);

/* Hands the screen to a launched game: back to the desktop mode, so an
   emulator that never switches modes itself doesn't inherit ours. Call
   before the game runs; pair with display_reclaim() once it has exited. */
void display_release(DisplayContext *ctx);

/* Takes the screen back: the mode display_release() left, re-asserted
   whatever the game did to it, and our window brought forward. */
void display_reclaim(DisplayContext *ctx);

/* Restores the desktop mode and destroys the window. */
void display_shutdown(DisplayContext *ctx);

#endif /* CRT_DISPLAY_H */
