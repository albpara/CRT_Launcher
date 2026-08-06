#ifndef CRT_CONFIG_H
#define CRT_CONFIG_H

#include <SDL.h>

/* Settings loaded from config.ini. Every field always ends up with a valid
   value -- config_load() fills in defaults for anything missing, unparsable,
   or if the file can't be opened at all. */
#define CONFIG_LAUNCHBOX_DIR_MAX 512

typedef struct {
    int width;
    int height;
    int refresh_rate;      /* Hz. 0 means "any refresh rate is fine". */
    SDL_Keycode toggle_hotkey;
    /* Up/Down/Left/Right navigation repeat, applied by main.c's own timer
       (SDL_GetTicks-based, not the OS's key-repeat rate -- see main.c) so
       it's actually tunable here instead of stuck at whatever Windows'
       keyboard repeat setting happens to be. */
    int nav_repeat_delay_ms;    /* how long a direction must be held before it starts repeating */
    int nav_repeat_interval_ms; /* time between repeats once it's repeating -- lower = faster */
    SDL_bool show_console;      /* SDL_FALSE hides the console window Windows opens alongside the app */
    char launchbox_dir[CONFIG_LAUNCHBOX_DIR_MAX]; /* LaunchBox install root, empty = not configured */
} AppConfig;

/* Reads `path` (INI format) into `out`. Logs what was loaded, what was
   defaulted, and why. Never fails -- a missing/broken file just means
   `out` comes back as all defaults. */
void config_load(const char *path, AppConfig *out);

#endif /* CRT_CONFIG_H */
