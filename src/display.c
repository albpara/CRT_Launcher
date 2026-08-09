#include "display.h"

static const char *WINDOW_TITLE = "CRT Launcher POC";

/* Exact-match search only -- deliberately stricter than
   SDL_GetClosestDisplayMode, which could silently snap elsewhere. */
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
        ctx->window = SDL_CreateWindow(WINDOW_TITLE,
                                        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                        exact_mode.w, exact_mode.h,
                                        SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIDDEN);

        if (ctx->window && SDL_SetWindowDisplayMode(ctx->window, &exact_mode) != 0) {
            SDL_Log("[display] WARNING: SDL_SetWindowDisplayMode failed (%s) -- falling back",
                     SDL_GetError());
            SDL_DestroyWindow(ctx->window);
            ctx->window = NULL;
        }

        if (ctx->window) {
            SDL_ShowWindow(ctx->window);
            ctx->width = exact_mode.w;
            ctx->height = exact_mode.h;
            ctx->mode = DISPLAY_MODE_LOWRES;
            ctx->lowres_is_exclusive = SDL_TRUE;
            SDL_Log("[display] APPLIED: exclusive fullscreen low-res mode %dx%d@%dHz",
                    ctx->width, ctx->height, exact_mode.refresh_rate);
            return SDL_TRUE;
        }

        SDL_Log("[display] WARNING: window creation for exclusive mode failed (%s)", SDL_GetError());
    } else {
        SDL_Log("[display] WARNING: no display mode matching %dx%d@%dHz -- expected on a desktop "
                "GPU; CRT Emudriver on the cabinet should expose it",
                cfg->width, cfg->height, cfg->refresh_rate);
    }

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

    if (SDL_SetWindowFullscreen(ctx->window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
        SDL_Log("[display] WARNING: SDL_SetWindowFullscreen(FULLSCREEN_DESKTOP) failed: %s", SDL_GetError());
        return;
    }

    ctx->width = desktop_mode.w;
    ctx->height = desktop_mode.h;
    ctx->mode = DISPLAY_MODE_DESKTOP;
    SDL_Log("[display] APPLIED: desktop native mode %dx%d@%dHz",
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
            SDL_Log("[display] WARNING: low-res mode no longer available, staying on desktop");
            return;
        }

        SDL_SetWindowFullscreen(ctx->window, 0);
        SDL_SetWindowDisplayMode(ctx->window, &mode);
        if (SDL_SetWindowFullscreen(ctx->window, SDL_WINDOW_FULLSCREEN) != 0) {
            SDL_Log("[display] WARNING: SDL_SetWindowFullscreen(FULLSCREEN) failed: %s", SDL_GetError());
            return;
        }

        ctx->width = mode.w;
        ctx->height = mode.h;
    } else {
        SDL_SetWindowFullscreen(ctx->window, 0);
        SDL_SetWindowSize(ctx->window, ctx->cfg.width, ctx->cfg.height);
        SDL_SetWindowPosition(ctx->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

        ctx->width = ctx->cfg.width;
        ctx->height = ctx->cfg.height;
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
