#include "render.h"

#include <stdio.h>
#include <string.h>

#include "font_data.h"

/* Text is sized relative to the configured low-res mode: at that exact
   resolution the scale is 1 (the smallest integer scale that still renders
   every glyph pixel -- see font_data.h). In desktop/fullscreen mode, where
   the window is much bigger than the low-res target, compute_text_scale()
   below picks the largest integer multiple of these base values that still
   fits, so the same crisp, unfiltered glyphs just get bigger instead of
   staying pinned at a handful of pixels on a 1080p+ screen. */
#define BASE_TEXT_SCALE 1
#define BASE_TEXT_MARGIN 8
/* Modal box internal padding -- kept as its own constant (rather than
   reusing BASE_TEXT_MARGIN) specifically so changing the main view's edge
   margin doesn't also resize the modal's padding. */
#define BASE_MODAL_PADDING 4
#define BASE_TEXT_LINE_GAP 2
#define GRID_CELL_SIZE 16
/* Cap for render_draw_modal_list's stack-allocated item array -- comfortably
   above any real LaunchBox game's version count (the largest observed while
   building this was 9), just a safety bound, not a expected-case limit. */
#define MODAL_MAX_ITEMS 128

static const SDL_Color COLOR_GRID_A = {32, 32, 40, 255};
static const SDL_Color COLOR_GRID_B = {70, 70, 90, 255};
static const SDL_Color COLOR_TEXT = {80, 255, 120, 255};
static const SDL_Color COLOR_FAVORITE = {255, 220, 40, 255};
static const SDL_Color COLOR_SYSTEM = {140, 140, 150, 255};
static const SDL_Color COLOR_TEXT_SHADOW = {0, 0, 0, 255};
static const SDL_Color COLOR_SELECT_TEXT = {10, 10, 15, 255};
static const SDL_Color COLOR_MODAL_DIM = {0, 0, 0, 180};
static const SDL_Color COLOR_MODAL_BG = {16, 16, 22, 255};
static const SDL_Color COLOR_MODAL_BORDER = {80, 255, 120, 255};

SDL_bool render_init(SDL_Window *window, RenderContext *rc) {
    /* Must be set before SDL_CreateRenderer -- "0"/"nearest" disables any
       bilinear/anisotropic filtering so every draw stays pixel-crisp. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    rc->renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!rc->renderer) {
        SDL_Log("[render] SDL_RENDERER_ACCELERATED failed (%s), retrying without vsync/acceleration flags",
                SDL_GetError());
        rc->renderer = SDL_CreateRenderer(window, -1, 0);
    }

    if (!rc->renderer) {
        SDL_Log("[render] FATAL: could not create renderer: %s", SDL_GetError());
        return SDL_FALSE;
    }

    SDL_Log("[render] Renderer created with nearest-neighbor scaling (SDL_HINT_RENDER_SCALE_QUALITY=0)");
    return SDL_TRUE;
}

static void draw_checkerboard(SDL_Renderer *renderer, int win_w, int win_h) {
    for (int y = 0; y < win_h; y += GRID_CELL_SIZE) {
        for (int x = 0; x < win_w; x += GRID_CELL_SIZE) {
            int col = x / GRID_CELL_SIZE;
            int row = y / GRID_CELL_SIZE;
            SDL_Color c = ((col + row) % 2 == 0) ? COLOR_GRID_A : COLOR_GRID_B;

            SDL_Rect cell;
            cell.x = x;
            cell.y = y;
            cell.w = (x + GRID_CELL_SIZE <= win_w) ? GRID_CELL_SIZE : (win_w - x);
            cell.h = (y + GRID_CELL_SIZE <= win_h) ? GRID_CELL_SIZE : (win_h - y);

            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(renderer, &cell);
        }
    }
}

/* Draws `text` as filled 1-pixel-multiple rects per glyph pixel -- see
   font_data.h for why this is a placeholder, not the final font pipeline. */
static int draw_text(SDL_Renderer *renderer, const char *text, int x, int y, int scale, SDL_Color color) {
    int cursor_x = x;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    for (const char *p = text; *p; p++) {
        const uint8_t *glyph = font_get_glyph(*p);

        for (int row = 0; row < FONT_GLYPH_HEIGHT; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < FONT_GLYPH_WIDTH; col++) {
                int bit = (bits >> (FONT_GLYPH_WIDTH - 1 - col)) & 1;
                if (!bit) {
                    continue;
                }
                SDL_Rect px;
                px.x = cursor_x + col * scale;
                px.y = y + row * scale;
                px.w = scale;
                px.h = scale;
                SDL_RenderFillRect(renderer, &px);
            }
        }

        cursor_x += (FONT_GLYPH_WIDTH + 1) * scale;
    }

    return cursor_x - x; /* total pixel width drawn */
}

static void draw_text_with_shadow(SDL_Renderer *renderer, const char *text, int x, int y, int scale) {
    /* Shadow offset is `scale` pixels (not a fixed 1px) so the drop shadow
       stays proportionally visible instead of shrinking to nothing once
       the glyphs themselves get bigger. */
    draw_text(renderer, text, x + scale, y + scale, scale, COLOR_TEXT_SHADOW);
    draw_text(renderer, text, x, y, scale, COLOR_TEXT);
}

/* Largest integer multiple of BASE_TEXT_SCALE that still lets the low-res
   reference resolution (`base_w` x `base_h`, i.e. the configured CRT mode)
   fit inside the actual window (`win_w` x `win_h`). Always at least
   BASE_TEXT_SCALE -- in low-res mode win size == base size, so this comes
   out to exactly 1; in desktop/fullscreen mode the window is bigger, so
   this scales up. Integer-only, so glyphs stay pixel-exact at any size. */
static int compute_text_scale(int win_w, int win_h, int base_w, int base_h) {
    if (base_w <= 0 || base_h <= 0) {
        return BASE_TEXT_SCALE;
    }

    int scale_w = win_w / base_w;
    int scale_h = win_h / base_h;
    int scale = (scale_w < scale_h) ? scale_w : scale_h;

    return (scale < BASE_TEXT_SCALE) ? BASE_TEXT_SCALE : scale;
}

/* Draws one row, either as a plain shadowed line or -- if `selected` -- as
   a highlight bar spanning [bar_x, bar_x+bar_w) with centered, contrasting
   text. `text_x` is independent of the bar bounds so the same helper works
   both for full-width list rows (bar_x=0, bar_w=window width) and for
   rows inside a narrower modal box (bar_x/bar_w = the box's content area).
   `text_color` doubles as the highlight bar's own fill color when
   selected -- so a favorite's bar highlights yellow, a system row's gray,
   a normal row green, instead of every row highlighting the same fixed
   color regardless of what it actually is. Only the text color switches to
   COLOR_SELECT_TEXT in that case, for contrast against the now-colored
   bar. */
static void draw_row(SDL_Renderer *renderer, const char *text, int text_x, int bar_x, int bar_w,
                      int y, int line_h, int text_scale, SDL_bool selected, SDL_Color text_color) {
    int text_y_offset = (line_h - FONT_GLYPH_HEIGHT * text_scale) / 2;
    int text_y = y + text_y_offset;

    if (selected) {
        SDL_Rect bar = {bar_x, y, bar_w, line_h};
        SDL_SetRenderDrawColor(renderer, text_color.r, text_color.g, text_color.b, 255);
        SDL_RenderFillRect(renderer, &bar);
        draw_text(renderer, text, text_x, text_y, text_scale, COLOR_SELECT_TEXT);
    } else {
        draw_text(renderer, text, text_x + text_scale, text_y + text_scale, text_scale, COLOR_TEXT_SHADOW);
        draw_text(renderer, text, text_x, text_y, text_scale, text_color);
    }
}

/* Draws the scrollable list starting at `list_y`: the system rows first
   (gamelist_system_entry_labels, in COLOR_SYSTEM gray -- see gamelist.h for
   why they're part of this same flat row space instead of a separate
   screen), then one row per LaunchboxGameGroup, with a "(N)>" suffix when
   it has multiple versions to pick from (see render_draw_modal_list for
   the picker that opens over this). Favorites are drawn in COLOR_FAVORITE,
   simply sorted ahead of the rest -- no divider between the two. Always
   flat -- versions never appear inline here anymore. `visible_rows` must
   already reflect the same layout (see render_frame, which computes it
   once and feeds it to gamelist_scroll_into_view() before calling this). */
static void draw_game_list(SDL_Renderer *renderer, const LaunchboxInfo *lb, const GameListState *gl,
                            int win_w, int list_y, int line_h, int visible_rows,
                            int text_scale, int text_margin) {
    int y = list_y;
    int rows_drawn = 0;
    int g = gl->scroll_offset;
    int total_rows = GAMELIST_SYSTEM_ENTRY_COUNT + lb->group_count;

    while (rows_drawn < visible_rows && g < total_rows) {
        if (g < GAMELIST_SYSTEM_ENTRY_COUNT) {
            SDL_bool selected = (g == gl->selected_group);
            draw_row(renderer, gamelist_system_entry_labels[g], text_margin, 0, win_w, y, line_h,
                     text_scale, selected, COLOR_SYSTEM);
            y += line_h;
            rows_drawn++;
            g++;
            continue;
        }

        const LaunchboxGameGroup *grp = &lb->groups[g - GAMELIST_SYSTEM_ENTRY_COUNT];
        SDL_bool selected = (g == gl->selected_group);
        SDL_Color color = grp->is_favorite ? COLOR_FAVORITE : COLOR_TEXT;

        char label[LAUNCHBOX_TITLE_MAX + 16];
        if (grp->version_count > 1) {
            snprintf(label, sizeof(label), "%s (%d)>", grp->title, grp->version_count);
        } else {
            snprintf(label, sizeof(label), "%s", grp->title);
        }

        draw_row(renderer, label, text_margin, 0, win_w, y, line_h, text_scale, selected, color);
        y += line_h;
        rows_drawn++;
        g++;
    }
}

/* Draws a full-screen dim overlay plus a centered, bordered box listing
   `items` (`item_count` C strings), highlighting `selected_index`, with
   `title` as a header above them (pass NULL to omit). Sizes itself to fit
   the longest string, clamped to the window so it can't overflow on a
   tiny low-res screen.

   Deliberately generic -- takes plain strings, knows nothing about games
   or versions -- so any future modal list (e.g. a settings menu) can
   reuse it as-is; it's just "a titled list of choices, one highlighted". */
void render_draw_modal_list(SDL_Renderer *renderer, int win_w, int win_h, int text_scale,
                             const char *title, const char *const *items, int item_count,
                             int selected_index) {
    int line_h = FONT_GLYPH_HEIGHT * text_scale + BASE_TEXT_LINE_GAP * text_scale;
    int padding = BASE_MODAL_PADDING * text_scale;
    int glyph_advance = (FONT_GLYPH_WIDTH + 1) * text_scale;

    int max_chars = title ? (int)strlen(title) : 0;
    for (int i = 0; i < item_count; i++) {
        int len = (int)strlen(items[i]);
        if (len > max_chars) {
            max_chars = len;
        }
    }

    /* A separator between the title and the list only makes sense if
       there's a title and something below it to separate from. */
    SDL_bool has_separator = (title != NULL) && (item_count > 0);

    int box_w = max_chars * glyph_advance + padding * 2;
    int box_h = (item_count + (title ? 1 : 0) + (has_separator ? 1 : 0)) * line_h + padding * 2;

    int max_w = win_w - padding * 4;
    int max_h = win_h - padding * 4;
    if (box_w > max_w) {
        box_w = max_w;
    }
    if (box_h > max_h) {
        box_h = max_h;
    }

    int box_x = (win_w - box_w) / 2;
    int box_y = (win_h - box_h) / 2;
    int border = text_scale;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, COLOR_MODAL_DIM.r, COLOR_MODAL_DIM.g, COLOR_MODAL_DIM.b, COLOR_MODAL_DIM.a);
    SDL_Rect dim = {0, 0, win_w, win_h};
    SDL_RenderFillRect(renderer, &dim);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    SDL_Rect outer = {box_x - border, box_y - border, box_w + border * 2, box_h + border * 2};
    SDL_SetRenderDrawColor(renderer, COLOR_MODAL_BORDER.r, COLOR_MODAL_BORDER.g, COLOR_MODAL_BORDER.b, 255);
    SDL_RenderFillRect(renderer, &outer);

    SDL_Rect inner = {box_x, box_y, box_w, box_h};
    SDL_SetRenderDrawColor(renderer, COLOR_MODAL_BG.r, COLOR_MODAL_BG.g, COLOR_MODAL_BG.b, 255);
    SDL_RenderFillRect(renderer, &inner);

    int y = box_y + padding;
    if (title) {
        draw_text_with_shadow(renderer, title, box_x + padding, y, text_scale);
        y += line_h;
    }

    if (has_separator) {
        SDL_Rect sep = {box_x + padding, y + (line_h - border) / 2, box_w - padding * 2, border};
        SDL_SetRenderDrawColor(renderer, COLOR_MODAL_BORDER.r, COLOR_MODAL_BORDER.g, COLOR_MODAL_BORDER.b, 255);
        SDL_RenderFillRect(renderer, &sep);
        y += line_h;
    }

    for (int i = 0; i < item_count; i++) {
        draw_row(renderer, items[i], box_x + padding, box_x, box_w, y, line_h, text_scale, i == selected_index, COLOR_TEXT);
        y += line_h;
    }
}

void render_frame(RenderContext *rc, const DisplayContext *dc, const AppConfig *cfg,
                   const LaunchboxInfo *lb, GameListState *gl) {
    SDL_Renderer *renderer = rc->renderer;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    draw_checkerboard(renderer, dc->width, dc->height);

    char line1[64];
    char line2[64];
    char line3[64];
    char line4[64];
    char line5[64];
    char line6[64];
    char line7[64];
    char line8[64];

    if (dc->mode == DISPLAY_MODE_LOWRES) {
        snprintf(line1, sizeof(line1), "MODE: LOW-RES");
        if (dc->refresh_rate > 0) {
            snprintf(line2, sizeof(line2), "%dX%d @ %dHZ", dc->width, dc->height, dc->refresh_rate);
        } else {
            snprintf(line2, sizeof(line2), "%dX%d", dc->width, dc->height);
        }
        snprintf(line3, sizeof(line3), dc->lowres_is_exclusive
                 ? "EXCLUSIVE FULLSCREEN"
                 : "WINDOWED FALLBACK (NO MODE MATCH)");
    } else {
        snprintf(line1, sizeof(line1), "MODE: DESKTOP");
        if (dc->refresh_rate > 0) {
            snprintf(line2, sizeof(line2), "%dX%d @ %dHZ", dc->width, dc->height, dc->refresh_rate);
        } else {
            snprintf(line2, sizeof(line2), "%dX%d", dc->width, dc->height);
        }
        snprintf(line3, sizeof(line3), "FULLSCREEN DESKTOP");
    }
    snprintf(line4, sizeof(line4), "PRESS %s TO TOGGLE", SDL_GetKeyName(cfg->toggle_hotkey));

    switch (lb->status) {
        case LAUNCHBOX_STATUS_LOADED:
            snprintf(line5, sizeof(line5), "LAUNCHBOX: %d PLATFORMS, %d GAMES (%d UNIQUE)",
                     lb->platform_count, lb->version_count, lb->group_count);
            break;
        case LAUNCHBOX_STATUS_DIR_NOT_FOUND:
            snprintf(line5, sizeof(line5), "LAUNCHBOX: DATA DIR NOT FOUND");
            break;
        case LAUNCHBOX_STATUS_NO_PLATFORMS:
            snprintf(line5, sizeof(line5), "LAUNCHBOX: NO PLATFORM XML FOUND");
            break;
        case LAUNCHBOX_STATUS_NOT_CONFIGURED:
        default:
            snprintf(line5, sizeof(line5), "LAUNCHBOX: NOT CONFIGURED");
            break;
    }

    /* The system section (gamelist_system_entry_labels) is always present,
       even with zero LaunchBox games loaded -- calibration shouldn't
       require LaunchBox to be configured first -- so these lines and the
       list below are unconditional now, unlike line5 above. */
    if (gamelist_selected_is_system(gl)) {
        snprintf(line6, sizeof(line6), "SETTINGS");
    } else if (lb->group_count > 0) {
        snprintf(line6, sizeof(line6), "GAME %d/%d",
                 gl->selected_group - GAMELIST_SYSTEM_ENTRY_COUNT + 1, lb->group_count);
    } else {
        snprintf(line6, sizeof(line6), "NO GAMES LOADED");
    }
    snprintf(line7, sizeof(line7), "UP/DN SELECT   L/R LETTER");
    snprintf(line8, sizeof(line8), "ENTER LAUNCH   SHIFT-ENTER VERSIONS");

    int text_scale = compute_text_scale(dc->width, dc->height, cfg->width, cfg->height);
    int text_margin = BASE_TEXT_MARGIN * text_scale;
    int line_h = FONT_GLYPH_HEIGHT * text_scale + BASE_TEXT_LINE_GAP * text_scale;

    int y = text_margin;

    draw_text_with_shadow(renderer, line1, text_margin, y, text_scale); y += line_h;
    draw_text_with_shadow(renderer, line2, text_margin, y, text_scale); y += line_h;
    draw_text_with_shadow(renderer, line3, text_margin, y, text_scale); y += line_h;
    draw_text_with_shadow(renderer, line4, text_margin, y, text_scale); y += line_h;
    draw_text_with_shadow(renderer, line5, text_margin, y, text_scale); y += line_h;
    draw_text_with_shadow(renderer, line6, text_margin, y, text_scale); y += line_h;
    draw_text_with_shadow(renderer, line7, text_margin, y, text_scale); y += line_h;
    draw_text_with_shadow(renderer, line8, text_margin, y, text_scale); y += line_h;

    /* Separator between the status header and the scrollable list below it
       -- same "thin border-colored rect" look as the modal's title
       separator. */
    SDL_Rect header_sep = {text_margin, y + (line_h - text_scale) / 2,
                            dc->width - text_margin * 2, text_scale};
    SDL_SetRenderDrawColor(renderer, COLOR_TEXT.r, COLOR_TEXT.g, COLOR_TEXT.b, 255);
    SDL_RenderFillRect(renderer, &header_sep);
    y += line_h;

    /* Bottom margin matches text_margin (the same edge padding used on
       the left/right/top) so the list stops short of the screen edge
       instead of running flush to it. */
    int visible_rows = (dc->height - y - text_margin) / line_h;
    if (visible_rows < 0) {
        visible_rows = 0;
    }
    gamelist_scroll_into_view(gl, lb, visible_rows);
    draw_game_list(renderer, lb, gl, dc->width, y, line_h, visible_rows, text_scale, text_margin);

    if (gl->selected_version >= 0) {
        const LaunchboxGameGroup *grp = &lb->groups[gl->selected_group - GAMELIST_SYSTEM_ENTRY_COUNT];
        int item_count = grp->version_count;
        if (item_count > MODAL_MAX_ITEMS) {
            item_count = MODAL_MAX_ITEMS; /* no real game has anywhere near this many versions */
        }

        const char *items[MODAL_MAX_ITEMS];
        for (int i = 0; i < item_count; i++) {
            items[i] = lb->versions[grp->version_start + i].label;
        }

        render_draw_modal_list(renderer, dc->width, dc->height, text_scale,
                                grp->title, items, item_count, gl->selected_version);
    } else if (gl->system_modal_open) {
        /* Placeholder body -- no real calibration flow wired up yet, this
           just proves the entry point opens something. */
        static const char *const placeholder_items[] = {"COMING SOON"};
        render_draw_modal_list(renderer, dc->width, dc->height, text_scale,
                                gamelist_system_entry_labels[gl->selected_group],
                                placeholder_items, 1, -1);
    }

    SDL_RenderPresent(renderer);
}

void render_shutdown(RenderContext *rc) {
    if (rc->renderer) {
        SDL_DestroyRenderer(rc->renderer);
        rc->renderer = NULL;
    }
}
