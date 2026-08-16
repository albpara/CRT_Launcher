#ifndef CRT_CALIBRATION_H
#define CRT_CALIBRATION_H

#include <SDL.h>

#include "config.h"
#include "gamelist.h"

/* Press-to-bind capture for the seven actions, driven straight off raw
   SDL events: during calibration there are no bindings to resolve against
   yet, and a joystick-only cabinet has no keyboard fallback either.

   Deliberately owns none of main.c's input trackers. main.c watches
   `active` for a false transition and re-primes them itself, so the press
   that ends calibration can't also act on the list. */
typedef struct {
    SDL_bool active;
    SDL_bool done_message;  /* completion text up, awaiting dismissal */
    int step;               /* the InputAction being captured */
    InputBinding bindings[INPUT_ACTION_COUNT];
    /* Axis events keep firing while held past the threshold -- without
       this gate one push captures several steps. Cleared when the stick
       returns under it. */
    SDL_bool axis_needs_release;
} CalibrationState;

/* Starts (or restarts) capture from the first action and puts the prompt
   up. Used at boot on an uncalibrated install and from the system menu. */
void calibration_begin(CalibrationState *cal, GameListState *gl);

/* Offers one raw event to the capture. SDL_TRUE if it was consumed, in
   which case nothing else may act on it. Commits the set and saves it to
   `config_path` on the last step; Escape cancels. */
SDL_bool calibration_handle_event(CalibrationState *cal, const SDL_Event *event,
                                   GameListState *gl, AppConfig *cfg, const char *config_path);

/* Clears the completion message and closes the modal it left up. */
void calibration_dismiss_message(CalibrationState *cal, GameListState *gl);

#endif /* CRT_CALIBRATION_H */
