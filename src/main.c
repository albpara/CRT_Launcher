#include <SDL.h>

#include "config.h"
#include "display.h"
#include "gamelist.h"
#include "launchbox.h"
#include "launcher.h"
#include "render.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define CONFIG_PATH "config.ini"

/* Hides the console window Windows opens for this console-subsystem exe.
   Best-effort only -- the OS creates that window before main() ever runs,
   so there's an unavoidable brief flash before this can hide it. For a
   build with no console at all (no flash, ever), build with
   -DCRT_LAUNCHER_NO_CONSOLE=ON instead (see CMakeLists.txt) -- that
   produces a WINDOWS-subsystem exe SDL_Log has nowhere to write to, so
   this function becomes a permanent no-op there (GetConsoleWindow()
   returns NULL) instead of doing anything at runtime. No-op on non-Windows
   too. */
static void hide_console_window(void) {
#ifdef _WIN32
    HWND console = GetConsoleWindow();
    if (console) {
        ShowWindow(console, SW_HIDE);
    }
#endif
}

/* Tracks one direction key's own held/repeat timing, independent of the
   OS's keyboard repeat rate (SDL_KEYDOWN's event.key.repeat just mirrors
   whatever Windows' Control Panel repeat setting is, which isn't
   configurable from here). Polled once per frame against
   SDL_GetKeyboardState instead. */
typedef struct {
    SDL_bool held;
    Uint32 next_repeat_time;
} KeyRepeatState;

/* Returns SDL_TRUE the frame(s) this direction should act: once
   immediately on first press, then every `interval_ms` after having been
   held for `delay_ms`. */
static SDL_bool key_repeat_tick(KeyRepeatState *state, SDL_bool is_down, Uint32 now,
                                 int delay_ms, int interval_ms) {
    if (!is_down) {
        state->held = SDL_FALSE;
        return SDL_FALSE;
    }
    if (!state->held) {
        state->held = SDL_TRUE;
        state->next_repeat_time = now + (Uint32)delay_ms;
        return SDL_TRUE;
    }
    if (now >= state->next_repeat_time) {
        state->next_repeat_time = now + (Uint32)interval_ms;
        return SDL_TRUE;
    }
    return SDL_FALSE;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("[main] FATAL: SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    AppConfig cfg;
    config_load(CONFIG_PATH, &cfg);

    if (!cfg.show_console) {
        hide_console_window();
    }

    /* Hidden whenever it's over one of this app's windows -- SDL restores
       the normal OS cursor automatically once it leaves. There's no on-
       screen pointer UI to click here, so it's just visual noise on the
       cabinet. */
    SDL_ShowCursor(SDL_DISABLE);

    LaunchboxInfo launchbox;
    launchbox_load(cfg.launchbox_dir, &launchbox);

    LauncherDatabase launcher;
    launcher_load(cfg.launchbox_dir, &launcher);

    GameListState gamelist;
    gamelist_init(&gamelist, &launchbox);

    DisplayContext display;
    if (!display_init(&cfg, &display)) {
        SDL_Quit();
        return 1;
    }

    RenderContext render;
    if (!render_init(display.window, &render)) {
        display_shutdown(&display);
        SDL_Quit();
        return 1;
    }

    SDL_Log("[main] Entering main loop. %s toggles resolution, UP/DOWN select a game, "
            "LEFT/RIGHT jump a letter, SHIFT+ENTER opens/closes the version picker, "
            "ENTER launches the selected game, ESC closes the version picker or quits, "
            "close the window to quit.",
            SDL_GetKeyName(cfg.toggle_hotkey));

    KeyRepeatState nav_up = {0};
    KeyRepeatState nav_down = {0};
    KeyRepeatState nav_left = {0};
    KeyRepeatState nav_right = {0};

    SDL_bool running = SDL_TRUE;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = SDL_FALSE;
            } else if (event.type == SDL_KEYDOWN) {
                SDL_Keycode key = event.key.keysym.sym;

                /* Up/Down/Left/Right are handled below via our own
                   per-frame repeat timer instead of SDL_KEYDOWN's
                   event.key.repeat, which just mirrors the OS's keyboard
                   repeat rate -- not tunable from config.ini. */
                if (!event.key.repeat) {
                    if (key == cfg.toggle_hotkey) {
                        display_toggle(&display);
                    } else if (key == SDLK_ESCAPE) {
                        if (gamelist.selected_version >= 0) {
                            gamelist_toggle_expand(&gamelist, &launchbox); /* close the version modal first */
                        } else if (gamelist.system_modal_open) {
                            gamelist.system_modal_open = SDL_FALSE;
                        } else {
                            running = SDL_FALSE;
                        }
                    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                        if (gamelist_selected_is_system(&gamelist)) {
                            /* Placeholder -- no real calibration flow wired
                               up yet, this just proves the entry point
                               opens something (see render.c). */
                            SDL_Log("[main] Opening system menu entry '%s' (placeholder, not implemented yet)",
                                    gamelist_system_entry_labels[gamelist.selected_group]);
                            gamelist.system_modal_open = SDL_TRUE;
                        } else if (launchbox.group_count > 0) {
                            SDL_bool shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;
                            if (shift) {
                                gamelist_toggle_expand(&gamelist, &launchbox);
                            } else {
                                const LaunchboxGameGroup *grp =
                                    &launchbox.groups[gamelist.selected_group - GAMELIST_SYSTEM_ENTRY_COUNT];

                                /* Modal closed (selected_version == -1): launch the default
                                   version -- versions[version_start] is always the game's own
                                   primary <Game> entry, guaranteed by compare_raw_games's
                                   primary-first sort tier, not just whichever version happens
                                   to sort first alphabetically. Modal open: launch whichever
                                   version is focused in it. */
                                int version_index = (gamelist.selected_version >= 0) ? gamelist.selected_version : 0;
                                const LaunchboxVersion *ver = &launchbox.versions[grp->version_start + version_index];

                                if (grp->version_count > 1 && gamelist.selected_version < 0) {
                                    SDL_Log("[main] Launching default version of '%s' (SHIFT+ENTER to pick another)",
                                            grp->title);
                                } else {
                                    SDL_Log("[main] Launching '%s' [%s]", grp->title, ver->label);
                                }

                                if (!launcher_launch(&launcher, ver)) {
                                    SDL_Log("[main] WARNING: failed to launch '%s'", grp->title);
                                }
                            }
                        }
                    }
                }
            }
        }

        /* List navigation repeat is driven here, once per frame, rather
           than off SDL_KEYDOWN's event.key.repeat -- see KeyRepeatState. */
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        Uint32 now = SDL_GetTicks();

        if (key_repeat_tick(&nav_up, keys[SDL_SCANCODE_UP], now, cfg.nav_repeat_delay_ms, cfg.nav_repeat_interval_ms)) {
            gamelist_move(&gamelist, &launchbox, -1);
        }
        if (key_repeat_tick(&nav_down, keys[SDL_SCANCODE_DOWN], now, cfg.nav_repeat_delay_ms, cfg.nav_repeat_interval_ms)) {
            gamelist_move(&gamelist, &launchbox, 1);
        }
        if (key_repeat_tick(&nav_left, keys[SDL_SCANCODE_LEFT], now, cfg.nav_repeat_delay_ms, cfg.nav_repeat_interval_ms)) {
            gamelist_jump_letter(&gamelist, &launchbox, -1);
        }
        if (key_repeat_tick(&nav_right, keys[SDL_SCANCODE_RIGHT], now, cfg.nav_repeat_delay_ms, cfg.nav_repeat_interval_ms)) {
            gamelist_jump_letter(&gamelist, &launchbox, 1);
        }

        render_frame(&render, &display, &cfg, &launchbox, &gamelist);
    }

    SDL_Log("[main] Shutting down");
    render_shutdown(&render);
    display_shutdown(&display);
    launchbox_free(&launchbox);
    launcher_free(&launcher);
    SDL_Quit();
    return 0;
}
