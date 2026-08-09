#ifndef CRT_STARTUP_H
#define CRT_STARTUP_H

#include <SDL.h>

/* Run-at-Windows-sign-in registration via a "CRT Launcher" value under
   HKCU\...\Run (per-user, no admin). Deliberately not mirrored in
   config.ini -- the registry is the only source of truth, so external
   changes (Task Manager's Startup tab) can't drift out of sync. */

/* SDL_TRUE if the Run-key value exists. Cheap enough to query per frame. */
SDL_bool startup_is_enabled(void);

/* Writes this exe's own current path (quoted) as the Run-key value --
   re-enabling after a move self-corrects the stored path. */
SDL_bool startup_enable(void);

/* Deletes the value. SDL_TRUE if it's gone (including already-absent). */
SDL_bool startup_disable(void);

#endif /* CRT_STARTUP_H */
