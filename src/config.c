#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_DEFAULT_WIDTH  320
#define CONFIG_DEFAULT_HEIGHT 240
#define CONFIG_DEFAULT_REFRESH 60
#define CONFIG_DEFAULT_HOTKEY SDLK_F5
#define CONFIG_DEFAULT_NAV_REPEAT_DELAY_MS 250
#define CONFIG_DEFAULT_NAV_REPEAT_INTERVAL_MS 40

#define CONFIG_MAX_LINE 256
#define CONFIG_MAX_SECTION 64

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

static SDL_bool parse_bool(const char *value, SDL_bool fallback) {
    if (SDL_strcasecmp(value, "true") == 0 || strcmp(value, "1") == 0) {
        return SDL_TRUE;
    }
    if (SDL_strcasecmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        return SDL_FALSE;
    }
    return fallback;
}

static SDL_Keycode parse_hotkey(const char *name) {
    SDL_Keycode kc = SDL_GetKeyFromName(name);
    if (kc == SDLK_UNKNOWN) {
        SDL_Log("[config] Unrecognized toggle_hotkey '%s', defaulting to F5", name);
        return CONFIG_DEFAULT_HOTKEY;
    }
    return kc;
}

void config_load(const char *path, AppConfig *out) {
    out->width = CONFIG_DEFAULT_WIDTH;
    out->height = CONFIG_DEFAULT_HEIGHT;
    out->refresh_rate = CONFIG_DEFAULT_REFRESH;
    out->toggle_hotkey = CONFIG_DEFAULT_HOTKEY;
    out->nav_repeat_delay_ms = CONFIG_DEFAULT_NAV_REPEAT_DELAY_MS;
    out->nav_repeat_interval_ms = CONFIG_DEFAULT_NAV_REPEAT_INTERVAL_MS;
    out->show_console = SDL_TRUE;
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
        } else if (strcmp(section, "debug") == 0) {
            if (strcmp(key, "show_console") == 0) {
                out->show_console = parse_bool(value, SDL_TRUE);
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

    SDL_Log("[config] Loaded '%s': low-res mode = %dx%d@%dHz, toggle hotkey = %s, "
            "nav repeat = %dms delay / %dms interval, console = %s",
            path, out->width, out->height, out->refresh_rate,
            SDL_GetKeyName(out->toggle_hotkey), out->nav_repeat_delay_ms, out->nav_repeat_interval_ms,
            out->show_console ? "shown" : "hidden");
    SDL_Log("[config] LaunchBox dir = %s",
            out->launchbox_dir[0] ? out->launchbox_dir : "(not configured)");
}
