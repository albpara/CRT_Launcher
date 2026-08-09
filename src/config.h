#ifndef CRT_CONFIG_H
#define CRT_CONFIG_H

#include <SDL.h>
#include <stddef.h>

#define CONFIG_LAUNCHBOX_DIR_MAX 512
/* Room for a comma-separated list of every platform name. */
#define CONFIG_SELECTED_PLATFORMS_MAX 2048
/* Exe directory plus "\config.ini" -- see config_resolve_default_path(). */
#define CONFIG_DEFAULT_PATH_MAX (CONFIG_LAUNCHBOX_DIR_MAX + 32)

/* One physical input an action can be bound to. Source-agnostic (keyboard
   or joystick); main.c resolves it against real-time input state each
   frame. Axis directions are needed in practice: the same GP2040 encoder
   reported a hat on one cabinet and an analog axis on another. */
typedef enum {
    INPUT_BINDING_NONE = 0,
    INPUT_BINDING_KEYBOARD,
    INPUT_BINDING_JOY_BUTTON,
    INPUT_BINDING_JOY_HAT,
    INPUT_BINDING_JOY_AXIS,
} InputBindingType;

typedef struct {
    InputBindingType type;
    SDL_Keycode key;          /* KEYBOARD */
    int joy_button;           /* JOY_BUTTON -- raw button index */
    int joy_hat;              /* JOY_HAT -- which hat (usually 0) */
    Uint8 joy_hat_direction;  /* JOY_HAT -- SDL_HAT_UP/DOWN/LEFT/RIGHT */
    int joy_axis;             /* JOY_AXIS -- raw axis index */
    int joy_axis_direction;   /* JOY_AXIS -- +1 or -1 */
} InputBinding;

/* The cabinet needs 3 buttons (SELECT, BACK, MODIFIER) plus 4 directions.
   MODIFIER+SELECT opens the version picker; plain SELECT launches. */
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

/* Display names (calibration prompts/logs) and config.ini key names,
   indexed by InputAction. Defined in config.c. */
extern const char *const INPUT_ACTION_NAMES[INPUT_ACTION_COUNT];
extern const char *const INPUT_ACTION_CONFIG_KEYS[INPUT_ACTION_COUNT];

/* Formats `b` the way config.ini stores it (e.g. "KEYBOARD Return",
   "JOYHAT 0 UP", "NONE"). */
void input_binding_to_string(const InputBinding *b, char *out, size_t out_cap);

/* What's drawn behind the game list. */
typedef enum {
    BACKGROUND_STARFIELD = 0,
    BACKGROUND_CHECKERBOARD,
} BackgroundStyle;

/* Every field ends up valid -- config_load() fills in defaults for
   anything missing or unparsable. */
typedef struct {
    int width;
    int height;
    int refresh_rate;      /* Hz; 0 = any */
    BackgroundStyle background;
    /* Idle ms before blanking to black (CRT burn-in protection). 0
       disables. Stored in ms (matches SDL_GetTicks); config.ini's key is
       screensaver_timeout_seconds, converted on load. */
    int screensaver_timeout_ms;
    SDL_Keycode toggle_hotkey;
    /* Nav repeat, timed by main.c itself rather than the OS key-repeat
       rate, so it's tunable and works for joysticks too. */
    int nav_repeat_delay_ms;
    int nav_repeat_interval_ms;
    /* Indexed by InputAction. Defaults are keyboard-only (arrows, Enter,
       Escape, Left Shift); overwritten wholesale by calibration. */
    InputBinding bindings[INPUT_ACTION_COUNT];
    /* SDL_FALSE = config.ini had no real [bindings] -- main.c auto-starts
       calibration on such an install. */
    SDL_bool bindings_calibrated;
    /* LaunchBox install root. Blank in config.ini triggers sibling-folder
       auto-detection (see config.c); still empty only if that failed too. */
    char launchbox_dir[CONFIG_LAUNCHBOX_DIR_MAX];
    /* Raw selected_platforms value: "All", "None", or a comma-separated
       name list. Resolved against real platform names in gamelist.c. */
    char selected_platforms[CONFIG_SELECTED_PLATFORMS_MAX];
} AppConfig;

/* Resolves "config.ini" next to the exe's own file (GetModuleFileNameA),
   NOT the current working directory -- a Windows-startup Run key launch
   doesn't preserve the exe's folder as CWD. Call once, reuse everywhere. */
void config_resolve_default_path(char *out, size_t out_cap);

/* Reads `path` into `out`. Never fails: a missing/broken file yields
   defaults, launchbox_dir auto-detection still runs, and if the file was
   absent a fresh default config.ini is written for next time. */
void config_load(const char *path, AppConfig *out);

/* Rewrites just the [bindings] section of `path`, leaving everything else
   untouched. Called after a full calibration pass. */
SDL_bool config_save_bindings(const char *path, const InputBinding bindings[INPUT_ACTION_COUNT]);

/* Rewrites just the selected_platforms= line (adding it, or a [launchbox]
   section, if missing). Called on every platform toggle. */
SDL_bool config_save_selected_platforms(const char *path, const char *value);

#endif /* CRT_CONFIG_H */
