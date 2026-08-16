#include <SDL.h>

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "display.h"
#include "gamelist.h"
#include "launchbox.h"
#include "launcher.h"
#include "render.h"
#include "starfield.h"
#include "startup.h"

/* Safety cap; a real cabinet has one encoder. */
#define MAX_TRACKED_JOYSTICKS 4

#ifdef CRT_LAUNCHER_NO_CONSOLE
/* The distribution build has no console, so SDL_Log would go nowhere and a
   cabinet-only problem would be undiagnosable. Mirror it to a file next to
   the exe instead, truncated each run so it can't grow forever. */
static FILE *log_file = NULL;

static void log_to_file(void *userdata, int category, SDL_LogPriority priority, const char *message) {
    (void)userdata;
    (void)category;
    (void)priority;
    if (log_file) {
        fprintf(log_file, "%s\n", message);
        fflush(log_file); /* a hard exit must not lose the last lines */
    }
}

/* `config_path` is "<exe dir>\config.ini"; the log goes beside it. */
static void open_log_file(const char *config_path) {
    char path[CONFIG_DEFAULT_PATH_MAX];
    snprintf(path, sizeof(path), "%s", config_path);

    char *slash = strrchr(path, '\\');
    if (slash) {
        slash[1] = '\0';
    } else {
        path[0] = '\0';
    }
    strncat(path, "crt_launcher.log", sizeof(path) - strlen(path) - 1);

    log_file = fopen(path, "wb");
    if (log_file) {
        SDL_LogSetOutputFunction(log_to_file, NULL);
        SDL_Log("[main] Logging to '%s'", path);
    }
}
#endif

/* Opens every visible joystick (unopened devices generate no events).
   All open joysticks are treated as one undifferentiated input source. */
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

/* Own repeat timing (config-tunable, works for joysticks) instead of the
   OS key-repeat rate. */
typedef struct {
    SDL_bool held;
    Uint32 next_repeat_time;
} KeyRepeatState;

/* Fires once on press, then every `interval_ms` after `delay_ms`. */
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

/* One-shot press tracking for SELECT/BACK. */
typedef struct {
    SDL_bool held;
} EdgeState;

/* SDL_TRUE only on the not-held -> held transition. */
static SDL_bool edge_tick(EdgeState *state, SDL_bool is_held_now) {
    SDL_bool fired = (!state->held && is_held_now) ? SDL_TRUE : SDL_FALSE;
    state->held = is_held_now;
    return fired;
}

/* Axis deadzone (~25% of range), shared with calibration capture. */
#define JOYSTICK_AXIS_THRESHOLD 8000

/* The launch warp always runs at least MIN (so SELECT visibly registers
   even on a fast emulator), then until the game takes the foreground.
   TIMEOUT only catches an emulator that never opens a window, which would
   otherwise leave us warping with input swallowed. */
#define LAUNCH_WARP_MIN_MS 2000
#define LAUNCH_WARP_TIMEOUT_MS 10000

/* Is `b` active this frame? Any open joystick counts. */
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

/* Hardcoded keyboard fallback, always active under whatever's calibrated
   so a plugged-in keyboard always works. Deliberately not configurable. */
static const SDL_Scancode ACTION_FALLBACK_SCANCODE[INPUT_ACTION_COUNT] = {
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_RETURN, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_LSHIFT,
};

/* Calibrated binding OR keyboard fallback (MODIFIER also takes right
   Shift). */
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

/* Every per-frame input tracker: repeat timers for the four directions
   (indexed by INPUT_ACTION_UP..RIGHT) plus one-shot edges for
   SELECT/BACK. */
typedef struct {
    KeyRepeatState nav[4];
    EdgeState select_edge;
    EdgeState back_edge;
} InputState;

/* Marks every tracker as already-held, so the key/button that just ended
   calibration or woke the screensaver is consumed rather than also acting
   on the list -- Escape cancelling calibration used to immediately
   re-fire BACK and quit the app. The directions need it just as much as
   the edges: while a modal or the screensaver is up their dispatch is
   skipped, so key_repeat_tick would see the waking press as a fresh one
   and scroll the list underneath. A direction that stays held simply
   resumes repeating after the usual delay. */
static void prime_inputs_held(InputState *in, Uint32 now, int delay_ms) {
    for (int i = 0; i < 4; i++) {
        in->nav[i].held = SDL_TRUE;
        in->nav[i].next_repeat_time = now + (Uint32)delay_ms;
    }
    in->select_edge.held = SDL_TRUE;
    in->back_edge.held = SDL_TRUE;
}

/* The launch warp, run right after spawning so it covers the emulator's
   start-up instead of delaying it. Holds for LAUNCH_WARP_MIN_MS so SELECT
   visibly registers, then until the game takes the foreground. Input is
   read and discarded so a still-held SELECT can't act on the list. */
static void run_launch_warp(RenderContext *render, const DisplayContext *display,
                             Starfield *starfield) {
    Uint32 start = SDL_GetTicks();
    SDL_bool game_up = SDL_FALSE;
    SDL_bool done = SDL_FALSE;

    starfield_warp_start(starfield);
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                SDL_PushEvent(&event); /* not ours to swallow */
                done = SDL_TRUE;
            } else if (event.type == SDL_WINDOWEVENT &&
                       (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                        event.window.event == SDL_WINDOWEVENT_MINIMIZED)) {
                game_up = SDL_TRUE;
            }
        }

        Uint32 elapsed = SDL_GetTicks() - start;
        if (game_up && elapsed >= LAUNCH_WARP_MIN_MS) {
            done = SDL_TRUE;
        } else if (elapsed >= LAUNCH_WARP_TIMEOUT_MS) {
            SDL_Log("[main] Launch warp gave up after %ums with focus still held", (unsigned)elapsed);
            done = SDL_TRUE;
        }

        render_starfield_frame(render, display, starfield);
    }

    starfield_warp_stop(starfield);
}

/* Stores one captured binding, advances the calibration step, and on the
   last step commits + saves the set and leaves a dismissible completion
   message up. Shared by all four capture event types. */
static void calibrate_capture(InputBinding captured, InputBinding *calibrate_bindings, int *calibrate_step,
                               SDL_bool *calibrating, SDL_bool *calibration_done_message,
                               InputState *in, GameListState *gamelist, AppConfig *cfg,
                               const char *config_path) {
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
        prime_inputs_held(in, SDL_GetTicks(), cfg->nav_repeat_delay_ms);
        snprintf(gamelist->system_modal_status, sizeof(gamelist->system_modal_status),
                 "SAVED - FIND ME AT THE TOP OF THE LIST");
        gamelist->system_modal_hint[0] = '\0';
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

    /* Force classic XInput -- with HIDAPI/RawInput enabled too, some
       encoders enumerate twice and the duplicate drops hat events. Must
       precede SDL_Init. */
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_XBOX, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("[main] FATAL: SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Joystick *joysticks[MAX_TRACKED_JOYSTICKS] = {0};
    open_all_joysticks(joysticks);

    /* Resolved from the exe's own directory (a Run-key launch doesn't set
       CWD there); computed once, reused for every load/save. */
    char config_path[CONFIG_DEFAULT_PATH_MAX];
    config_resolve_default_path(config_path, sizeof(config_path));

#ifdef CRT_LAUNCHER_NO_CONSOLE
    open_log_file(config_path);
#endif

    AppConfig cfg;
    config_load(config_path, &cfg);

    /* No pointer UI; the cursor is just noise on the cabinet. */
    SDL_ShowCursor(SDL_DISABLE);

    LaunchboxInfo launchbox;
    launchbox_load(cfg.launchbox_dir, &launchbox);

    LauncherDatabase launcher;
    launcher_load(cfg.launchbox_dir, &launcher);

    GameListState gamelist;
    gamelist_init(&gamelist, &launchbox, cfg.selected_platforms);
    /* Nothing to show and the user hasn't chosen that -- say so up front
       rather than leaving an unexplained empty list. */
    gamelist.notice_open = (launchbox.status != LAUNCHBOX_STATUS_LOADED) ? SDL_TRUE : SDL_FALSE;

    DisplayContext display;
    if (!display_init(&cfg, &display)) {
        SDL_Quit();
        return 1;
    }

    RenderContext render;
    if (!render_init(display.window, &cfg, &render)) {
        display_shutdown(&display);
        SDL_Quit();
        return 1;
    }

    SDL_Log("[main] Entering main loop. %s toggles resolution; close the window to quit.",
            SDL_GetKeyName(cfg.toggle_hotkey));

    InputState input = {0};

    /* CRT burn-in protection -- see AppConfig.screensaver_timeout_ms.
       Runs regardless of focus (blanking behind a running game is
       harmless and invisible); regaining focus wakes it, see the
       SDL_WINDOWEVENT handler. */
    Uint32 last_activity_time = SDL_GetTicks();
    SDL_bool screensaver_active = SDL_FALSE;
    Starfield starfield;
    starfield_init(&starfield);

    SDL_bool calibrating = SDL_FALSE;
    SDL_bool calibration_done_message = SDL_FALSE;
    int calibrate_step = 0;
    InputBinding calibrate_bindings[INPUT_ACTION_COUNT] = {0};
    /* Axis events keep firing while held past the threshold -- without
       this gate one held push would capture several steps. Cleared when
       the stick returns under the threshold. */
    SDL_bool axis_needs_release = SDL_FALSE;

    if (!cfg.bindings_calibrated) {
        /* Uncalibrated install -- start in calibration; there's no
           reliable way to navigate to it yet. */
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
                /* Hot-plug: re-scan opens the new device. */
                open_all_joysticks(joysticks);
                continue;
            } else if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                    /* Coming back from a launched game: wake immediately
                       (returning to a black screen looks like a hang) and
                       restart the idle timer. Re-hiding the cursor matters
                       too -- the game can leave it visible over us. */
                    screensaver_active = SDL_FALSE;
                    last_activity_time = SDL_GetTicks();
                    SDL_ShowCursor(SDL_DISABLE);
                    prime_inputs_held(&input, SDL_GetTicks(), cfg.nav_repeat_delay_ms);
                }
                continue;
            }

            /* Raw-event activity check for the screensaver -- calibrated
               bindings can't be used here because during calibration none
               exist yet, and a joystick-only cabinet has no keyboard
               fallback either. */
            SDL_bool is_activity_event =
                (event.type == SDL_KEYDOWN && !event.key.repeat) ||
                event.type == SDL_JOYBUTTONDOWN ||
                (event.type == SDL_JOYHATMOTION && event.jhat.value != SDL_HAT_CENTERED) ||
                (event.type == SDL_JOYAXISMOTION &&
                 (event.jaxis.value > JOYSTICK_AXIS_THRESHOLD || event.jaxis.value < -JOYSTICK_AXIS_THRESHOLD));
            if (is_activity_event) {
                last_activity_time = SDL_GetTicks();
            }

            if (screensaver_active) {
                if (is_activity_event) {
                    SDL_Log("[main] Screensaver dismissed");
                    screensaver_active = SDL_FALSE;
                    prime_inputs_held(&input, SDL_GetTicks(), cfg.nav_repeat_delay_ms);
                }
                /* Event swallowed either way -- the waking press must not
                   also complete a calibration step or toggle resolution. */
            } else if (calibrating) {
                /* The next qualifying raw input IS the binding for the
                   current step; Escape is reserved to cancel. */
                if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        calibrating = SDL_FALSE;
                        gamelist.system_modal_open = SDL_FALSE;
                        prime_inputs_held(&input, SDL_GetTicks(), cfg.nav_repeat_delay_ms);
                        SDL_Log("[main] Calibration cancelled");
                    } else {
                        InputBinding captured;
                        memset(&captured, 0, sizeof(captured));
                        captured.type = INPUT_BINDING_KEYBOARD;
                        captured.key = event.key.keysym.sym;
                        calibrate_capture(captured, calibrate_bindings, &calibrate_step, &calibrating,
                                           &calibration_done_message, &input, &gamelist, &cfg, config_path);
                    }
                } else if (event.type == SDL_JOYBUTTONDOWN) {
                    InputBinding captured;
                    memset(&captured, 0, sizeof(captured));
                    captured.type = INPUT_BINDING_JOY_BUTTON;
                    captured.joy_button = event.jbutton.button;
                    calibrate_capture(captured, calibrate_bindings, &calibrate_step, &calibrating,
                                       &calibration_done_message, &input, &gamelist, &cfg, config_path);
                } else if (event.type == SDL_JOYHATMOTION) {
                    /* Logged unconditionally so "no hat events at all" is
                       diagnosable from the log. */
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
                                           &calibration_done_message, &input, &gamelist, &cfg, config_path);
                    }
                } else if (event.type == SDL_JOYAXISMOTION) {
                    if (event.jaxis.value > -JOYSTICK_AXIS_THRESHOLD && event.jaxis.value < JOYSTICK_AXIS_THRESHOLD) {
                        axis_needs_release = SDL_FALSE;
                    } else if (!axis_needs_release) {
                        InputBinding captured;
                        memset(&captured, 0, sizeof(captured));
                        captured.type = INPUT_BINDING_JOY_AXIS;
                        captured.joy_axis = event.jaxis.axis;
                        captured.joy_axis_direction = (event.jaxis.value > 0) ? 1 : -1;
                        axis_needs_release = SDL_TRUE;
                        calibrate_capture(captured, calibrate_bindings, &calibrate_step, &calibrating,
                                           &calibration_done_message, &input, &gamelist, &cfg, config_path);
                    }
                }
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym == cfg.toggle_hotkey) {
                display_toggle(&display);
            }
        }

        /* Per-frame input resolution. Screensaver idle-tracking always
           runs (even during calibration -- a cabinet parked on the
           calibration prompt must still blank); nav/SELECT/BACK dispatch
           is gated below. */
        {
            const Uint8 *keys = SDL_GetKeyboardState(NULL);
            Uint32 now = SDL_GetTicks();

            SDL_bool up_held = action_is_held(INPUT_ACTION_UP, &cfg, keys, joysticks, MAX_TRACKED_JOYSTICKS);
            SDL_bool down_held = action_is_held(INPUT_ACTION_DOWN, &cfg, keys, joysticks, MAX_TRACKED_JOYSTICKS);
            SDL_bool left_held = action_is_held(INPUT_ACTION_LEFT, &cfg, keys, joysticks, MAX_TRACKED_JOYSTICKS);
            SDL_bool right_held = action_is_held(INPUT_ACTION_RIGHT, &cfg, keys, joysticks, MAX_TRACKED_JOYSTICKS);
            SDL_bool select_held = action_is_held(INPUT_ACTION_SELECT, &cfg, keys, joysticks, MAX_TRACKED_JOYSTICKS);
            SDL_bool back_held = action_is_held(INPUT_ACTION_BACK, &cfg, keys, joysticks, MAX_TRACKED_JOYSTICKS);
            SDL_bool modifier_held = action_is_held(INPUT_ACTION_MODIFIER, &cfg, keys, joysticks, MAX_TRACKED_JOYSTICKS);
            SDL_bool any_action_held = up_held || down_held || left_held || right_held ||
                                        select_held || back_held || modifier_held;

            if (screensaver_active) {
                if (any_action_held) {
                    SDL_Log("[main] Screensaver dismissed");
                    screensaver_active = SDL_FALSE;
                    last_activity_time = now;
                    /* The waking press must not also act on the list. */
                    prime_inputs_held(&input, SDL_GetTicks(), cfg.nav_repeat_delay_ms);
                }
            } else {
                if (any_action_held) {
                    last_activity_time = now;
                } else if (cfg.screensaver_timeout_ms > 0 &&
                           (now - last_activity_time) >= (Uint32)cfg.screensaver_timeout_ms) {
                    SDL_Log("[main] Screensaver activated after %dms idle", cfg.screensaver_timeout_ms);
                    screensaver_active = SDL_TRUE;
                }
            }

            if (!calibrating && !screensaver_active) {
                if (key_repeat_tick(&input.nav[INPUT_ACTION_UP], up_held, now, cfg.nav_repeat_delay_ms, cfg.nav_repeat_interval_ms)) {
                    gamelist_move(&gamelist, &launchbox, -1);
                }
                if (key_repeat_tick(&input.nav[INPUT_ACTION_DOWN], down_held, now, cfg.nav_repeat_delay_ms, cfg.nav_repeat_interval_ms)) {
                    gamelist_move(&gamelist, &launchbox, 1);
                }
                if (key_repeat_tick(&input.nav[INPUT_ACTION_LEFT], left_held, now, cfg.nav_repeat_delay_ms, cfg.nav_repeat_interval_ms)) {
                    gamelist_jump_letter(&gamelist, &launchbox, -1);
                }
                if (key_repeat_tick(&input.nav[INPUT_ACTION_RIGHT], right_held, now, cfg.nav_repeat_delay_ms, cfg.nav_repeat_interval_ms)) {
                    gamelist_jump_letter(&gamelist, &launchbox, 1);
                }

                SDL_bool select_pressed = edge_tick(&input.select_edge, select_held);
                SDL_bool back_pressed = edge_tick(&input.back_edge, back_held);

                if (calibration_done_message) {
                    /* Either action dismisses the completion message. */
                    if (select_pressed || back_pressed) {
                        calibration_done_message = SDL_FALSE;
                        gamelist.system_modal_open = SDL_FALSE;
                    }
                } else if (gamelist.exit_confirm_open) {
                    if (select_pressed) {
                        running = SDL_FALSE;
                    } else if (back_pressed) {
                        gamelist.exit_confirm_open = SDL_FALSE;
                    }
                } else if (gamelist.notice_open) {
                    /* After calibration, never before: an uncalibrated
                       install has both up, and this must not swallow the
                       press that dismisses the completion message. Either
                       action closes it, leaving the settings rows behind
                       it usable. */
                    if (select_pressed || back_pressed) {
                        gamelist.notice_open = SDL_FALSE;
                    }
                } else {
                    /* SELECT and BACK are mutually exclusive per frame --
                       both firing together used to corrupt modal state
                       (SELECT opening a modal, same-frame BACK closing it). */
                    if (select_pressed) {
                        if (gamelist_selected_is_system(&gamelist)) {
                            if (gamelist.selected_group == GAMELIST_SYSTEM_ENTRY_STARTUP) {
                                /* Immediate toggle; the registry is the only
                                   source of truth (see startup.h). */
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
                                    /* Picker closed: launch the default version
                                       (versions[version_start] is the primary
                                       <Game> entry). Open: launch the focused one. */
                                    int version_index = (gamelist.selected_version >= 0) ? gamelist.selected_version : 0;
                                    const LaunchboxVersion *ver = &launchbox.versions[grp->version_start + version_index];

                                    SDL_Log("[main] Launching '%s' [%s]", grp->title, ver->label);
                                    if (launcher_launch(&launcher, ver)) {
                                        /* CreateProcess returned already, so
                                           the warp covers start-up rather
                                           than delaying it. */
                                        run_launch_warp(&render, &display, &starfield);
                                    } else {
                                        SDL_Log("[main] WARNING: failed to launch '%s'", grp->title);
                                    }
                                    /* The warp ran with input swallowed;
                                       don't let it count toward idle. */
                                    last_activity_time = SDL_GetTicks();

                                    /* Close the picker so the app isn't stuck
                                       "inside" it after returning from the game. */
                                    gamelist.selected_version = -1;
                                }
                            }
                        }
                    } else if (back_pressed) {
                        if (gamelist.selected_version >= 0) {
                            gamelist_toggle_expand(&gamelist, &launchbox);
                        } else if (gamelist.system_modal_open) {
                            gamelist.system_modal_open = SDL_FALSE;
                        } else {
                            /* Confirm rather than quit -- BACK is easy to bump
                               on a controller. */
                            gamelist.exit_confirm_open = SDL_TRUE;
                        }
                    }
                }
            }
        }

        if (screensaver_active) {
            render_starfield_frame(&render, &display, &starfield);
        } else {
            render_frame(&render, &display, &cfg, &launchbox, &gamelist, &starfield);
        }
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
    starfield_free(&starfield);
    SDL_Quit();
    return 0;
}
