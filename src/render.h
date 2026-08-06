#ifndef CRT_RENDER_H
#define CRT_RENDER_H

#include <SDL.h>

#include "display.h"
#include "gamelist.h"
#include "launchbox.h"

typedef struct {
    SDL_Renderer *renderer;
} RenderContext;

/* Creates a renderer bound to `window`. Sets SDL_HINT_RENDER_SCALE_QUALITY
   to "0" (nearest-neighbor) before creating it, so anything drawn -- and
   any future texture-based rendering -- stays pixel-crisp with no
   filtering. */
SDL_bool render_init(SDL_Window *window, RenderContext *rc);

/* Draws one frame: full-window checkerboard grid at native pixel
   resolution, a status text overlay showing the active mode/resolution,
   and a scrollable, highlighted list below it -- the system section
   (gamelist_system_entry_labels, gray, hidden above the top by default)
   followed by the LaunchBox game list, if one was found. If the selected
   game's version-picker modal is open (gl->selected_version >= 0) or a
   system entry's own modal is (gl->system_modal_open), draws it on top via
   render_draw_modal_list. Mutates `gl->scroll_offset` to keep the
   selected row on screen -- layout (how many rows fit) is decided here,
   not in gamelist.c. Presents the frame. */
void render_frame(RenderContext *rc, const DisplayContext *dc, const AppConfig *cfg,
                   const LaunchboxInfo *lb, GameListState *gl);

/* Draws a full-screen dim overlay plus a centered, bordered box listing
   `items` (`item_count` C strings), highlighting `selected_index`, with
   `title` as a header above them (pass NULL to omit). Sizes itself to fit
   the longest string, clamped to the window.

   Deliberately generic -- takes plain strings, knows nothing about games
   or versions -- so it's reusable for any future modal list (e.g. a
   settings menu), not just the version picker it was built for. Exposed
   here (rather than kept static in render.c) specifically so other
   callers can reuse it later. */
void render_draw_modal_list(SDL_Renderer *renderer, int win_w, int win_h, int text_scale,
                             const char *title, const char *const *items, int item_count,
                             int selected_index);

void render_shutdown(RenderContext *rc);

#endif /* CRT_RENDER_H */
