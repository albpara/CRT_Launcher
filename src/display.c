#include "display.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static const char *WINDOW_TITLE = "CRT Launcher POC";

#ifdef _WIN32
/* We set the mode ourselves (the call SDL makes internally) rather than use
   SDL's exclusive fullscreen, which ties it to focus: every focus bounce
   during an emulator's start-up cost two CRT resyncs. */

static SDL_bool query_mode(DWORD which, DEVMODEA *out) {
    SDL_zerop(out);
    out->dmSize = (WORD)sizeof(*out);
    return EnumDisplaySettingsA(NULL, which, out) ? SDL_TRUE : SDL_FALSE;
}

/* Exact match only, never a neighbouring mode. Ties go to the deepest
   colour, then the highest refresh -- the order SDL's mode list had. */
static SDL_bool find_lowres_mode(const AppConfig *cfg, DEVMODEA *out) {
    SDL_bool found = SDL_FALSE;

    for (DWORD i = 0;; i++) {
        DEVMODEA dm;
        if (!query_mode(i, &dm)) {
            break;
        }
        if ((int)dm.dmPelsWidth != cfg->width || (int)dm.dmPelsHeight != cfg->height) {
            continue;
        }
        if (cfg->refresh_rate > 0 && (int)dm.dmDisplayFrequency != cfg->refresh_rate) {
            continue;
        }
        if (!found || dm.dmBitsPerPel > out->dmBitsPerPel ||
            (dm.dmBitsPerPel == out->dmBitsPerPel && dm.dmDisplayFrequency > out->dmDisplayFrequency)) {
            *out = dm;
            found = SDL_TRUE;
        }
    }

    if (found) {
        out->dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_DISPLAYFLAGS;
    }
    return found;
}

static SDL_bool same_mode(const DEVMODEA *a, const DEVMODEA *b) {
    return (a->dmPelsWidth == b->dmPelsWidth && a->dmPelsHeight == b->dmPelsHeight &&
            a->dmBitsPerPel == b->dmBitsPerPel && a->dmDisplayFrequency == b->dmDisplayFrequency &&
            a->dmDisplayFlags == b->dmDisplayFlags) ? SDL_TRUE : SDL_FALSE;
}

/* `target`, or the registry's desktop mode when NULL. A no-op if already
   up: every real change is a CRT resync. CDS_FULLSCREEN = never saved. */
static SDL_bool set_display_mode(DEVMODEA *target) {
    DEVMODEA want;
    DEVMODEA current;
    SDL_bool want_known = SDL_TRUE;

    if (target) {
        want = *target;
    } else {
        want_known = query_mode(ENUM_REGISTRY_SETTINGS, &want);
    }
    if (want_known && query_mode(ENUM_CURRENT_SETTINGS, &current) && same_mode(&current, &want)) {
        SDL_Log("[display] Display already at %lux%lu@%luHz, no mode change",
                (unsigned long)current.dmPelsWidth, (unsigned long)current.dmPelsHeight,
                (unsigned long)current.dmDisplayFrequency);
        return SDL_TRUE;
    }

    LONG status = target ? ChangeDisplaySettingsExA(NULL, target, NULL, CDS_FULLSCREEN, NULL)
                         : ChangeDisplaySettingsExA(NULL, NULL, NULL, 0, NULL);
    if (status != DISP_CHANGE_SUCCESSFUL) {
        SDL_Log("[display] WARNING: ChangeDisplaySettingsEx failed (status %ld)", (long)status);
        return SDL_FALSE;
    }

    if (query_mode(ENUM_CURRENT_SETTINGS, &current)) {
        SDL_Log("[display] Display mode set to %lux%lu@%luHz",
                (unsigned long)current.dmPelsWidth, (unsigned long)current.dmPelsHeight,
                (unsigned long)current.dmDisplayFrequency);
    }
    return SDL_TRUE;
}

static SDL_bool lowres_mode_exists(const AppConfig *cfg) {
    DEVMODEA dm;
    return find_lowres_mode(cfg, &dm);
}

static SDL_bool set_lowres_mode(const AppConfig *cfg) {
    DEVMODEA dm;
    return (find_lowres_mode(cfg, &dm) && set_display_mode(&dm)) ? SDL_TRUE : SDL_FALSE;
}

static void set_desktop_mode(void) {
    set_display_mode(NULL);
}
#else
/* No mode switching elsewhere: always the windowed fallback. */
static SDL_bool lowres_mode_exists(const AppConfig *cfg) {
    (void)cfg;
    return SDL_FALSE;
}

static SDL_bool set_lowres_mode(const AppConfig *cfg) {
    (void)cfg;
    return SDL_FALSE;
}

static void set_desktop_mode(void) {
}
#endif

/* Stretches the borderless window over `bounds`. It only ever grows: left
   as large as the biggest mode seen, it already covers the screen when a
   mode change lands, so the desktop never shows in the gap before we could
   react. SDL_TRUE if it had to grow. */
static SDL_bool cover_bounds(DisplayContext *ctx, const SDL_Rect *bounds) {
    int w, h;
    SDL_GetWindowSize(ctx->window, &w, &h);
    SDL_SetWindowPosition(ctx->window, bounds->x, bounds->y);
    if (bounds->w <= w && bounds->h <= h) {
        return SDL_FALSE;
    }
    SDL_SetWindowSize(ctx->window, SDL_max(w, bounds->w), SDL_max(h, bounds->h));
    return SDL_TRUE;
}

/* Borderless over the whole display, or the fallback's plain low-res-sized
   window. A real mode is always on the primary display, SDL's index 0. */
static void place_window(DisplayContext *ctx, SDL_bool cover_display) {
    if (!cover_display) {
        SDL_SetWindowBordered(ctx->window, SDL_TRUE);
        SDL_SetWindowSize(ctx->window, ctx->cfg.width, ctx->cfg.height);
        SDL_SetWindowPosition(ctx->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        ctx->width = ctx->cfg.width;
        ctx->height = ctx->cfg.height;
        return;
    }

    int display_index = ctx->lowres_is_real ? 0 : SDL_GetWindowDisplayIndex(ctx->window);
    if (display_index < 0) {
        display_index = 0;
    }

    /* Let SDL notice a mode change of ours before asking it for bounds. */
    SDL_PumpEvents();

    SDL_Rect bounds;
    if (SDL_GetDisplayBounds(display_index, &bounds) != 0) {
        SDL_Log("[display] WARNING: SDL_GetDisplayBounds failed (%s), window left as is", SDL_GetError());
        return;
    }

    SDL_SetWindowBordered(ctx->window, SDL_FALSE);
    cover_bounds(ctx, &bounds);
    ctx->width = bounds.w;
    ctx->height = bounds.h;
}

/* Every mode change after start-up: the display first, then the window to
   match. Leaves everything alone if the low-res mode can't be set. */
static void enter_mode(DisplayContext *ctx, DisplayMode mode) {
    if (ctx->lowres_is_real) {
        if (mode == DISPLAY_MODE_DESKTOP) {
            set_desktop_mode();
        } else if (!set_lowres_mode(&ctx->cfg)) {
            SDL_Log("[display] WARNING: low-res mode no longer available, display left as is");
            return;
        }
    }

    place_window(ctx, (ctx->lowres_is_real || mode == DISPLAY_MODE_DESKTOP) ? SDL_TRUE : SDL_FALSE);
    ctx->mode = mode;
    SDL_Log("[display] APPLIED: %s mode %dx%d",
            (mode == DISPLAY_MODE_LOWRES) ? "low-res" : "desktop", ctx->width, ctx->height);
}

SDL_bool display_init(const AppConfig *cfg, DisplayContext *ctx) {
    SDL_zerop(ctx);
    ctx->cfg = *cfg;

    /* The game closing normally hands focus straight back; this covers the
       case where Windows refuses a plain SetForegroundWindow. */
    SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");

    SDL_Log("[display] Requesting low-res mode %dx%d@%dHz", cfg->width, cfg->height, cfg->refresh_rate);

    ctx->lowres_is_real = lowres_mode_exists(cfg);
    if (!ctx->lowres_is_real) {
        SDL_Log("[display] WARNING: no display mode matching %dx%d@%dHz -- expected on a desktop "
                "GPU; CRT Emudriver on the cabinet should expose it",
                cfg->width, cfg->height, cfg->refresh_rate);
    }

    /* Hidden until placed, so it never shows in the old mode. Created
       before the mode change so SDL is there to see it happen. */
    ctx->window = SDL_CreateWindow(WINDOW_TITLE,
                                    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    cfg->width, cfg->height,
                                    SDL_WINDOW_HIDDEN);
    if (!ctx->window) {
        SDL_Log("[display] FATAL: could not create window: %s", SDL_GetError());
        return SDL_FALSE;
    }

    if (ctx->lowres_is_real) {
        /* Sized over the desktop mode before leaving it, so a launch that
           hands that mode back finds the screen already covered. */
        place_window(ctx, SDL_TRUE);
        if (!set_lowres_mode(cfg)) {
            SDL_Log("[display] WARNING: could not set the low-res mode -- falling back");
            ctx->lowres_is_real = SDL_FALSE;
        }
    }

    place_window(ctx, ctx->lowres_is_real);
    ctx->mode = DISPLAY_MODE_LOWRES;
    ctx->released_from = DISPLAY_MODE_LOWRES;
    SDL_ShowWindow(ctx->window);

    SDL_Log("[display] APPLIED: %s %dx%d",
            ctx->lowres_is_real ? "borderless low-res mode" : "windowed fallback",
            ctx->width, ctx->height);
    return SDL_TRUE;
}

void display_toggle(DisplayContext *ctx) {
    enter_mode(ctx, (ctx->mode == DISPLAY_MODE_LOWRES) ? DISPLAY_MODE_DESKTOP : DISPLAY_MODE_LOWRES);
}

void display_release(DisplayContext *ctx) {
    ctx->released_from = ctx->mode;
    /* The windowed fallback owns no mode, so there's nothing to hand over. */
    if (ctx->lowres_is_real && ctx->mode == DISPLAY_MODE_LOWRES) {
        enter_mode(ctx, DISPLAY_MODE_DESKTOP);
    }
}

SDL_bool display_follow(DisplayContext *ctx) {
    SDL_Rect bounds;
    int w, h;
    SDL_GetWindowSize(ctx->window, &w, &h);
    if (!ctx->lowres_is_real || SDL_GetDisplayBounds(0, &bounds) != 0 ||
        (bounds.w <= w && bounds.h <= h)) {
        return SDL_FALSE;
    }
    cover_bounds(ctx, &bounds);
    SDL_Log("[display] Game set a %dx%d mode, window grown to cover it", bounds.w, bounds.h);
    return SDL_TRUE;
}

void display_reclaim(DisplayContext *ctx) {
    if (ctx->lowres_is_real) {
        /* Even if we never left it: the game may have set a mode of its own. */
        enter_mode(ctx, ctx->released_from);
    }
    SDL_RaiseWindow(ctx->window);
}

void display_shutdown(DisplayContext *ctx) {
    if (ctx->window) {
        SDL_DestroyWindow(ctx->window);
        ctx->window = NULL;
    }
    if (ctx->lowres_is_real) {
        set_desktop_mode();
    }
}
