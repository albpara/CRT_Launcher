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
    int refresh_rate; /* Hz for the currently active mode, 0 if unknown/windowed */

    /* SDL_TRUE if the low-res mode is a real exclusive-fullscreen display
       mode the GPU/driver reported; SDL_FALSE if we're only faking it with
       a plain windowed surface because the mode wasn't available. */
    SDL_bool lowres_is_exclusive;
} DisplayContext;

/* Creates the window and attempts to apply the configured low-res mode as
   an exclusive-fullscreen display mode. If that exact mode isn't reported
   by the display, falls back to an ordinary windowed surface at the same
   pixel size (no stretching either way). Returns SDL_FALSE on unrecoverable
   window-creation failure. */
SDL_bool display_init(const AppConfig *cfg, DisplayContext *ctx);

/* Toggles between the low-res mode and the desktop's native resolution.
   Logs which path it took (fullscreen-desktop vs. windowed low-res). */
void display_toggle(DisplayContext *ctx);

void display_shutdown(DisplayContext *ctx);

#endif /* CRT_DISPLAY_H */
