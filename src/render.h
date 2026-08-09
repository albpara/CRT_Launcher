#ifndef CRT_RENDER_H
#define CRT_RENDER_H

#include <SDL.h>

#include "display.h"
#include "gamelist.h"
#include "launchbox.h"

typedef struct {
    SDL_Renderer *renderer;
} RenderContext;

/* Creates a nearest-neighbor renderer bound to `window`. */
SDL_bool render_init(SDL_Window *window, RenderContext *rc);

/* Draws one frame: checkerboard, title + game counter, the scrollable
   list, and whichever modal is open. Mutates gl->scroll_offset (layout is
   decided here). Not called while the screensaver is up -- see
   screensaver_draw(). */
void render_frame(RenderContext *rc, const DisplayContext *dc, const AppConfig *cfg,
                   const LaunchboxInfo *lb, GameListState *gl);

/* Generic modal: dim overlay + centered box of plain strings, one
   optionally highlighted (selected_index, -1 for none). min_width_chars 0
   sizes purely from content. Exposed for reuse by future menus. */
void render_draw_modal_list(SDL_Renderer *renderer, int win_w, int win_h, int text_scale,
                             const char *title, const char *const *items, int item_count,
                             int selected_index, int min_width_chars, SDL_Color accent_color);

void render_shutdown(RenderContext *rc);

#endif /* CRT_RENDER_H */
