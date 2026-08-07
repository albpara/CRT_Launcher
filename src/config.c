#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define CONFIG_DEFAULT_WIDTH  320
#define CONFIG_DEFAULT_HEIGHT 240
#define CONFIG_DEFAULT_REFRESH 60
#define CONFIG_DEFAULT_HOTKEY SDLK_F5
#define CONFIG_DEFAULT_NAV_REPEAT_DELAY_MS 250
#define CONFIG_DEFAULT_NAV_REPEAT_INTERVAL_MS 40

#define CONFIG_MAX_LINE 256
#define CONFIG_MAX_SECTION 64

const char *const INPUT_ACTION_NAMES[INPUT_ACTION_COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT", "SELECT", "BACK", "MODIFIER",
};
const char *const INPUT_ACTION_CONFIG_KEYS[INPUT_ACTION_COUNT] = {
    "up", "down", "left", "right", "select", "back", "modifier",
};

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return s;
}

static SDL_Keycode parse_hotkey(const char *name) {
    SDL_Keycode kc = SDL_GetKeyFromName(name);
    if (kc == SDLK_UNKNOWN) {
        SDL_Log("[config] Unrecognized toggle_hotkey '%s', defaulting to F5", name);
        return CONFIG_DEFAULT_HOTKEY;
    }
    return kc;
}

static InputBinding make_keyboard_binding(SDL_Keycode key) {
    InputBinding b;
    memset(&b, 0, sizeof(b));
    b.type = INPUT_BINDING_KEYBOARD;
    b.key = key;
    return b;
}

/* Parses one [bindings] value, e.g. "KEYBOARD Left Shift", "JOYBUTTON 3",
   "JOYHAT 0 UP", or "JOYAXIS 1 NEGATIVE". Returns an INPUT_BINDING_NONE
   binding (rather than touching *out*) on anything unrecognized, so a
   malformed line just leaves the action at whatever default config_load
   already set instead of unbinding it. */
static InputBinding parse_input_binding(const char *value) {
    InputBinding b;
    memset(&b, 0, sizeof(b));

    char kind[16];
    int consumed = 0;
    if (sscanf(value, "%15s%n", kind, &consumed) != 1) {
        return b;
    }

    if (strcmp(kind, "KEYBOARD") == 0) {
        const char *name = value + consumed;
        while (isspace((unsigned char)*name)) {
            name++;
        }
        SDL_Keycode kc = SDL_GetKeyFromName(name);
        if (kc != SDLK_UNKNOWN) {
            b.type = INPUT_BINDING_KEYBOARD;
            b.key = kc;
        }
    } else if (strcmp(kind, "JOYBUTTON") == 0) {
        int n;
        if (sscanf(value + consumed, "%d", &n) == 1 && n >= 0) {
            b.type = INPUT_BINDING_JOY_BUTTON;
            b.joy_button = n;
        }
    } else if (strcmp(kind, "JOYHAT") == 0) {
        int hat;
        char dir[16];
        if (sscanf(value + consumed, "%d %15s", &hat, dir) == 2 && hat >= 0) {
            Uint8 d = 0;
            if (strcmp(dir, "UP") == 0) { d = SDL_HAT_UP; }
            else if (strcmp(dir, "DOWN") == 0) { d = SDL_HAT_DOWN; }
            else if (strcmp(dir, "LEFT") == 0) { d = SDL_HAT_LEFT; }
            else if (strcmp(dir, "RIGHT") == 0) { d = SDL_HAT_RIGHT; }
            if (d != 0) {
                b.type = INPUT_BINDING_JOY_HAT;
                b.joy_hat = hat;
                b.joy_hat_direction = d;
            }
        }
    } else if (strcmp(kind, "JOYAXIS") == 0) {
        int axis;
        char dir[16];
        if (sscanf(value + consumed, "%d %15s", &axis, dir) == 2 && axis >= 0) {
            int d = 0;
            if (strcmp(dir, "POSITIVE") == 0) { d = 1; }
            else if (strcmp(dir, "NEGATIVE") == 0) { d = -1; }
            if (d != 0) {
                b.type = INPUT_BINDING_JOY_AXIS;
                b.joy_axis = axis;
                b.joy_axis_direction = d;
            }
        }
    }

    return b;
}

void input_binding_to_string(const InputBinding *b, char *out, size_t out_cap) {
    switch (b->type) {
        case INPUT_BINDING_KEYBOARD:
            snprintf(out, out_cap, "KEYBOARD %s", SDL_GetKeyName(b->key));
            break;
        case INPUT_BINDING_JOY_BUTTON:
            snprintf(out, out_cap, "JOYBUTTON %d", b->joy_button);
            break;
        case INPUT_BINDING_JOY_HAT: {
            const char *dir = "CENTERED";
            if (b->joy_hat_direction == SDL_HAT_UP) { dir = "UP"; }
            else if (b->joy_hat_direction == SDL_HAT_DOWN) { dir = "DOWN"; }
            else if (b->joy_hat_direction == SDL_HAT_LEFT) { dir = "LEFT"; }
            else if (b->joy_hat_direction == SDL_HAT_RIGHT) { dir = "RIGHT"; }
            snprintf(out, out_cap, "JOYHAT %d %s", b->joy_hat, dir);
            break;
        }
        case INPUT_BINDING_JOY_AXIS:
            snprintf(out, out_cap, "JOYAXIS %d %s", b->joy_axis, b->joy_axis_direction > 0 ? "POSITIVE" : "NEGATIVE");
            break;
        case INPUT_BINDING_NONE:
        default:
            snprintf(out, out_cap, "NONE");
            break;
    }
}

#ifdef _WIN32
/* SDL_TRUE if `dir\Data\Platforms` exists and is a real directory -- the
   same check launchbox.c itself does before trusting a launchbox_dir, used
   here too so auto-detection can't succeed on a folder that merely happens
   to be named right. */
static SDL_bool looks_like_launchbox_install(const char *dir) {
    char platforms_dir[CONFIG_LAUNCHBOX_DIR_MAX];
    snprintf(platforms_dir, sizeof(platforms_dir), "%s\\Data\\Platforms", dir);
    DWORD attrs = GetFileAttributesA(platforms_dir);
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) ? SDL_TRUE : SDL_FALSE;
}

/* Falls back to a LaunchBox install living as a sibling of this exe's own
   folder (e.g. "Cabinet\CRT Launcher\crt_launcher.exe" next to
   "Cabinet\LaunchBox\") -- a natural layout for something meant to replace
   BigBox on the same machine. Deliberately based on the exe's own
   directory (GetModuleFileNameA), not the current working directory --
   CWD depends on how the app was launched (a shortcut's "Start in" field,
   a script, etc.) and isn't reliable for "sibling of the exe" the way the
   exe's own path is. Only ever consulted when config.ini leaves
   launchbox_dir blank, so it never overrides an explicit setting. Returns
   SDL_TRUE and fills `out` only if a real-looking install was found. */
static SDL_bool autodetect_launchbox_dir(char *out, size_t out_cap) {
    char exe_path[CONFIG_LAUNCHBOX_DIR_MAX];
    DWORD len = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
    if (len == 0 || len >= sizeof(exe_path)) {
        return SDL_FALSE;
    }

    char *last_sep = strrchr(exe_path, '\\');
    if (!last_sep) {
        return SDL_FALSE;
    }
    *last_sep = '\0'; /* exe_path is now the exe's own directory */

    char candidate[CONFIG_LAUNCHBOX_DIR_MAX];
    snprintf(candidate, sizeof(candidate), "%s\\..\\LaunchBox", exe_path);

    if (!looks_like_launchbox_install(candidate)) {
        return SDL_FALSE;
    }

    snprintf(out, out_cap, "%s", candidate);
    return SDL_TRUE;
}
#endif /* _WIN32 */

void config_load(const char *path, AppConfig *out) {
    out->width = CONFIG_DEFAULT_WIDTH;
    out->height = CONFIG_DEFAULT_HEIGHT;
    out->refresh_rate = CONFIG_DEFAULT_REFRESH;
    out->toggle_hotkey = CONFIG_DEFAULT_HOTKEY;
    out->nav_repeat_delay_ms = CONFIG_DEFAULT_NAV_REPEAT_DELAY_MS;
    out->nav_repeat_interval_ms = CONFIG_DEFAULT_NAV_REPEAT_INTERVAL_MS;
    out->bindings[INPUT_ACTION_UP] = make_keyboard_binding(SDLK_UP);
    out->bindings[INPUT_ACTION_DOWN] = make_keyboard_binding(SDLK_DOWN);
    out->bindings[INPUT_ACTION_LEFT] = make_keyboard_binding(SDLK_LEFT);
    out->bindings[INPUT_ACTION_RIGHT] = make_keyboard_binding(SDLK_RIGHT);
    out->bindings[INPUT_ACTION_SELECT] = make_keyboard_binding(SDLK_RETURN);
    out->bindings[INPUT_ACTION_BACK] = make_keyboard_binding(SDLK_ESCAPE);
    out->bindings[INPUT_ACTION_MODIFIER] = make_keyboard_binding(SDLK_LSHIFT);
    out->bindings_calibrated = SDL_FALSE;
    out->launchbox_dir[0] = '\0';

    FILE *f = fopen(path, "r");
    if (!f) {
        SDL_Log("[config] Could not open '%s' (using built-in defaults: %dx%d@%dHz, hotkey=%s)",
                path, out->width, out->height, out->refresh_rate,
                SDL_GetKeyName(out->toggle_hotkey));
        return;
    }

    char line[CONFIG_MAX_LINE];
    char section[CONFIG_MAX_SECTION] = "";

    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);

        if (*s == '\0' || *s == ';' || *s == '#') {
            continue; /* blank line or comment */
        }

        if (*s == '[') {
            char *close = strchr(s, ']');
            if (close) {
                *close = '\0';
                snprintf(section, sizeof(section), "%s", s + 1);
            }
            continue;
        }

        char *eq = strchr(s, '=');
        if (!eq) {
            continue; /* not a key=value line, ignore */
        }
        *eq = '\0';
        char *key = trim(s);
        char *value = trim(eq + 1);
        if (*key == '\0') {
            continue;
        }

        if (strcmp(section, "display") == 0) {
            if (strcmp(key, "width") == 0) {
                out->width = atoi(value);
            } else if (strcmp(key, "height") == 0) {
                out->height = atoi(value);
            } else if (strcmp(key, "refresh_rate") == 0) {
                out->refresh_rate = atoi(value);
            }
        } else if (strcmp(section, "input") == 0) {
            if (strcmp(key, "toggle_hotkey") == 0) {
                out->toggle_hotkey = parse_hotkey(value);
            } else if (strcmp(key, "nav_repeat_delay_ms") == 0) {
                out->nav_repeat_delay_ms = atoi(value);
            } else if (strcmp(key, "nav_repeat_interval_ms") == 0) {
                out->nav_repeat_interval_ms = atoi(value);
            }
        } else if (strcmp(section, "bindings") == 0) {
            for (int a = 0; a < INPUT_ACTION_COUNT; a++) {
                if (strcmp(key, INPUT_ACTION_CONFIG_KEYS[a]) == 0) {
                    InputBinding parsed = parse_input_binding(value);
                    if (parsed.type != INPUT_BINDING_NONE) {
                        out->bindings[a] = parsed;
                        out->bindings_calibrated = SDL_TRUE;
                    } else {
                        SDL_Log("[config] Unrecognized binding '%s' for '%s', keeping default", value, key);
                    }
                    break;
                }
            }
        } else if (strcmp(section, "launchbox") == 0) {
            if (strcmp(key, "launchbox_dir") == 0) {
                snprintf(out->launchbox_dir, sizeof(out->launchbox_dir), "%s", value);
            }
        }
    }

    fclose(f);

    if (out->width <= 0 || out->height <= 0) {
        SDL_Log("[config] Invalid width/height in '%s', falling back to %dx%d",
                path, CONFIG_DEFAULT_WIDTH, CONFIG_DEFAULT_HEIGHT);
        out->width = CONFIG_DEFAULT_WIDTH;
        out->height = CONFIG_DEFAULT_HEIGHT;
    }
    if (out->refresh_rate < 0) {
        out->refresh_rate = 0;
    }
    if (out->nav_repeat_delay_ms < 0) {
        out->nav_repeat_delay_ms = CONFIG_DEFAULT_NAV_REPEAT_DELAY_MS;
    }
    if (out->nav_repeat_interval_ms <= 0) {
        out->nav_repeat_interval_ms = CONFIG_DEFAULT_NAV_REPEAT_INTERVAL_MS;
    }

#ifdef _WIN32
    if (!out->launchbox_dir[0]) {
        char detected[CONFIG_LAUNCHBOX_DIR_MAX];
        if (autodetect_launchbox_dir(detected, sizeof(detected))) {
            SDL_Log("[config] launchbox_dir not set -- found a sibling LaunchBox install at '%s', using it",
                    detected);
            snprintf(out->launchbox_dir, sizeof(out->launchbox_dir), "%s", detected);
        }
    }
#endif

    SDL_Log("[config] Loaded '%s': low-res mode = %dx%d@%dHz, toggle hotkey = %s, "
            "nav repeat = %dms delay / %dms interval",
            path, out->width, out->height, out->refresh_rate,
            SDL_GetKeyName(out->toggle_hotkey), out->nav_repeat_delay_ms, out->nav_repeat_interval_ms);
    SDL_Log("[config] LaunchBox dir = %s",
            out->launchbox_dir[0] ? out->launchbox_dir : "(not configured)");

    for (int a = 0; a < INPUT_ACTION_COUNT; a++) {
        char value[64];
        input_binding_to_string(&out->bindings[a], value, sizeof(value));
        SDL_Log("[config] Binding %s = %s", INPUT_ACTION_NAMES[a], value);
    }
}

/* SDL_TRUE if `p` (which must point at a '[' inside `data`) is genuinely a
   section-header start -- i.e. either the very first byte of the file, or
   immediately preceded by a newline -- rather than a stray '[' inside a
   value or comment. */
static SDL_bool is_line_start_bracket(const char *data, const char *p) {
    return (p == data || *(p - 1) == '\n') ? SDL_TRUE : SDL_FALSE;
}

SDL_bool config_save_bindings(const char *path, const InputBinding bindings[INPUT_ACTION_COUNT]) {
    /* Read the existing file (if any) so everything outside [bindings] can
       be carried over untouched -- this app's own config.ini has a lot of
       hand-written documentation in it that a naive "regenerate from
       AppConfig" save would destroy. Binary mode on both ends (here and
       the write below) deliberately -- Windows' text-mode CRT translates
       every '\n' passing through a text-mode stream into "\r\n", including
       ones already read verbatim out of a CRLF file, which double-inserts
       carriage returns on a read-then-write round trip. Binary mode keeps
       bytes exactly as read/written, at the cost of this function's own
       freshly-generated lines being LF-only even inside an otherwise CRLF
       file -- purely cosmetic, every reader here (config_load, a text
       editor) handles bare LF fine. */
    char *data = NULL;
    long len = 0;
    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len > 0) {
            data = (char *)malloc((size_t)len + 1);
            if (data) {
                size_t read = fread(data, 1, (size_t)len, f);
                data[read] = '\0';
                len = (long)read;
            }
        }
        fclose(f);
    }

    char section[2048];
    int off = snprintf(section, sizeof(section),
        "[bindings]\n"
        "; Machine-managed by the in-app calibration (scroll up past\n"
        "; favorites to CALIBRATE CONTROLS, press SELECT) -- running it\n"
        "; again overwrites this whole section. Hand-editing is fine too,\n"
        "; same format: KEYBOARD <key name> | JOYBUTTON <index> |\n"
        "; JOYHAT <hat> <UP|DOWN|LEFT|RIGHT> | JOYAXIS <axis> <POSITIVE|NEGATIVE>\n");
    for (int a = 0; a < INPUT_ACTION_COUNT && off > 0 && off < (int)sizeof(section); a++) {
        char value[64];
        input_binding_to_string(&bindings[a], value, sizeof(value));
        off += snprintf(section + off, sizeof(section) - (size_t)off, "%s=%s\n", INPUT_ACTION_CONFIG_KEYS[a], value);
    }

    /* Assembled entirely in memory first (config.ini is a few KB at most)
       so the "is there a trailing blank line" check below can just look at
       the buffer -- no seeking/reading back through a write-only stream,
       which isn't valid. */
    size_t result_cap = (size_t)(len > 0 ? len : 0) + strlen(section) + 16;
    char *result = (char *)malloc(result_cap);
    if (!result) {
        SDL_Log("[config] WARNING: out of memory saving '%s', calibration not saved", path);
        free(data);
        return SDL_FALSE;
    }
    size_t result_len = 0;

    if (data) {
        /* Copy everything EXCEPT any existing "[bindings]"-headed
           section(s) -- looping (not just handling one) so that if a prior
           bug or hand-edit ever left more than one behind, this cleans all
           of them up rather than adding yet another. */
        const char *p = data;
        while (*p) {
            const char *tag = strstr(p, "[bindings]");
            if (!tag || !is_line_start_bracket(data, tag)) {
                size_t n = strlen(p);
                memcpy(result + result_len, p, n);
                result_len += n;
                break;
            }

            size_t n = (size_t)(tag - p);
            memcpy(result + result_len, p, n);
            result_len += n;

            const char *next_section = NULL;
            for (const char *q = tag + 1; *q; q++) {
                if (*q == '\n' && *(q + 1) == '[') {
                    next_section = q + 1;
                    break;
                }
            }
            p = next_section ? next_section : (tag + strlen(tag));
        }
    }

    /* Exactly one blank line before the freshly written section. */
    if (result_len > 0 && result[result_len - 1] != '\n') {
        result[result_len++] = '\n';
    }
    if (result_len > 0) {
        result[result_len++] = '\n';
    }
    memcpy(result + result_len, section, strlen(section));
    result_len += strlen(section);

    FILE *out = fopen(path, "wb");
    if (!out) {
        SDL_Log("[config] WARNING: could not open '%s' for writing, calibration not saved", path);
        free(data);
        free(result);
        return SDL_FALSE;
    }
    fwrite(result, 1, result_len, out);
    fclose(out);

    free(data);
    free(result);
    SDL_Log("[config] Saved calibrated bindings to '%s'", path);
    return SDL_TRUE;
}
