#ifndef CRT_STARTUP_H
#define CRT_STARTUP_H

#include <SDL.h>

/*
 * Whether/how CRT Launcher registers itself to run when Windows starts --
 * a single value named "CRT Launcher" under the current user's
 * HKCU\Software\Microsoft\Windows\CurrentVersion\Run key (per-user, no
 * admin rights needed, no shortcut/COM machinery -- see CLAUDE.md for why
 * this was picked over a Startup-folder .lnk). Deliberately NOT reflected
 * in config.ini -- the registry itself is the only source of truth, so
 * toggling it from Task Manager's Startup tab or msconfig stays in sync
 * with what the app shows automatically, and there's no risk of the two
 * disagreeing after a hand-edit.
 */

/* SDL_TRUE if the "CRT Launcher" value currently exists under the Run
   key. Queried fresh every call (one registry read) -- cheap enough to
   call every frame the system-menu row showing this is on screen, so the
   label never shows stale state. */
SDL_bool startup_is_enabled(void);

/* Writes this exe's own current path (resolved via GetModuleFileNameA,
   quoted) as the "CRT Launcher" value under the Run key. Re-running this
   after the install has moved overwrites the old path, so toggling it off
   and back on self-corrects rather than leaving a stale entry. Returns
   SDL_TRUE on success. */
SDL_bool startup_enable(void);

/* Deletes the "CRT Launcher" value. Returns SDL_TRUE if it's gone
   afterward, including when it didn't exist to begin with. */
SDL_bool startup_disable(void);

#endif /* CRT_STARTUP_H */
