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

    /* SDL_TRUE if low-res is a real exclusive-fullscreen mode; SDL_FALSE
       when faked with a plain window because the mode wasn't available. */
    SDL_bool lowres_is_exclusive;
} DisplayContext;

/* Creates the window in the configured low-res mode (exclusive fullscreen
   if the display reports that exact mode, else a windowed surface at the
   same pixel size). Returns SDL_FALSE on window-creation failure. */
SDL_bool display_init(const AppConfig *cfg, DisplayContext *ctx);

/* Toggles between low-res and the desktop's native resolution. */
void display_toggle(DisplayContext *ctx);

void display_shutdown(DisplayContext *ctx);

#endif /* CRT_DISPLAY_H */
