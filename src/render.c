#include "render.h"

#include <stdio.h>
#include <string.h>

#include "font_data.h"
#include "startup.h"

/* Base sizes correspond to the configured low-res mode (scale 1); bigger
   windows get the largest integer multiple that fits, keeping glyphs
   pixel-exact. */
#define BASE_TEXT_SCALE 1
#define BASE_TEXT_MARGIN 8
#define BASE_MODAL_PADDING 4
#define BASE_TEXT_LINE_GAP 2
#define GRID_CELL_SIZE 16
/* Safety cap for the version-picker item array (most seen in real data: 9). */
#define MODAL_MAX_ITEMS 128
/* Min width of the system modal so it doesn't resize between calibration
   prompts. Deliberately not configurable. */
#define SYSTEM_MODAL_WIDTH_CHARS 42
/* Marquee cycle for an over-wide selected row (see marquee_offset). Speed
   is per text_scale unit so it tracks the glyph size. */
#define MARQUEE_SPEED_PX_PER_S 40
#define MARQUEE_START_HOLD_MS 200
#define MARQUEE_END_HOLD_MS 1000
#define MARQUEE_REPEAT_HOLD_MS 2000

static const SDL_Color COLOR_GRID_A = {32, 32, 40, 255};
static const SDL_Color COLOR_GRID_B = {70, 70, 90, 255};
static const SDL_Color COLOR_TEXT = {80, 255, 120, 255};
static const SDL_Color COLOR_FAVORITE = {255, 220, 40, 255};
static const SDL_Color COLOR_SYSTEM = {140, 140, 150, 255};
static const SDL_Color COLOR_PLATFORM = {90, 170, 255, 255};
static const SDL_Color COLOR_EXIT = {255, 70, 70, 255};
static const SDL_Color COLOR_TEXT_SHADOW = {0, 0, 0, 255};
static const SDL_Color COLOR_SELECT_TEXT = {10, 10, 15, 255};
static const SDL_Color COLOR_MODAL_DIM = {0, 0, 0, 180};
static const SDL_Color COLOR_MODAL_BG = {16, 16, 22, 255};

static const char *const APP_TITLE = "CRT LAUNCHER";

SDL_bool render_init(SDL_Window *window, const AppConfig *cfg, RenderContext *rc) {
    rc->font = (cfg->font == FONT_STYLE_GALAGA88) ? &FONT_GALAGA88 : &FONT_COMPACT;
    rc->marquee_row = -1;
    rc->marquee_since = 0;

    /* Nearest-neighbor; must be set before SDL_CreateRenderer. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    rc->renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!rc->renderer) {
        SDL_Log("[render] SDL_RENDERER_ACCELERATED failed (%s), retrying without flags", SDL_GetError());
        rc->renderer = SDL_CreateRenderer(window, -1, 0);
    }

    if (!rc->renderer) {
        SDL_Log("[render] FATAL: could not create renderer: %s", SDL_GetError());
        return SDL_FALSE;
    }

    SDL_Log("[render] Renderer created with nearest-neighbor scaling");
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

/* One character cell, including the 1px inter-glyph gap. */
static int glyph_advance(const BitmapFont *font, int scale) {
    return (font->width + 1) * scale;
}

static int text_line_height(const BitmapFont *font, int scale) {
    return (font->height + BASE_TEXT_LINE_GAP) * scale;
}

/* Ink width of `text`. One cell per character, minus the trailing
   inter-glyph gap the last one doesn't use -- counting it makes every
   string look a pixel wider than it draws. */
static int text_width(const BitmapFont *font, const char *text, int scale) {
    size_t n = strlen(text);
    return n ? (int)n * glyph_advance(font, scale) - scale : 0;
}

/* Glyphs drawn as filled rects -- see font_data.h. */
static void draw_text(SDL_Renderer *renderer, const BitmapFont *font, const char *text,
                       int x, int y, int scale, SDL_Color color) {
    int cursor_x = x;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    for (const char *p = text; *p; p++) {
        const uint8_t *glyph = font_glyph(font, *p);

        for (int row = 0; row < font->height; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < font->width; col++) {
                int bit = (bits >> (font->width - 1 - col)) & 1;
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

        cursor_x += glyph_advance(font, scale);
    }
}

static void draw_text_with_shadow(SDL_Renderer *renderer, const BitmapFont *font, const char *text,
                                   int x, int y, int scale, SDL_Color color) {
    /* Shadow offset scales with the glyphs so it stays visible. */
    draw_text(renderer, font, text, x + scale, y + scale, scale, COLOR_TEXT_SHADOW);
    draw_text(renderer, font, text, x, y, scale, color);
}

/* Largest integer scale that fits the configured low-res reference size
   into the window; at least BASE_TEXT_SCALE. */
static int compute_text_scale(int win_w, int win_h, int base_w, int base_h) {
    if (base_w <= 0 || base_h <= 0) {
        return BASE_TEXT_SCALE;
    }

    int scale_w = win_w / base_w;
    int scale_h = win_h / base_h;
    int scale = (scale_w < scale_h) ? scale_w : scale_h;

    return (scale < BASE_TEXT_SCALE) ? BASE_TEXT_SCALE : scale;
}

/* One row: plain shadowed text, or a highlight bar over [bar_x,
   bar_x+bar_w) when selected. `text_color` doubles as the bar fill so
   favorites highlight yellow, system rows gray, etc. `clip_w` > 0 confines
   the text to [text_x, text_x+clip_w) so a `scroll_px` marquee offset
   can't spill into the margins. */
static void draw_row(SDL_Renderer *renderer, const BitmapFont *font, const char *text,
                      int text_x, int bar_x, int bar_w,
                      int y, int line_h, int text_scale, SDL_bool selected, SDL_Color text_color,
                      int clip_w, int scroll_px) {
    int text_y_offset = (line_h - font->height * text_scale) / 2;
    int text_y = y + text_y_offset;
    int x = text_x - scroll_px;

    if (selected) {
        SDL_Rect bar = {bar_x, y, bar_w, line_h};
        SDL_SetRenderDrawColor(renderer, text_color.r, text_color.g, text_color.b, 255);
        SDL_RenderFillRect(renderer, &bar);
    }

    SDL_Rect clip = {text_x, y, clip_w, line_h};
    if (clip_w > 0) {
        SDL_RenderSetClipRect(renderer, &clip);
    }

    if (selected) {
        draw_text(renderer, font, text, x, text_y, text_scale, COLOR_SELECT_TEXT);
    } else {
        draw_text(renderer, font, text, x + text_scale, text_y + text_scale, text_scale, COLOR_TEXT_SHADOW);
        draw_text(renderer, font, text, x, text_y, text_scale, text_color);
    }

    if (clip_w > 0) {
        SDL_RenderSetClipRect(renderer, NULL);
    }
}

/* Marquee position `elapsed_ms` after the row became selected, scrolling
   `scroll_px` in total. One cycle: hold at the start, scroll to the end,
   hold, scroll back, then a longer hold before repeating. */
static int marquee_offset(Uint32 elapsed_ms, int scroll_px, int text_scale) {
    if (scroll_px <= 0) {
        return 0;
    }

    int travel_ms = scroll_px * 1000 / (MARQUEE_SPEED_PX_PER_S * text_scale);
    if (travel_ms < 1) {
        travel_ms = 1;
    }

    int cycle = MARQUEE_START_HOLD_MS + travel_ms + MARQUEE_END_HOLD_MS + travel_ms + MARQUEE_REPEAT_HOLD_MS;
    int t = (int)(elapsed_ms % (Uint32)cycle);

    if (t < MARQUEE_START_HOLD_MS) {
        return 0;
    }
    t -= MARQUEE_START_HOLD_MS;
    if (t < travel_ms) {
        return scroll_px * t / travel_ms;
    }
    t -= travel_ms;
    if (t < MARQUEE_END_HOLD_MS) {
        return scroll_px;
    }
    t -= MARQUEE_END_HOLD_MS;
    if (t < travel_ms) {
        return scroll_px - scroll_px * t / travel_ms;
    }
    return 0;  /* parked back at the start for the repeat hold */
}

/* The unified scrollable list: system rows (startup row's label reflects
   live registry state), platform checkboxes ("X " = enabled), then the
   filtered game rows (trailing " >" = multiple versions; favorites
   yellow, sorted first). Shows a "NO GAMES" placeholder when the filter
   leaves nothing. Every row is clipped to the text column; the selected
   one marquees when it doesn't fit. */
static void draw_game_list(const RenderContext *rc, const LaunchboxInfo *lb, const GameListState *gl,
                            int win_w, int list_y, int line_h, int visible_rows,
                            int text_scale, int text_margin) {
    SDL_Renderer *renderer = rc->renderer;
    const BitmapFont *font = rc->font;
    int advance = glyph_advance(font, text_scale);
    int avail_w = win_w - text_margin * 2;

    int y = list_y;
    int rows_drawn = 0;
    int g = gl->scroll_offset;

    int platform_start = GAMELIST_SYSTEM_ENTRY_COUNT;
    int platform_end = GAMELIST_SYSTEM_ENTRY_COUNT + lb->platform_count;
    int game_start = platform_end;
    int game_end = game_start + gl->visible_group_count;

    SDL_bool show_no_games_row = (gl->visible_group_count == 0);
    int total_rows = show_no_games_row ? game_end + 1 : game_end;

    while (rows_drawn < visible_rows && g < total_rows) {
        char label[LAUNCHBOX_TITLE_MAX + 16];
        SDL_Color color;
        SDL_bool selected = (g == gl->selected_group);

        if (g < platform_start) {
            if (g == GAMELIST_SYSTEM_ENTRY_STARTUP) {
                snprintf(label, sizeof(label), "%s",
                         startup_is_enabled() ? "REMOVE FROM STARTUP" : "ADD TO STARTUP");
            } else {
                snprintf(label, sizeof(label), "%s", gamelist_system_entry_labels[g]);
            }
            color = COLOR_SYSTEM;
        } else if (g < platform_end) {
            int p = g - platform_start;
            SDL_bool checked = gl->platform_selected && gl->platform_selected[p];
            snprintf(label, sizeof(label), "%s%s", checked ? "X " : "  ", lb->platform_names[p]);
            color = COLOR_PLATFORM;
        } else if (g < game_end) {
            const LaunchboxGameGroup *grp = &lb->groups[gl->visible_group_indices[g - game_start]];
            if (grp->version_count > 1) {
                snprintf(label, sizeof(label), "%s >", grp->title);
            } else {
                snprintf(label, sizeof(label), "%s", grp->title);
            }
            color = grp->is_favorite ? COLOR_FAVORITE : COLOR_TEXT;
        } else {
            /* The synthetic non-selectable "NO GAMES" row. */
            snprintf(label, sizeof(label), "NO GAMES");
            color = COLOR_TEXT;
            selected = SDL_FALSE;
        }

        int overflow = text_width(font, label, text_scale) - avail_w;
        int offset = 0;
        /* Below half a glyph there's nothing worth reading in the hidden
           sliver -- scrolling for it just makes the row twitch. */
        if (selected && overflow > font->width * text_scale / 2) {
            /* Round the travel up to a whole cell so the end of the scroll
               parks on a character boundary instead of mid-glyph. */
            int scroll = (overflow + advance - 1) / advance * advance;
            offset = marquee_offset(SDL_GetTicks() - rc->marquee_since, scroll, text_scale);
        }
        draw_row(renderer, font, label, text_margin, 0, win_w, y, line_h,
                 text_scale, selected, color, avail_w, offset);

        y += line_h;
        rows_drawn++;
        g++;
    }
}

/* Generic modal: dim overlay + centered bordered box of plain strings,
   one optionally highlighted. Sized to the longest string or
   `min_width_chars`, clamped to the window. `accent_color` themes the
   border/title/text and the selected-row bar. */
void render_draw_modal_list(const RenderContext *rc, int win_w, int win_h, int text_scale,
                             const char *title, const char *const *items, int item_count,
                             int selected_index, int min_width_chars, SDL_Color accent_color) {
    SDL_Renderer *renderer = rc->renderer;
    const BitmapFont *font = rc->font;
    int line_h = text_line_height(font, text_scale);
    int padding = BASE_MODAL_PADDING * text_scale;
    int advance = glyph_advance(font, text_scale);

    int max_chars = title ? (int)strlen(title) : 0;
    for (int i = 0; i < item_count; i++) {
        int len = (int)strlen(items[i]);
        if (len > max_chars) {
            max_chars = len;
        }
    }
    if (min_width_chars > max_chars) {
        max_chars = min_width_chars;
    }

    SDL_bool has_separator = (title != NULL) && (item_count > 0);

    int box_w = max_chars * advance + padding * 2;
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
    SDL_SetRenderDrawColor(renderer, accent_color.r, accent_color.g, accent_color.b, 255);
    SDL_RenderFillRect(renderer, &outer);

    SDL_Rect inner = {box_x, box_y, box_w, box_h};
    SDL_SetRenderDrawColor(renderer, COLOR_MODAL_BG.r, COLOR_MODAL_BG.g, COLOR_MODAL_BG.b, 255);
    SDL_RenderFillRect(renderer, &inner);

    /* The box may have been clamped narrower than its content; without
       clipping, over-long text would draw straight through the border. */
    int content_w = box_w - padding * 2;

    int y = box_y + padding;
    if (title) {
        SDL_Rect title_clip = {box_x + padding, y, content_w, line_h};
        SDL_RenderSetClipRect(renderer, &title_clip);
        draw_text_with_shadow(renderer, font, title, box_x + padding, y, text_scale, accent_color);
        SDL_RenderSetClipRect(renderer, NULL);
        y += line_h;
    }

    if (has_separator) {
        SDL_Rect sep = {box_x + padding, y + (line_h - border) / 2, box_w - padding * 2, border};
        SDL_SetRenderDrawColor(renderer, accent_color.r, accent_color.g, accent_color.b, 255);
        SDL_RenderFillRect(renderer, &sep);
        y += line_h;
    }

    for (int i = 0; i < item_count; i++) {
        draw_row(renderer, font, items[i], box_x + padding, box_x, box_w, y, line_h,
                 text_scale, i == selected_index, accent_color, content_w, 0);
        y += line_h;
    }
}

void render_starfield_frame(RenderContext *rc, const DisplayContext *dc, Starfield *sf) {
    SDL_Renderer *renderer = rc->renderer;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    starfield_draw(sf, renderer, dc->width, dc->height);
    SDL_RenderPresent(renderer);
}

void render_frame(RenderContext *rc, const DisplayContext *dc, const AppConfig *cfg,
                   const LaunchboxInfo *lb, GameListState *gl, Starfield *sf) {
    SDL_Renderer *renderer = rc->renderer;
    const BitmapFont *font = rc->font;

    /* Restart the marquee whenever the selection moves, so a long title
       always starts scrolling from its beginning. */
    if (gl->selected_group != rc->marquee_row) {
        rc->marquee_row = gl->selected_group;
        rc->marquee_since = SDL_GetTicks();
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    if (cfg->background == BACKGROUND_CHECKERBOARD) {
        draw_checkerboard(renderer, dc->width, dc->height);
    } else {
        starfield_draw(sf, renderer, dc->width, dc->height);
    }

    char game_count_line[64];

    /* "OF" instead of '/' -- the font has no slash glyph. Counts reflect
       the filtered view, not the full database. */
    if (gamelist_selected_is_system(gl)) {
        snprintf(game_count_line, sizeof(game_count_line), "SETTINGS");
    } else if (gamelist_selected_is_platform(gl, lb)) {
        snprintf(game_count_line, sizeof(game_count_line), "PLATFORMS");
    } else if (gl->visible_group_count > 0) {
        int game_rows_start = GAMELIST_SYSTEM_ENTRY_COUNT + lb->platform_count;
        snprintf(game_count_line, sizeof(game_count_line), "GAME %d OF %d",
                 gl->selected_group - game_rows_start + 1, gl->visible_group_count);
    } else {
        snprintf(game_count_line, sizeof(game_count_line), "NO GAMES LOADED");
    }

    int text_scale = compute_text_scale(dc->width, dc->height, cfg->width, cfg->height);
    int text_margin = BASE_TEXT_MARGIN * text_scale;
    int line_h = text_line_height(font, text_scale);

    int y = text_margin;

    /* Centered app title at 2x scale. */
    int title_scale = text_scale * 2;
    int title_line_h = text_line_height(font, title_scale);
    int title_w = text_width(font, APP_TITLE, title_scale);
    int title_x = (dc->width - title_w) / 2;
    draw_text_with_shadow(renderer, font, APP_TITLE, title_x, y, title_scale, COLOR_TEXT);
    y += title_line_h;

    /* Right-aligned game counter. */
    int game_count_w = text_width(font, game_count_line, text_scale);
    int game_count_x = dc->width - text_margin - game_count_w;
    draw_text_with_shadow(renderer, font, game_count_line, game_count_x, y, text_scale, COLOR_TEXT); y += line_h;

    /* Separator between header and list. */
    SDL_Rect header_sep = {text_margin, y + (line_h - text_scale) / 2,
                            dc->width - text_margin * 2, text_scale};
    SDL_SetRenderDrawColor(renderer, COLOR_TEXT.r, COLOR_TEXT.g, COLOR_TEXT.b, 255);
    SDL_RenderFillRect(renderer, &header_sep);
    y += line_h;

    int visible_rows = (dc->height - y - text_margin) / line_h;
    if (visible_rows < 0) {
        visible_rows = 0;
    }
    gamelist_scroll_into_view(gl, lb, visible_rows);
    draw_game_list(rc, lb, gl, dc->width, y, line_h, visible_rows, text_scale, text_margin);

    const LaunchboxGameGroup *selected_grp = (gl->selected_version >= 0) ? gamelist_selected_group(gl, lb) : NULL;
    if (selected_grp) {
        int item_count = selected_grp->version_count;
        if (item_count > MODAL_MAX_ITEMS) {
            item_count = MODAL_MAX_ITEMS;
        }

        const char *items[MODAL_MAX_ITEMS];
        for (int i = 0; i < item_count; i++) {
            items[i] = lb->versions[selected_grp->version_start + i].label;
        }

        /* Accent matches the row that opened it (yellow for favorites). */
        SDL_Color accent = selected_grp->is_favorite ? COLOR_FAVORITE : COLOR_TEXT;
        render_draw_modal_list(rc, dc->width, dc->height, text_scale,
                                selected_grp->title, items, item_count, gl->selected_version, 0, accent);
    } else if (gl->system_modal_open) {
        /* Calibration prompts/completion text, owned by main.c. Fixed min
           width so the box doesn't resize between steps. */
        const char *items[2];
        int item_count = 0;
        items[item_count++] = gl->system_modal_status;
        if (gl->system_modal_hint[0]) {
            items[item_count++] = gl->system_modal_hint;
        }
        render_draw_modal_list(rc, dc->width, dc->height, text_scale,
                                gamelist_system_entry_labels[gl->selected_group],
                                items, item_count, -1, SYSTEM_MODAL_WIDTH_CHARS, COLOR_SYSTEM);
    } else if (gl->exit_confirm_open) {
        /* Generic action names (bindings vary); no '?' -- font lacks it. */
        static const char *const items[] = {"PRESS SELECT TO CONFIRM", "PRESS BACK TO GO BACK"};
        render_draw_modal_list(rc, dc->width, dc->height, text_scale,
                                "EXIT", items, 2, -1, 0, COLOR_EXIT);
    }

    SDL_RenderPresent(renderer);
}

void render_shutdown(RenderContext *rc) {
    if (rc->renderer) {
        SDL_DestroyRenderer(rc->renderer);
        rc->renderer = NULL;
    }
}
