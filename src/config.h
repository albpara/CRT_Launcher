#ifndef CRT_CONFIG_H
#define CRT_CONFIG_H

#include <SDL.h>
#include <stddef.h>

/* Settings loaded from config.ini. Every field always ends up with a valid
   value -- config_load() fills in defaults for anything missing, unparsable,
   or if the file can't be opened at all. */
#define CONFIG_LAUNCHBOX_DIR_MAX 512
/* Room for a comma-separated list of every platform LaunchBox might have
   (each up to LAUNCHBOX_PLATFORM_MAX in launchbox.h) -- generous, matching
   this app's "config.ini is a few KB at most" assumption elsewhere. */
#define CONFIG_SELECTED_PLATFORMS_MAX 2048
/* Room for the exe's own directory (CONFIG_LAUNCHBOX_DIR_MAX) plus
   "\config.ini" -- see config_resolve_default_path(). */
#define CONFIG_DEFAULT_PATH_MAX (CONFIG_LAUNCHBOX_DIR_MAX + 32)

/* One physical input a bindable action can be tied to. Deliberately
   source-agnostic (keyboard OR joystick) -- main.c resolves whichever one
   this is against the real-time keyboard/joystick state each frame (see
   binding_is_held there), so the rest of the app (gamelist.c, render.c)
   never has to care whether the cabinet is being driven by a keyboard or a
   JPAC/XInput/etc. encoder. Axis-based directions ARE supported (unlike an
   earlier assumption here that real cabinet joysticks always report
   digitally) -- confirmed necessary in practice: a GP2040 encoder in
   XInput mode reported its directions as a hat when tested on one cabinet,
   but as an analog axis on another. */
typedef enum {
    INPUT_BINDING_NONE = 0,
    INPUT_BINDING_KEYBOARD,
    INPUT_BINDING_JOY_BUTTON,
    INPUT_BINDING_JOY_HAT,
    INPUT_BINDING_JOY_AXIS,
} InputBindingType;

typedef struct {
    InputBindingType type;
    SDL_Keycode key;         /* INPUT_BINDING_KEYBOARD */
    int joy_button;          /* INPUT_BINDING_JOY_BUTTON -- raw SDL_Joystick button index */
    int joy_hat;              /* INPUT_BINDING_JOY_HAT -- which hat (usually 0) */
    Uint8 joy_hat_direction;  /* INPUT_BINDING_JOY_HAT -- one of SDL_HAT_UP/DOWN/LEFT/RIGHT */
    int joy_axis;              /* INPUT_BINDING_JOY_AXIS -- raw SDL_Joystick axis index */
    int joy_axis_direction;    /* INPUT_BINDING_JOY_AXIS -- +1 (push positive) or -1 (push negative) */
} InputBinding;

/* The full set of actions the app needs bound to something -- deliberately
   small and flat, no modifier combos baked into the list itself. SELECT
   and MODIFIER are separate so "hold MODIFIER, press SELECT" can mean
   "open the version picker" while plain SELECT means "launch" (see
   main.c) -- the cabinet only needs 3 buttons total (SELECT, BACK,
   MODIFIER) plus the 4 directions, not one dedicated button per action. */
typedef enum {
    INPUT_ACTION_UP = 0,
    INPUT_ACTION_DOWN,
    INPUT_ACTION_LEFT,
    INPUT_ACTION_RIGHT,
    INPUT_ACTION_SELECT,
    INPUT_ACTION_BACK,
    INPUT_ACTION_MODIFIER,
    INPUT_ACTION_COUNT
} InputAction;

/* Display name (for the calibration prompt/logging) and config.ini key
   name (lowercase, under [bindings]) for each InputAction, indexed the
   same way. Defined in config.c. */
extern const char *const INPUT_ACTION_NAMES[INPUT_ACTION_COUNT];
extern const char *const INPUT_ACTION_CONFIG_KEYS[INPUT_ACTION_COUNT];

/* Formats `b` the same way config.ini stores it (e.g. "KEYBOARD Return",
   "JOYBUTTON 3", "JOYHAT 0 UP", "NONE") -- used both for writing config.ini
   and for logging what the calibration flow just captured. */
void input_binding_to_string(const InputBinding *b, char *out, size_t out_cap);

typedef struct {
    int width;
    int height;
    int refresh_rate;      /* Hz. 0 means "any refresh rate is fine". */
    /* How long (ms) with no input before the screen blanks to solid black
       -- CRT burn-in protection, since the game list is otherwise a
       static, fairly bright image that could sit unchanged for hours on
       a cabinet nobody's using. 0 disables it entirely. Any held/pressed
       input wakes it immediately; see main.c for how that wake is kept
       from ALSO acting on the list underneath (the same class of problem
       prime_edges_held solves for calibration). Milliseconds internally
       (matches SDL_GetTicks(), what main.c actually compares this
       against) even though config.ini's screensaver_timeout_seconds key
       is in whole seconds -- config_load() does the x1000 conversion. */
    int screensaver_timeout_ms;
    SDL_Keycode toggle_hotkey;
    /* Up/Down/Left/Right navigation repeat, applied by main.c's own timer
       (SDL_GetTicks-based, not the OS's key-repeat rate -- see main.c) so
       it's actually tunable here instead of stuck at whatever Windows'
       keyboard repeat setting happens to be. */
    int nav_repeat_delay_ms;    /* how long a direction must be held before it starts repeating */
    int nav_repeat_interval_ms; /* time between repeats once it's repeating -- lower = faster */
    /* Indexed by InputAction. Defaults (if config.ini has no [bindings]
       section, or a given key is missing/unparsable) reproduce the app's
       original hardcoded keyboard-only behavior exactly -- Up/Down/Left/
       Right arrows, Enter for SELECT, Escape for BACK, Left Shift for
       MODIFIER -- so an uncalibrated install behaves like before this
       system existed. Overwritten wholesale by the in-app calibration flow
       (system menu > CALIBRATE CONTROLS, see main.c) and persisted via
       config_save_bindings(). */
    InputBinding bindings[INPUT_ACTION_COUNT];
    /* SDL_TRUE if config.ini's [bindings] section actually had at least one
       real binding in it -- SDL_FALSE means `bindings` above is entirely
       the hardcoded keyboard defaults, i.e. calibration has never been run
       on this install. main.c uses this to drop straight into the
       calibration flow on first launch instead of the game list. */
    SDL_bool bindings_calibrated;
    /* LaunchBox install root. If config.ini leaves this blank, config_load
       falls back to checking for a LaunchBox install as a sibling of this
       exe's own folder (e.g. "Cabinet\CRT Launcher\" next to
       "Cabinet\LaunchBox\") before giving up -- still empty here only if
       neither an explicit setting nor that fallback found one. */
    char launchbox_dir[CONFIG_LAUNCHBOX_DIR_MAX];
    /* Raw value of config.ini's [launchbox] selected_platforms key --
       "All" (default), "None", or a comma-separated list of platform
       names (e.g. "Arcade,SNES"), matched case-insensitively against
       whatever LaunchboxInfo.platform_names the scan actually found.
       Kept as unparsed text here since config_load runs before
       launchbox_load and has no platform list yet to validate against --
       see gamelist_init/gamelist_toggle_platform in gamelist.c for where
       this actually gets resolved and re-persisted. */
    char selected_platforms[CONFIG_SELECTED_PLATFORMS_MAX];
} AppConfig;

/* Resolves `out` to "config.ini" next to this exe's own file (via
   GetModuleFileNameA on Windows), NOT relative to the process's current
   working directory -- CWD cannot be assumed to be the exe's own folder.
   This matters in practice: launched via a plain double-click or a
   shortcut with an explicit "Start in", CWD happens to match, but
   launched via a Windows-startup Run key entry (see startup.c) it does
   not, which broke config.ini discovery entirely on an auto-launched
   install before this existed. Falls back to the bare relative
   "config.ini" if the exe's own path can't be resolved (non-Windows, or
   GetModuleFileNameA fails) -- same behavior as before this function
   existed. Call once at startup and reuse the result for every
   config_load/config_save_* call in the process, the way main.c does. */
void config_resolve_default_path(char *out, size_t out_cap);

/* Reads `path` (INI format) into `out`. Logs what was loaded, what was
   defaulted, and why. Never fails -- a missing/broken file just means
   `out` comes back as all defaults, EXCEPT launchbox_dir, which still
   goes through sibling-folder auto-detection same as a real file that
   just left the key blank would (a truly bare install -- e.g. only
   crt_launcher.exe copied somewhere -- can still find an adjacent
   LaunchBox folder this way). If `path` didn't exist at all, a fresh
   default config.ini is written there afterward so it exists (and is
   editable) for next time. */
void config_load(const char *path, AppConfig *out);

/* Rewrites just the [bindings] section of `path` to reflect `bindings`,
   leaving every other section/comment in the file untouched. If the file
   doesn't have a [bindings] section yet, appends one; if it does, replaces
   that whole section (so hand-edits inside it don't survive a
   recalibration, but everything outside it does). Returns SDL_FALSE if the
   file couldn't be written. Called once, after a full calibration pass
   completes -- see main.c. */
SDL_bool config_save_bindings(const char *path, const InputBinding bindings[INPUT_ACTION_COUNT]);

/* Rewrites just the [launchbox] section's `selected_platforms=` line in
   `path` to `value` (as produced by gamelist_format_platform_selection:
   "All", "None", or a comma-separated platform name list), leaving
   everything else in the file untouched. Appends the line right after the
   [launchbox] header if it isn't present yet, or appends a whole new
   [launchbox] section if even that header is missing. Same in-memory
   rewrite approach as config_save_bindings, for the same CRLF-safety
   reason -- see its comment. Called every time a platform's toggled in
   the system menu's platform list, so the choice survives a restart. */
SDL_bool config_save_selected_platforms(const char *path, const char *value);

#endif /* CRT_CONFIG_H */
