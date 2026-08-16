#include "calibration.h"

#include <stdio.h>
#include <string.h>

static void set_prompt(CalibrationState *cal, GameListState *gl) {
    snprintf(gl->system_modal_status, sizeof(gl->system_modal_status),
             "PRESS INPUT FOR %s", INPUT_ACTION_NAMES[cal->step]);
    snprintf(gl->system_modal_hint, sizeof(gl->system_modal_hint),
             "ESC WILL EXIT CALIBRATION");
}

void calibration_begin(CalibrationState *cal, GameListState *gl) {
    memset(cal, 0, sizeof(*cal));
    cal->active = SDL_TRUE;
    gl->system_modal_open = SDL_TRUE;
    set_prompt(cal, gl);
}

void calibration_dismiss_message(CalibrationState *cal, GameListState *gl) {
    cal->done_message = SDL_FALSE;
    gl->system_modal_open = SDL_FALSE;
}

/* Stores one captured binding and advances; on the last step commits the
   set, saves it, and leaves a dismissible completion message up. */
static void capture(CalibrationState *cal, InputBinding captured, GameListState *gl,
                     AppConfig *cfg, const char *config_path) {
    char value[64];
    input_binding_to_string(&captured, value, sizeof(value));
    SDL_Log("[calibration] Calibrated %s = %s", INPUT_ACTION_NAMES[cal->step], value);

    cal->bindings[cal->step] = captured;
    cal->step++;

    if (cal->step >= INPUT_ACTION_COUNT) {
        memcpy(cfg->bindings, cal->bindings, sizeof(cfg->bindings));
        cfg->bindings_calibrated = SDL_TRUE;
        config_save_bindings(config_path, cfg->bindings);
        cal->active = SDL_FALSE;
        cal->done_message = SDL_TRUE;
        snprintf(gl->system_modal_status, sizeof(gl->system_modal_status),
                 "SAVED - FIND ME AT THE TOP OF THE LIST");
        gl->system_modal_hint[0] = '\0';
    } else {
        set_prompt(cal, gl);
    }
}

static InputBinding make(InputBindingType type) {
    InputBinding b;
    memset(&b, 0, sizeof(b));
    b.type = type;
    return b;
}

SDL_bool calibration_handle_event(CalibrationState *cal, const SDL_Event *event,
                                   GameListState *gl, AppConfig *cfg, const char *config_path) {
    if (!cal->active) {
        return SDL_FALSE;
    }

    /* The next qualifying raw input IS the binding for the current step;
       Escape is reserved to cancel. */
    if (event->type == SDL_KEYDOWN && !event->key.repeat) {
        if (event->key.keysym.sym == SDLK_ESCAPE) {
            cal->active = SDL_FALSE;
            gl->system_modal_open = SDL_FALSE;
            SDL_Log("[calibration] Cancelled");
        } else {
            InputBinding captured = make(INPUT_BINDING_KEYBOARD);
            captured.key = event->key.keysym.sym;
            capture(cal, captured, gl, cfg, config_path);
        }
        return SDL_TRUE;
    }

    if (event->type == SDL_JOYBUTTONDOWN) {
        InputBinding captured = make(INPUT_BINDING_JOY_BUTTON);
        captured.joy_button = event->jbutton.button;
        capture(cal, captured, gl, cfg, config_path);
        return SDL_TRUE;
    }

    if (event->type == SDL_JOYHATMOTION) {
        /* Logged unconditionally so "no hat events at all" is diagnosable
           from the log. */
        SDL_Log("[calibration] Saw JOYHATMOTION: hat=%d value=%d", event->jhat.hat, event->jhat.value);

        Uint8 dir = 0;
        if (event->jhat.value & SDL_HAT_UP) { dir = SDL_HAT_UP; }
        else if (event->jhat.value & SDL_HAT_DOWN) { dir = SDL_HAT_DOWN; }
        else if (event->jhat.value & SDL_HAT_LEFT) { dir = SDL_HAT_LEFT; }
        else if (event->jhat.value & SDL_HAT_RIGHT) { dir = SDL_HAT_RIGHT; }

        if (dir != 0) {
            InputBinding captured = make(INPUT_BINDING_JOY_HAT);
            captured.joy_hat = event->jhat.hat;
            captured.joy_hat_direction = dir;
            capture(cal, captured, gl, cfg, config_path);
        }
        return SDL_TRUE;
    }

    if (event->type == SDL_JOYAXISMOTION) {
        if (event->jaxis.value > -JOYSTICK_AXIS_THRESHOLD && event->jaxis.value < JOYSTICK_AXIS_THRESHOLD) {
            cal->axis_needs_release = SDL_FALSE;
        } else if (!cal->axis_needs_release) {
            InputBinding captured = make(INPUT_BINDING_JOY_AXIS);
            captured.joy_axis = event->jaxis.axis;
            captured.joy_axis_direction = (event->jaxis.value > 0) ? 1 : -1;
            cal->axis_needs_release = SDL_TRUE;
            capture(cal, captured, gl, cfg, config_path);
        }
        return SDL_TRUE;
    }

    return SDL_TRUE;  /* active: swallow anything else too */
}
