#include <SDL.h>

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "display.h"
#include "gamelist.h"
#include "launchbox.h"
#include "launcher.h"
#include "render.h"
#include "startup.h"

/* Bound for how many physical joysticks/pads we'll keep open at once --
   comfortably above any real cabinet setup (typically one encoder), just a
   safety cap. */
#define MAX_TRACKED_JOYSTICKS 4

/* Opens every joystick currently visible to SDL (a no-op device generates
   no button/hat events until it's actually opened). Not a real device-
   selection step -- every open joystick is treated as one undifferentiated
   input source (see binding_is_held) -- fine for the single-encoder
   cabinet this was built for. See the SDL_JOYDEVICEADDED handling in the
   main loop for how this stays current if something's plugged in after
   launch. */
static void open_all_joysticks(SDL_Joystick *joysticks[MAX_TRACKED_JOYSTICKS]) {
    int count = SDL_NumJoysticks();
    for (int i = 0; i < count && i < MAX_TRACKED_JOYSTICKS; i++) {
        if (!joysticks[i]) {
            joysticks[i] = SDL_JoystickOpen(i);
            if (joysticks[i]) {
                SDL_Log("[main] Opened joystick %d: '%s'", i, SDL_JoystickNameForIndex(i));
            }
        }
    }
}

/* Tracks one action's own held/repeat timing for continuous navigation,
   independent of the OS's keyboard repeat rate (SDL_KEYDOWN's
   event.key.repeat just mirrors whatever Windows' Control Panel repeat
   setting is, which isn't configurable from here, and doesn't exist at all
   for joystick input anyway). Polled once per frame against
   binding_is_held() instead. */
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

/* Tracks one action's held state for a one-shot (non-repeating) press --
   SELECT/BACK should fire exactly once per press, not keep re-firing every
   frame while held (that would re-launch a game or re-toggle a modal
   continuously). */
typedef struct {
    SDL_bool held;
} EdgeState;

/* Returns SDL_TRUE only on the transition from not-held to held. */
static SDL_bool edge_tick(EdgeState *state, SDL_bool is_held_now) {
    SDL_bool fired = (!state->held && is_held_now) ? SDL_TRUE : SDL_FALSE;
    state->held = is_held_now;
    return fired;
}

/* Axis motion jitters near center even at rest -- only worth treating as
   "held" once clearly past a deadzone, not on every tiny fluctuation.
   ~25% of SDL's +-32767 axis range. Shared between runtime resolution
   (binding_is_held) and calibration capture, so both agree on what counts
   as a real push. */
#define JOYSTICK_AXIS_THRESHOLD 8000

/* Resolves whether `b` is currently active against this frame's keyboard
   state and every open joystick -- source-agnostic, so the rest of main()
   never needs to know whether a given action is bound to a key or a
   physical pad input. Joysticks aren't disambiguated by device (see
   open_all_joysticks) -- any open joystick matching counts, which is fine
   for a single-encoder cabinet. */
static SDL_bool binding_is_held(const InputBinding *b, const Uint8 *keyboard_state,
                                 SDL_Joystick *const *joysticks, int joystick_count) {
    switch (b->type) {
        case INPUT_BINDING_KEYBOARD: {
            SDL_Scancode sc = SDL_GetScancodeFromKey(b->key);
            return keyboard_state[sc] ? SDL_TRUE : SDL_FALSE;
        }
        case INPUT_BINDING_JOY_BUTTON:
            for (int i = 0; i < joystick_count; i++) {
                if (joysticks[i] && SDL_JoystickGetButton(joysticks[i], b->joy_button)) {
                    return SDL_TRUE;
                }
            }
            return SDL_FALSE;
        case INPUT_BINDING_JOY_HAT:
            for (int i = 0; i < joystick_count; i++) {
                if (joysticks[i] && (SDL_JoystickGetHat(joysticks[i], b->joy_hat) & b->joy_hat_direction)) {
                    return SDL_TRUE;
                }
            }
            return SDL_FALSE;
        case INPUT_BINDING_JOY_AXIS:
            for (int i = 0; i < joystick_count; i++) {
                if (!joysticks[i]) {
                    continue;
                }
                Sint16 value = SDL_JoystickGetAxis(joysticks[i], b->joy_axis);
                if (b->joy_axis_direction > 0 && value > JOYSTICK_AXIS_THRESHOLD) {
                    return SDL_TRUE;
                }
                if (b->joy_axis_direction < 0 && value < -JOYSTICK_AXIS_THRESHOLD) {
                    return SDL_TRUE;
                }
            }
            return SDL_FALSE;
        case INPUT_BINDING_NONE:
        default:
            return SDL_FALSE;
    }
}

/* Hardcoded keyboard fallback for each action -- arrows/Enter/Esc/Shift
   always work for navigation, regardless of what's actually calibrated, so
   a keyboard plugged into the cabinet is always a working escape hatch
   even after calibrating for a different device entirely. Deliberately
   NOT configurable (per explicit request) -- this is meant to be a fixed
   safety net, not another setting to maintain. */
static const SDL_Scancode ACTION_FALLBACK_SCANCODE[INPUT_ACTION_COUNT] = {
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_RETURN, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_LSHIFT,
};

/* SDL_TRUE if `action`'s calibrated binding is active, OR its hardcoded
   keyboard fallback is -- MODIFIER also accepts the right-hand Shift key,
   matching how most software treats "Shift" as either one. */
static SDL_bool action_is_held(InputAction action, const AppConfig *cfg, const Uint8 *keys,
                                SDL_Joystick *const *joysticks, int joystick_count) {
    if (binding_is_held(&cfg->bindings[action], keys, joysticks, joystick_count)) {
        return SDL_TRUE;
    }
    if (keys[ACTION_FALLBACK_SCANCODE[action]]) {
        return SDL_TRUE;
    }
    if (action == INPUT_ACTION_MODIFIER && keys[SDL_SCANCODE_RSHIFT]) {
        return SDL_TRUE;
    }
    return SDL_FALSE;
}

/* Marks both edge trackers as "already held" -- called at every point
   calibration mode ends (cancelled or finished) so that whatever physical
   key/button is *still being held* at that exact instant (it necessarily
   is one -- that's what just ended calibration) doesn't ALSO register as a
   fresh SELECT/BACK press the instant per-frame polling resumes next
   frame. Without this, e.g. cancelling with Escape -- which is both the
   calibration-cancel key AND BACK's hardcoded fallback -- immediately
   re-fired BACK on the very next frame and quit the whole app, which is
   what was being seen as "Esc crashes during calibration". Worst case if
   the key already happens to be released by then, this just costs one
   extra frame before a genuinely new press registers -- harmless. */
static void prime_edges_held(EdgeState *select_edge, EdgeState *back_edge) {
    select_edge->held = SDL_TRUE;
    back_edge->held = SDL_TRUE;
}

/* Records `captured` as the binding for the calibration step currently in
   progress, advances to the next action, and -- once all of them are done
   -- commits the result into `cfg`, persists it via
   config_save_bindings(), and leaves a dismissible "where to find this
   again" message up (see calibration_done_message in main()) instead of
   closing the modal immediately, since the system menu it lives under is
   hidden by default. Shared by the three raw-event sources that can supply
   a binding (keyboard, joystick button, joystick hat, joystick axis) so
   the advance/finish logic isn't quadrupled. */
static void calibrate_capture(InputBinding captured, InputBinding *calibrate_bindings, int *calibrate_step,
                               SDL_bool *calibrating, SDL_bool *calibration_done_message,
                               EdgeState *select_edge, EdgeState *back_edge,
                               GameListState *gamelist, AppConfig *cfg, const char *config_path) {
    char value[64];
    input_binding_to_string(&captured, value, sizeof(value));
    SDL_Log("[main] Calibrated %s = %s", INPUT_ACTION_NAMES[*calibrate_step], value);

    calibrate_bindings[*calibrate_step] = captured;
    (*calibrate_step)++;

    if (*calibrate_step >= INPUT_ACTION_COUNT) {
        memcpy(cfg->bindings, calibrate_bindings, sizeof(cfg->bindings));
        cfg->bindings_calibrated = SDL_TRUE;
        config_save_bindings(config_path, cfg->bindings);
        *calibrating = SDL_FALSE;
        *calibration_done_message = SDL_TRUE;
        prime_edges_held(select_edge, back_edge);
        snprintf(gamelist->system_modal_status, sizeof(gamelist->system_modal_status),
                 "SAVED - FIND ME AT THE TOP OF THE LIST");
        gamelist->system_modal_hint[0] = '\0'; /* Esc has no special meaning here anymore */
    } else {
        snprintf(gamelist->system_modal_status, sizeof(gamelist->system_modal_status),
                 "PRESS INPUT FOR %s", INPUT_ACTION_NAMES[*calibrate_step]);
        snprintf(gamelist->system_modal_hint, sizeof(gamelist->system_modal_hint),
                 "ESC WILL EXIT CALIBRATION");
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    /* Some Xbox-compatible pads/encoders get enumerated TWICE on Windows
       when both the classic XInput backend and the newer HIDAPI backend
       claim the same physical device -- when that happens, only one of the
       two enumerations reliably reports the D-pad as a hat (the other may
       drop it or expose it differently), which can make the hat appear to
       just not work even though regular buttons do. Forcing the classic
       XInput-only path avoids the double-enumeration. Must be set before
       SDL_Init. */
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_XBOX, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");

    /* SDL_INIT_GAMECONTROLLER implies SDL_INIT_JOYSTICK -- needed for
       joystick bindings/calibration to see any pad/encoder at all; without
       it no joystick events are ever generated, even for a device that's
       physically connected. */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("[main] FATAL: SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Joystick *joysticks[MAX_TRACKED_JOYSTICKS] = {0};
    open_all_joysticks(joysticks);

    /* Resolved from the exe's own directory, not the process's current
       working directory -- see config_resolve_default_path()'s doc
       comment for why that distinction matters (a Windows-startup Run key
       launch does not preserve the exe's folder as CWD the way a
       shortcut's "Start in" field would). Computed once and reused for
       every config_load/config_save_* call below. */
    char config_path[CONFIG_DEFAULT_PATH_MAX];
    config_resolve_default_path(config_path, sizeof(config_path));

    AppConfig cfg;
    config_load(config_path, &cfg);

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
    gamelist_init(&gamelist, &launchbox, cfg.selected_platforms);

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

    SDL_Log("[main] Entering main loop. %s toggles resolution; directions/select/back/modifier "
            "are whatever's bound in config.ini (scroll up to CALIBRATE CONTROLS to change them); "
            "close the window to quit.",
            SDL_GetKeyName(cfg.toggle_hotkey));

    KeyRepeatState nav_up = {0};
    KeyRepeatState nav_down = {0};
    KeyRepeatState nav_left = {0};
    KeyRepeatState nav_right = {0};
    EdgeState select_edge = {0};
    EdgeState back_edge = {0};

    SDL_bool calibrating = SDL_FALSE;
    SDL_bool calibration_done_message = SDL_FALSE;
    int calibrate_step = 0;
    InputBinding calibrate_bindings[INPUT_ACTION_COUNT] = {0};
    /* Unlike a button press or a hat's value change, axis motion keeps
       firing events every tick while held past the threshold -- without
       this, holding a direction a moment too long would capture the SAME
       axis/direction again for the NEXT step too. Sits true from a
       successful axis capture until an axis event reports back under the
       threshold (the stick returning toward center), gating out further
       axis captures in between. Reset whenever calibration (re)starts. */
    SDL_bool axis_needs_release = SDL_FALSE;

    if (!cfg.bindings_calibrated) {
        /* Nothing has ever been calibrated on this install (config.ini has
           no [bindings] section) -- land directly in calibration instead
           of the game list, since an uncalibrated cabinet has no reliable
           way to navigate to CALIBRATE CONTROLS in the first place. */
        SDL_Log("[main] No calibration on file -- starting calibration automatically");
        calibrating = SDL_TRUE;
        axis_needs_release = SDL_FALSE;
        gamelist.selected_group = 0;
        gamelist.system_modal_open = SDL_TRUE;
        snprintf(gamelist.system_modal_status, sizeof(gamelist.system_modal_status),
                 "PRESS INPUT FOR %s", INPUT_ACTION_NAMES[0]);
        snprintf(gamelist.system_modal_hint, sizeof(gamelist.system_modal_hint),
                 "ESC WILL EXIT CALIBRATION");
    }

    SDL_bool running = SDL_TRUE;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = SDL_FALSE;
            } else if (event.type == SDL_JOYDEVICEADDED) {
                /* event.jdevice.which is a device INDEX for this event
                   specifically (unlike button/hat events, where it's an
                   instance ID) -- open_all_joysticks() re-scans from 0, so
                   a freshly plugged-in pad gets opened here without needing
                   to track index-to-slot bookkeeping ourselves. Handled
                   whether or not calibration is in progress. */
                open_all_joysticks(joysticks);
            } else if (calibrating) {
                /* While calibrating, the next qualifying raw input IS the
                   binding for the current step -- Escape is the one
                   reserved key, it cancels instead of being capturable (so
                   BACK can still be bound to anything else, just not
                   discovered via this particular key during calibration). */
                if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        calibrating = SDL_FALSE;
                        gamelist.system_modal_open = SDL_FALSE;
                        prime_edges_held(&select_edge, &back_edge);
                        SDL_Log("[main] Calibration cancelled");
                    } else {
                        InputBinding captured;
                        memset(&captured, 0, sizeof(captured));
                        captured.type = INPUT_BINDING_KEYBOARD;
                        captured.key = event.key.keysym.sym;
                        calibrate_capture(captured, calibrate_bindings, &calibrate_step, &calibrating,
                                           &calibration_done_message, &select_edge, &back_edge, &gamelist, &cfg, config_path);
                    }
                } else if (event.type == SDL_JOYBUTTONDOWN) {
                    InputBinding captured;
                    memset(&captured, 0, sizeof(captured));
                    captured.type = INPUT_BINDING_JOY_BUTTON;
                    captured.joy_button = event.jbutton.button;
                    calibrate_capture(captured, calibrate_bindings, &calibrate_step, &calibrating,
                                       &calibration_done_message, &select_edge, &back_edge, &gamelist, &cfg, config_path);
                } else if (event.type == SDL_JOYHATMOTION) {
                    /* Logged unconditionally (not just on a successful
                       capture) -- if a direction still doesn't register
                       during calibration, this line's presence or absence
                       in the log says whether SDL is sending hat events for
                       this device at all, which is the key fact needed to
                       tell "wrong bit decoded" from "no event arrived". */
                    SDL_Log("[main] Calibration saw JOYHATMOTION: hat=%d value=%d", event.jhat.hat, event.jhat.value);

                    Uint8 dir = 0;
                    if (event.jhat.value & SDL_HAT_UP) { dir = SDL_HAT_UP; }
                    else if (event.jhat.value & SDL_HAT_DOWN) { dir = SDL_HAT_DOWN; }
                    else if (event.jhat.value & SDL_HAT_LEFT) { dir = SDL_HAT_LEFT; }
                    else if (event.jhat.value & SDL_HAT_RIGHT) { dir = SDL_HAT_RIGHT; }
                    if (dir != 0) {
                        InputBinding captured;
                        memset(&captured, 0, sizeof(captured));
                        captured.type = INPUT_BINDING_JOY_HAT;
                        captured.joy_hat = event.jhat.hat;
                        captured.joy_hat_direction = dir;
                        calibrate_capture(captured, calibrate_bindings, &calibrate_step, &calibrating,
                                           &calibration_done_message, &select_edge, &back_edge, &gamelist, &cfg, config_path);
                    } /* SDL_HAT_CENTERED (releasing back to center) isn't a press -- ignored */
                } else if (event.type == SDL_JOYAXISMOTION) {
                    if (event.jaxis.value > -JOYSTICK_AXIS_THRESHOLD && event.jaxis.value < JOYSTICK_AXIS_THRESHOLD) {
                        /* Back under the threshold -- the stick returning
                           toward center. Not itself a capture, but this is
                           what clears axis_needs_release so the next real
                           push can be captured. */
                        axis_needs_release = SDL_FALSE;
                    } else if (!axis_needs_release) {
                        InputBinding captured;
                        memset(&captured, 0, sizeof(captured));
                        captured.type = INPUT_BINDING_JOY_AXIS;
                        captured.joy_axis = event.jaxis.axis;
                        captured.joy_axis_direction = (event.jaxis.value > 0) ? 1 : -1;
                        axis_needs_release = SDL_TRUE;
                        calibrate_capture(captured, calibrate_bindings, &calibrate_step, &calibrating,
                                           &calibration_done_message, &select_edge, &back_edge, &gamelist, &cfg, config_path);
                    } /* else: still the same held push past threshold -- already captured, ignored until release */
                }
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym == cfg.toggle_hotkey) {
                display_toggle(&display);
            }
        }

        /* Navigation repeat and SELECT/BACK are driven here, once per
           frame, against whatever's bound -- not tied to SDL_KEYDOWN at
           all, so the same code path drives keyboard and joystick
           identically. Suppressed entirely while calibrating (the modal
           has nothing to navigate, and a bound key/button firing here
           while ALSO being captured above would be confusing). */
        if (!calibrating) {
            const Uint8 *keys = SDL_GetKeyboardState(NULL);
            Uint32 now = SDL_GetTicks();

            if (key_repeat_tick(&nav_up, action_is_held(INPUT_ACTION_UP, &cfg, keys, joysticks, MAX_TRACKED_JOYSTICKS),
                                 now, cfg.nav_repeat_delay_ms, cfg.nav_repeat_interval_ms)) {
                gamelist_move(&gamelist, &launchbox, -1);
            }
            if (key_repeat_tick(&nav_down, action_is_held(INPUT_ACTION_DOWN, &cfg, keys, joysticks, MAX_TRACKED_JOYSTICKS),
                                 now, cfg.nav_repeat_delay_ms, cfg.nav_repeat_interval_ms)) {
                gamelist_move(&gamelist, &launchbox, 1);
            }
            if (key_repeat_tick(&nav_left, action_is_held(INPUT_ACTION_LEFT, &cfg, keys, joysticks, MAX_TRACKED_JOYSTICKS),
                                 now, cfg.nav_repeat_delay_ms, cfg.nav_repeat_interval_ms)) {
                gamelist_jump_letter(&gamelist, &launchbox, -1);
            }
            if (key_repeat_tick(&nav_right, action_is_held(INPUT_ACTION_RIGHT, &cfg, keys, joysticks, MAX_TRACKED_JOYSTICKS),
                                 now, cfg.nav_repeat_delay_ms, cfg.nav_repeat_interval_ms)) {
                gamelist_jump_letter(&gamelist, &launchbox, 1);
            }

            SDL_bool select_pressed = edge_tick(&select_edge,
                action_is_held(INPUT_ACTION_SELECT, &cfg, keys, joysticks, MAX_TRACKED_JOYSTICKS));
            SDL_bool back_pressed = edge_tick(&back_edge,
                action_is_held(INPUT_ACTION_BACK, &cfg, keys, joysticks, MAX_TRACKED_JOYSTICKS));
            SDL_bool modifier_held =
                action_is_held(INPUT_ACTION_MODIFIER, &cfg, keys, joysticks, MAX_TRACKED_JOYSTICKS);

            if (calibration_done_message) {
                /* The "find me at the top of the list" message left up by
                   calibrate_capture() -- either action just dismisses it,
                   deliberately not distinguishing SELECT/BACK here so
                   whichever one the user just calibrated works. Nothing
                   else (navigation, launching) is live while this is up. */
                if (select_pressed || back_pressed) {
                    calibration_done_message = SDL_FALSE;
                    gamelist.system_modal_open = SDL_FALSE;
                }
            } else if (gamelist.exit_confirm_open) {
                /* SELECT confirms (actually quit); BACK backs out to the
                   list without quitting. Nothing else is live while this
                   is up (see the gamelist.c guards on exit_confirm_open). */
                if (select_pressed) {
                    running = SDL_FALSE;
                } else if (back_pressed) {
                    gamelist.exit_confirm_open = SDL_FALSE;
                }
            } else {
                /* select_pressed and back_pressed are handled as mutually
                   exclusive within a single frame (else if, not two plain
                   ifs) -- both firing on the same frame is a real
                   possibility (a controller chord, or SELECT/BACK sharing
                   a fallback key with something else) and used to corrupt
                   state: e.g. SELECT starting calibration (setting
                   calibrating=TRUE, system_modal_open=TRUE) immediately
                   followed by BACK's still-stale system_modal_open check
                   closing the modal right back -- leaving calibration
                   silently running with nothing on screen to show it. If
                   both are pressed together now, only SELECT's action
                   fires this frame; BACK just waits for a frame where it's
                   pressed alone. */
                if (select_pressed) {
                    if (gamelist_selected_is_system(&gamelist)) {
                        if (gamelist.selected_group == GAMELIST_SYSTEM_ENTRY_STARTUP) {
                            /* Immediate toggle, no modal -- matches the
                               platform checkboxes below, not the
                               calibration flow. Nothing to persist to
                               config.ini either; the registry itself is
                               the only source of truth (see startup.h). */
                            if (startup_is_enabled()) {
                                startup_disable();
                            } else {
                                startup_enable();
                            }
                        } else {
                            SDL_Log("[main] Starting calibration");
                            calibrating = SDL_TRUE;
                            calibrate_step = 0;
                            axis_needs_release = SDL_FALSE;
                            memset(calibrate_bindings, 0, sizeof(calibrate_bindings));
                            gamelist.system_modal_open = SDL_TRUE;
                            snprintf(gamelist.system_modal_status, sizeof(gamelist.system_modal_status),
                                     "PRESS INPUT FOR %s", INPUT_ACTION_NAMES[0]);
                            snprintf(gamelist.system_modal_hint, sizeof(gamelist.system_modal_hint),
                                     "ESC WILL EXIT CALIBRATION");
                        }
                    } else if (gamelist_selected_is_platform(&gamelist, &launchbox)) {
                        int idx = gamelist_selected_platform_index(&gamelist);
                        gamelist_toggle_platform(&gamelist, &launchbox, idx);

                        char csv[CONFIG_SELECTED_PLATFORMS_MAX];
                        gamelist_format_platform_selection(&gamelist, &launchbox, csv, sizeof(csv));
                        config_save_selected_platforms(config_path, csv);

                        SDL_Log("[main] Platform '%s' %s", launchbox.platform_names[idx],
                                (gamelist.platform_selected && gamelist.platform_selected[idx]) ? "enabled" : "disabled");
                    } else {
                        const LaunchboxGameGroup *grp = gamelist_selected_group(&gamelist, &launchbox);
                        if (grp) {
                            if (modifier_held) {
                                gamelist_toggle_expand(&gamelist, &launchbox);
                            } else {
                                /* Modal closed (selected_version == -1): launch the default
                                   version -- versions[version_start] is always the game's own
                                   primary <Game> entry, guaranteed by compare_raw_games's
                                   primary-first sort tier, not just whichever version happens
                                   to sort first alphabetically. Modal open: launch whichever
                                   version is focused in it. */
                                int version_index = (gamelist.selected_version >= 0) ? gamelist.selected_version : 0;
                                const LaunchboxVersion *ver = &launchbox.versions[grp->version_start + version_index];

                                if (grp->version_count > 1 && gamelist.selected_version < 0) {
                                    SDL_Log("[main] Launching default version of '%s' (MODIFIER+SELECT to pick another)",
                                            grp->title);
                                } else {
                                    SDL_Log("[main] Launching '%s' [%s]", grp->title, ver->label);
                                }

                                if (!launcher_launch(&launcher, ver)) {
                                    SDL_Log("[main] WARNING: failed to launch '%s'", grp->title);
                                }

                                /* Close the version-picker modal if launching from
                                   within it -- without this, selected_version stayed
                                   >= 0 after the launch, so the app was still
                                   logically "inside" the modal (and would keep
                                   showing it) the whole time the game was running
                                   and after returning from it. */
                                gamelist.selected_version = -1;
                            }
                        }
                    }
                } else if (back_pressed) {
                    if (gamelist.selected_version >= 0) {
                        gamelist_toggle_expand(&gamelist, &launchbox); /* close the version modal first */
                    } else if (gamelist.system_modal_open) {
                        gamelist.system_modal_open = SDL_FALSE;
                    } else {
                        /* Top level of the main list -- ask for
                           confirmation rather than quitting immediately,
                           since BACK is now reachable from a controller
                           button that's easy to bump by accident. */
                        gamelist.exit_confirm_open = SDL_TRUE;
                    }
                }
            }
        }

        render_frame(&render, &display, &cfg, &launchbox, &gamelist);
    }

    SDL_Log("[main] Shutting down");
    for (int i = 0; i < MAX_TRACKED_JOYSTICKS; i++) {
        if (joysticks[i]) {
            SDL_JoystickClose(joysticks[i]);
        }
    }
    render_shutdown(&render);
    display_shutdown(&display);
    gamelist_free(&gamelist);
    launchbox_free(&launchbox);
    launcher_free(&launcher);
    SDL_Quit();
    return 0;
}
