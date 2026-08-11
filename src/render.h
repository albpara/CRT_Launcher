#ifndef CRT_RENDER_H
#define CRT_RENDER_H

#include <SDL.h>

#include "display.h"
#include "gamelist.h"
#include "launchbox.h"
#include "starfield.h"

/* Laid out in font_data.h; only render.c needs its contents. */
struct BitmapFont;

typedef struct {
    SDL_Renderer *renderer;
    /* Picked once from cfg->font -- there's no hot-reload. */
    const struct BitmapFont *font;
} RenderContext;

/* Creates a nearest-neighbor renderer bound to `window`. */
SDL_bool render_init(SDL_Window *window, const AppConfig *cfg, RenderContext *rc);

/* Draws one frame: background (cfg->background), title + game counter,
   the scrollable list, and whichever modal is open. Mutates
   gl->scroll_offset (layout is decided here). `sf` is only touched for
   the starfield background. */
void render_frame(RenderContext *rc, const DisplayContext *dc, const AppConfig *cfg,
                   const LaunchboxInfo *lb, GameListState *gl, Starfield *sf);

/* The screensaver frame: starfield on black, nothing else. */
void render_screensaver_frame(RenderContext *rc, const DisplayContext *dc, Starfield *sf);

/* Generic modal: dim overlay + centered box of plain strings, one
   optionally highlighted (selected_index, -1 for none). min_width_chars 0
   sizes purely from content. Exposed for reuse by future menus. */
void render_draw_modal_list(const RenderContext *rc, int win_w, int win_h, int text_scale,
                             const char *title, const char *const *items, int item_count,
                             int selected_index, int min_width_chars, SDL_Color accent_color);

void render_shutdown(RenderContext *rc);

#endif /* CRT_RENDER_H */
