#include "display.h"

static const char *WINDOW_TITLE = "CRT Launcher POC";

/* Searches the given display for a mode that matches width/height exactly
   and, if refresh > 0, matches refresh_rate exactly too. This is stricter
   than SDL_GetClosestDisplayMode() on purpose -- we don't want to silently
   snap to some other resolution and call it "applied". */
static SDL_bool find_exact_mode(int display_index, int w, int h, int refresh, SDL_DisplayMode *out) {
    int count = SDL_GetNumDisplayModes(display_index);
    if (count < 0) {
        SDL_Log("[display] SDL_GetNumDisplayModes failed: %s", SDL_GetError());
        return SDL_FALSE;
    }

    for (int i = 0; i < count; i++) {
        SDL_DisplayMode mode;
        if (SDL_GetDisplayMode(display_index, i, &mode) != 0) {
            continue;
        }
        if (mode.w == w && mode.h == h && (refresh <= 0 || mode.refresh_rate == refresh)) {
            *out = mode;
            return SDL_TRUE;
        }
    }
    return SDL_FALSE;
}

SDL_bool display_init(const AppConfig *cfg, DisplayContext *ctx) {
    SDL_zerop(ctx);
    ctx->cfg = *cfg;

    SDL_Log("[display] Requesting low-res exclusive-fullscreen mode %dx%d@%dHz",
            cfg->width, cfg->height, cfg->refresh_rate);

    SDL_DisplayMode exact_mode;
    SDL_bool have_exact = find_exact_mode(0, cfg->width, cfg->height, cfg->refresh_rate, &exact_mode);

    if (have_exact) {
        SDL_Log("[display] Display reports a matching mode (%dx%d@%dHz) -- attempting exclusive fullscreen",
                exact_mode.w, exact_mode.h, exact_mode.refresh_rate);

        ctx->window = SDL_CreateWindow(WINDOW_TITLE,
                                        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                        exact_mode.w, exact_mode.h,
                                        SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIDDEN);

        if (ctx->window && SDL_SetWindowDisplayMode(ctx->window, &exact_mode) != 0) {
            SDL_Log("[display] WARNING: SDL_SetWindowDisplayMode failed (%s) -- destroying and falling back",
                     SDL_GetError());
            SDL_DestroyWindow(ctx->window);
            ctx->window = NULL;
        }

        if (ctx->window) {
            SDL_ShowWindow(ctx->window);
            ctx->width = exact_mode.w;
            ctx->height = exact_mode.h;
            ctx->refresh_rate = exact_mode.refresh_rate;
            ctx->mode = DISPLAY_MODE_LOWRES;
            ctx->lowres_is_exclusive = SDL_TRUE;
            SDL_Log("[display] APPLIED: exclusive fullscreen low-res mode %dx%d@%dHz",
                    ctx->width, ctx->height, exact_mode.refresh_rate);
            return SDL_TRUE;
        }

        SDL_Log("[display] WARNING: window creation for exclusive mode failed (%s)", SDL_GetError());
    } else {
        SDL_Log("[display] WARNING: no display mode matching %dx%d@%dHz was reported by the OS. "
                "This is expected on a normal desktop GPU during development -- most drivers won't "
                "expose sub-640x480 modes. CRT Emudriver on the real cabinet should expose it.",
                cfg->width, cfg->height, cfg->refresh_rate);
    }

    SDL_Log("[display] FALLBACK: opening windowed at exact target pixel size %dx%d (no stretching)",
            cfg->width, cfg->height);

    ctx->window = SDL_CreateWindow(WINDOW_TITLE,
                                    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    cfg->width, cfg->height,
                                    SDL_WINDOW_SHOWN);
    if (!ctx->window) {
        SDL_Log("[display] FATAL: could not create fallback window: %s", SDL_GetError());
        return SDL_FALSE;
    }

    ctx->width = cfg->width;
    ctx->height = cfg->height;
    ctx->refresh_rate = 0;
    ctx->mode = DISPLAY_MODE_LOWRES;
    ctx->lowres_is_exclusive = SDL_FALSE;
    SDL_Log("[display] APPLIED: windowed fallback %dx%d", ctx->width, ctx->height);
    return SDL_TRUE;
}

static void switch_to_desktop(DisplayContext *ctx) {
    int display_index = SDL_GetWindowDisplayIndex(ctx->window);
    if (display_index < 0) {
        display_index = 0;
    }

    SDL_DisplayMode desktop_mode;
    if (SDL_GetDesktopDisplayMode(display_index, &desktop_mode) != 0) {
        SDL_Log("[display] WARNING: SDL_GetDesktopDisplayMode failed (%s), toggle aborted", SDL_GetError());
        return;
    }

    SDL_Log("[display] Toggling to desktop native resolution %dx%d@%dHz using fullscreen-desktop (borderless)",
            desktop_mode.w, desktop_mode.h, desktop_mode.refresh_rate);

    if (SDL_SetWindowFullscreen(ctx->window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
        SDL_Log("[display] WARNING: SDL_SetWindowFullscreen(FULLSCREEN_DESKTOP) failed: %s", SDL_GetError());
        return;
    }

    ctx->width = desktop_mode.w;
    ctx->height = desktop_mode.h;
    ctx->refresh_rate = desktop_mode.refresh_rate;
    ctx->mode = DISPLAY_MODE_DESKTOP;
    SDL_Log("[display] APPLIED: desktop native mode %dx%d@%dHz (fullscreen-desktop)",
            ctx->width, ctx->height, desktop_mode.refresh_rate);
}

static void switch_to_lowres(DisplayContext *ctx) {
    if (ctx->lowres_is_exclusive) {
        int display_index = SDL_GetWindowDisplayIndex(ctx->window);
        if (display_index < 0) {
            display_index = 0;
        }

        SDL_DisplayMode mode;
        if (!find_exact_mode(display_index, ctx->cfg.width, ctx->cfg.height, ctx->cfg.refresh_rate, &mode)) {
            SDL_Log("[display] WARNING: low-res mode no longer available on toggle back, staying on desktop");
            return;
        }

        SDL_Log("[display] Toggling back to exclusive fullscreen low-res %dx%d@%dHz",
                mode.w, mode.h, mode.refresh_rate);

        SDL_SetWindowFullscreen(ctx->window, 0);
        SDL_SetWindowDisplayMode(ctx->window, &mode);
        if (SDL_SetWindowFullscreen(ctx->window, SDL_WINDOW_FULLSCREEN) != 0) {
            SDL_Log("[display] WARNING: SDL_SetWindowFullscreen(FULLSCREEN) failed: %s", SDL_GetError());
            return;
        }

        ctx->width = mode.w;
        ctx->height = mode.h;
        ctx->refresh_rate = mode.refresh_rate;
    } else {
        SDL_Log("[display] Toggling back to windowed low-res fallback %dx%d", ctx->cfg.width, ctx->cfg.height);

        SDL_SetWindowFullscreen(ctx->window, 0);
        SDL_SetWindowSize(ctx->window, ctx->cfg.width, ctx->cfg.height);
        SDL_SetWindowPosition(ctx->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

        ctx->width = ctx->cfg.width;
        ctx->height = ctx->cfg.height;
        ctx->refresh_rate = 0;
    }

    ctx->mode = DISPLAY_MODE_LOWRES;
    SDL_Log("[display] APPLIED: low-res mode %dx%d", ctx->width, ctx->height);
}

void display_toggle(DisplayContext *ctx) {
    if (ctx->mode == DISPLAY_MODE_LOWRES) {
        switch_to_desktop(ctx);
    } else {
        switch_to_lowres(ctx);
    }
}

void display_shutdown(DisplayContext *ctx) {
    if (ctx->window) {
        SDL_DestroyWindow(ctx->window);
        ctx->window = NULL;
    }
}
