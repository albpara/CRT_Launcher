#include "config.h"

/* Baked in by CMake; see render.c for the same fallback. */
#ifndef CRT_LAUNCHER_VERSION
#define CRT_LAUNCHER_VERSION "dev"
#endif

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
#define CONFIG_DEFAULT_SCREENSAVER_TIMEOUT_MS 60000

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

/* Parses one [bindings] value ("KEYBOARD Left Shift", "JOYBUTTON 3",
   "JOYHAT 0 UP", "JOYAXIS 1 NEGATIVE"). Returns INPUT_BINDING_NONE on
   anything unrecognized so a malformed line keeps the default. */
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
/* Same "<dir>\Data\Platforms exists" check launchbox.c trusts. */
static SDL_bool looks_like_launchbox_install(const char *dir) {
    char platforms_dir[CONFIG_LAUNCHBOX_DIR_MAX];
    snprintf(platforms_dir, sizeof(platforms_dir), "%s\\Data\\Platforms", dir);
    DWORD attrs = GetFileAttributesA(platforms_dir);
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) ? SDL_TRUE : SDL_FALSE;
}

/* Exe's own directory via GetModuleFileNameA -- NOT the CWD, which a
   Windows-startup Run key launch sets to e.g. C:\Windows\System32. */
static SDL_bool get_exe_directory(char *out, size_t out_cap) {
    char exe_path[CONFIG_LAUNCHBOX_DIR_MAX];
    DWORD len = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
    if (len == 0 || len >= sizeof(exe_path)) {
        return SDL_FALSE;
    }

    char *last_sep = strrchr(exe_path, '\\');
    if (!last_sep) {
        return SDL_FALSE;
    }
    *last_sep = '\0';

    snprintf(out, out_cap, "%s", exe_path);
    return SDL_TRUE;
}

/* Looks for a LaunchBox install next to the exe's own folder. Only
   consulted when launchbox_dir is blank, so it never overrides an
   explicit setting. */
static SDL_bool autodetect_launchbox_dir(char *out, size_t out_cap) {
    char exe_dir[CONFIG_LAUNCHBOX_DIR_MAX];
    if (!get_exe_directory(exe_dir, sizeof(exe_dir))) {
        return SDL_FALSE;
    }

    char candidate[CONFIG_LAUNCHBOX_DIR_MAX];
    snprintf(candidate, sizeof(candidate), "%s\\..\\LaunchBox", exe_dir);

    if (!looks_like_launchbox_install(candidate)) {
        return SDL_FALSE;
    }

    snprintf(out, out_cap, "%s", candidate);
    return SDL_TRUE;
}
#endif /* _WIN32 */

void config_resolve_default_path(char *out, size_t out_cap) {
#ifdef _WIN32
    char exe_dir[CONFIG_LAUNCHBOX_DIR_MAX];
    if (get_exe_directory(exe_dir, sizeof(exe_dir))) {
        snprintf(out, out_cap, "%s\\config.ini", exe_dir);
        return;
    }
#endif
    snprintf(out, out_cap, "config.ini");
}

/* Minimal bootstrap config written when none exists, so a bare exe copy
   self-heals into an editable file. [bindings] stays absent (triggers
   first-launch calibration) and launchbox_dir blank (keeps auto-detection
   live). Deliberately not a copy of the documented repo config.ini. */
static void config_write_default_file(const char *path) {
    static const char DEFAULT_CONTENT[] =
        "; CRT Launcher " CRT_LAUNCHER_VERSION " configuration -- auto-generated\n"
        "; because none was found next to the exe. Edit values, then just\n"
        "; relaunch the app -- no rebuild needed. The version above records\n"
        "; which build wrote this file, not which one reads it.\n"
        "\n"
        "[display]\n"
        "width=320\n"
        "height=240\n"
        "refresh_rate=60\n"
        "; Possible values: checkerboard and starfield\n"
        "background=checkerboard\n"
        "; Possible values: compact and galaga88\n"
        "font=compact\n"
        "screensaver_timeout_seconds=60\n"
        "\n"
        "[input]\n"
        "toggle_hotkey=F5\n"
        "nav_repeat_delay_ms=250\n"
        "nav_repeat_interval_ms=40\n"
        "\n"
        "[launchbox]\n"
        "; Leave blank to auto-detect a LaunchBox install as a sibling of\n"
        "; this exe's own folder; set explicitly if it lives somewhere else.\n"
        "launchbox_dir=\n"
        "selected_platforms=All\n";

    FILE *f = fopen(path, "wb");
    if (!f) {
        SDL_Log("[config] WARNING: could not create default '%s'", path);
        return;
    }
    fwrite(DEFAULT_CONTENT, 1, sizeof(DEFAULT_CONTENT) - 1, f);
    fclose(f);
    SDL_Log("[config] Created default '%s'", path);
}

void config_load(const char *path, AppConfig *out) {
    out->width = CONFIG_DEFAULT_WIDTH;
    out->height = CONFIG_DEFAULT_HEIGHT;
    out->refresh_rate = CONFIG_DEFAULT_REFRESH;
    out->background = BACKGROUND_CHECKERBOARD;
    out->font = FONT_STYLE_COMPACT;
    out->screensaver_timeout_ms = CONFIG_DEFAULT_SCREENSAVER_TIMEOUT_MS;
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
    snprintf(out->selected_platforms, sizeof(out->selected_platforms), "All");

    SDL_bool file_found = SDL_TRUE;

    FILE *f = fopen(path, "r");
    if (!f) {
        /* No early return: launchbox_dir auto-detection below must still
           run, and the missing file gets created afterward. */
        file_found = SDL_FALSE;
        SDL_Log("[config] '%s' not found -- using built-in defaults for now", path);
    } else {
        char line[CONFIG_MAX_LINE];
        char section[CONFIG_MAX_SECTION] = "";

        while (fgets(line, sizeof(line), f)) {
            char *s = trim(line);

            if (*s == '\0' || *s == ';' || *s == '#') {
                continue;
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
                continue;
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
                } else if (strcmp(key, "screensaver_timeout_seconds") == 0) {
                    out->screensaver_timeout_ms = atoi(value) * 1000;
                } else if (strcmp(key, "background") == 0) {
                    if (SDL_strcasecmp(value, "checkerboard") == 0) {
                        out->background = BACKGROUND_CHECKERBOARD;
                    } else if (SDL_strcasecmp(value, "starfield") == 0) {
                        out->background = BACKGROUND_STARFIELD;
                    } else {
                        /* Fall back rather than keep whatever was parsed
                           before, so a bad value always lands on the
                           default. */
                        out->background = BACKGROUND_CHECKERBOARD;
                        SDL_Log("[config] Unrecognized background '%s', using checkerboard", value);
                    }
                } else if (strcmp(key, "font") == 0) {
                    if (SDL_strcasecmp(value, "galaga88") == 0) {
                        out->font = FONT_STYLE_GALAGA88;
                    } else if (SDL_strcasecmp(value, "compact") == 0) {
                        out->font = FONT_STYLE_COMPACT;
                    } else {
                        out->font = FONT_STYLE_COMPACT;
                        SDL_Log("[config] Unrecognized font '%s', using compact", value);
                    }
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
                } else if (strcmp(key, "selected_platforms") == 0) {
                    snprintf(out->selected_platforms, sizeof(out->selected_platforms), "%s", value);
                }
            }
        }

        fclose(f);
    }

    if (out->width <= 0 || out->height <= 0) {
        SDL_Log("[config] Invalid width/height in '%s', falling back to %dx%d",
                path, CONFIG_DEFAULT_WIDTH, CONFIG_DEFAULT_HEIGHT);
        out->width = CONFIG_DEFAULT_WIDTH;
        out->height = CONFIG_DEFAULT_HEIGHT;
    }
    if (out->refresh_rate < 0) {
        out->refresh_rate = 0;
    }
    if (out->screensaver_timeout_ms < 0) {
        out->screensaver_timeout_ms = CONFIG_DEFAULT_SCREENSAVER_TIMEOUT_MS;
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

    if (!file_found) {
        config_write_default_file(path);
    }

    SDL_Log("[config] Loaded '%s': low-res mode = %dx%d@%dHz, toggle hotkey = %s, "
            "nav repeat = %dms delay / %dms interval, screensaver timeout = %dms%s",
            path, out->width, out->height, out->refresh_rate,
            SDL_GetKeyName(out->toggle_hotkey), out->nav_repeat_delay_ms, out->nav_repeat_interval_ms,
            out->screensaver_timeout_ms, out->screensaver_timeout_ms > 0 ? "" : " (disabled)");
    SDL_Log("[config] background = %s, font = %s",
            out->background == BACKGROUND_CHECKERBOARD ? "checkerboard" : "starfield",
            out->font == FONT_STYLE_GALAGA88 ? "galaga88" : "compact");
    SDL_Log("[config] LaunchBox dir = %s",
            out->launchbox_dir[0] ? out->launchbox_dir : "(not configured)");
    SDL_Log("[config] selected_platforms = %s", out->selected_platforms);

    for (int a = 0; a < INPUT_ACTION_COUNT; a++) {
        char value[64];
        input_binding_to_string(&out->bindings[a], value, sizeof(value));
        SDL_Log("[config] Binding %s = %s", INPUT_ACTION_NAMES[a], value);
    }
}

/* True at the first byte of the file or right after a newline -- rejects
   substring matches inside other lines. */
static SDL_bool is_line_start(const char *data, const char *p) {
    return (p == data || *(p - 1) == '\n') ? SDL_TRUE : SDL_FALSE;
}

/* Reads `path` fully into a heap buffer (caller frees). Binary mode on
   purpose: Windows' text-mode CRT would turn '\n' back into "\r\n" on a
   read-modify-write round trip, doubling carriage returns. */
/* A write offset that stayed inside `cap`, given snprintf's return (which
   is the length it wanted, not the length it wrote). */
static int clamp_offset(int off, size_t cap) {
    if (off < 0) {
        return 0;
    }
    return (off > (int)cap - 1) ? (int)cap - 1 : off;
}

/* NULL (and *out_len 0) for a missing, empty or unreadable file -- callers
   treat that the same as "no existing config". */
static char *read_config_file(const char *path, long *out_len) {
    *out_len = 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    char *data = (char *)malloc((size_t)len + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(data, 1, (size_t)len, f);
    fclose(f);
    data[read] = '\0';
    *out_len = (long)read;
    return data;
}

static SDL_bool write_config_file(const char *path, const char *data, size_t len, const char *what) {
    FILE *out = fopen(path, "wb");
    if (!out) {
        SDL_Log("[config] WARNING: could not open '%s' for writing, %s not saved", path, what);
        return SDL_FALSE;
    }
    fwrite(data, 1, len, out);
    fclose(out);
    return SDL_TRUE;
}

SDL_bool config_save_bindings(const char *path, const InputBinding bindings[INPUT_ACTION_COUNT]) {
    long len = 0;
    char *data = read_config_file(path, &len);

    char section[2048];
    int off = snprintf(section, sizeof(section),
        "[bindings]\n"
        "; Machine-managed by the in-app calibration (scroll up past\n"
        "; favorites to CALIBRATE CONTROLS, press SELECT) -- running it\n"
        "; again overwrites this whole section. Hand-editing is fine too,\n"
        "; same format: KEYBOARD <key name> | JOYBUTTON <index> |\n"
        "; JOYHAT <hat> <UP|DOWN|LEFT|RIGHT> | JOYAXIS <axis> <POSITIVE|NEGATIVE>\n");
    /* snprintf returns what it *would* have written, so `off` has to be
       clamped after every call or the next one indexes past `section`. */
    off = clamp_offset(off, sizeof(section));
    for (int a = 0; a < INPUT_ACTION_COUNT && off < (int)sizeof(section) - 1; a++) {
        char value[64];
        input_binding_to_string(&bindings[a], value, sizeof(value));
        int n = snprintf(section + off, sizeof(section) - (size_t)off, "%s=%s\n",
                          INPUT_ACTION_CONFIG_KEYS[a], value);
        if (n < 0) {
            break;
        }
        off = clamp_offset(off + n, sizeof(section));
    }

    size_t result_cap = (size_t)len + strlen(section) + 16;
    char *result = (char *)malloc(result_cap);
    if (!result) {
        SDL_Log("[config] WARNING: out of memory saving '%s', calibration not saved", path);
        free(data);
        return SDL_FALSE;
    }
    size_t result_len = 0;

    if (data) {
        /* Copy everything except existing [bindings] section(s) -- looping
           so duplicates left by earlier bugs/hand-edits get cleaned up. */
        const char *p = data;
        while (*p) {
            const char *tag = strstr(p, "[bindings]");
            if (!tag || !is_line_start(data, tag)) {
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

    /* Exactly one blank line before the fresh section. */
    if (result_len > 0 && result[result_len - 1] != '\n') {
        result[result_len++] = '\n';
    }
    if (result_len > 0) {
        result[result_len++] = '\n';
    }
    memcpy(result + result_len, section, strlen(section));
    result_len += strlen(section);

    SDL_bool ok = write_config_file(path, result, result_len, "calibration");

    free(data);
    free(result);
    if (ok) {
        SDL_Log("[config] Saved calibrated bindings to '%s'", path);
    }
    return ok;
}

SDL_bool config_save_selected_platforms(const char *path, const char *value) {
    long len = 0;
    char *data = read_config_file(path, &len);

    char new_line[CONFIG_SELECTED_PLATFORMS_MAX + 32];
    /* Same trap as above: snprintf's return is what it wanted to write, so
       on truncation this length would run the memcpys below off the end. */
    size_t line_len = (size_t)clamp_offset(
        snprintf(new_line, sizeof(new_line), "selected_platforms=%s\n", value), sizeof(new_line));

    size_t result_cap = (size_t)len + line_len + 64;
    char *result = (char *)malloc(result_cap);
    if (!result) {
        SDL_Log("[config] WARNING: out of memory saving '%s', platform selection not saved", path);
        free(data);
        return SDL_FALSE;
    }
    size_t result_len = 0;

    /* Replace an existing selected_platforms= line in place; else insert
       after the [launchbox] header; else append a fresh section. */
    const char *existing_key = NULL;
    const char *launchbox_header = NULL;
    if (data) {
        for (const char *p = data; (p = strstr(p, "selected_platforms=")) != NULL; p++) {
            if (is_line_start(data, p)) {
                existing_key = p;
                break;
            }
        }
        if (!existing_key) {
            for (const char *p = data; (p = strstr(p, "[launchbox]")) != NULL; p++) {
                if (is_line_start(data, p)) {
                    launchbox_header = p;
                    break;
                }
            }
        }
    }

    if (existing_key) {
        size_t prefix_len = (size_t)(existing_key - data);
        memcpy(result + result_len, data, prefix_len);
        result_len += prefix_len;
        memcpy(result + result_len, new_line, line_len);
        result_len += line_len;

        const char *eol = strchr(existing_key, '\n');
        const char *rest = eol ? eol + 1 : existing_key + strlen(existing_key);
        size_t rest_len = strlen(rest);
        memcpy(result + result_len, rest, rest_len);
        result_len += rest_len;
    } else if (launchbox_header) {
        const char *eol = strchr(launchbox_header, '\n');
        const char *after_header = eol ? eol + 1 : launchbox_header + strlen(launchbox_header);
        size_t prefix_len = (size_t)(after_header - data);
        memcpy(result + result_len, data, prefix_len);
        result_len += prefix_len;
        memcpy(result + result_len, new_line, line_len);
        result_len += line_len;

        size_t rest_len = strlen(after_header);
        memcpy(result + result_len, after_header, rest_len);
        result_len += rest_len;
    } else {
        if (data) {
            memcpy(result + result_len, data, (size_t)len);
            result_len += (size_t)len;
        }
        if (result_len > 0 && result[result_len - 1] != '\n') {
            result[result_len++] = '\n';
        }
        static const char header[] = "\n[launchbox]\n";
        memcpy(result + result_len, header, sizeof(header) - 1);
        result_len += sizeof(header) - 1;
        memcpy(result + result_len, new_line, line_len);
        result_len += line_len;
    }

    SDL_bool ok = write_config_file(path, result, result_len, "platform selection");

    free(data);
    free(result);
    if (ok) {
        SDL_Log("[config] Saved selected_platforms=%s to '%s'", value, path);
    }
    return ok;
}
