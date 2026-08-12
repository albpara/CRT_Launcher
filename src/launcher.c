#include "launcher.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xml_util.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define LNC_INITIAL_CAPACITY 64

typedef struct {
    LauncherEmulator *items;
    int count;
    int capacity;
} EmulatorArray;

typedef struct {
    LauncherEmulatorPlatform *items;
    int count;
    int capacity;
} EmulatorPlatformArray;

static LauncherEmulator *emulator_array_push(EmulatorArray *arr) {
    if (arr->count >= arr->capacity) {
        int new_capacity = (arr->capacity == 0) ? LNC_INITIAL_CAPACITY : arr->capacity * 2;
        LauncherEmulator *grown = (LauncherEmulator *)realloc(arr->items, (size_t)new_capacity * sizeof(LauncherEmulator));
        if (!grown) {
            return NULL;
        }
        arr->items = grown;
        arr->capacity = new_capacity;
    }
    LauncherEmulator *slot = &arr->items[arr->count];
    memset(slot, 0, sizeof(*slot));
    arr->count++;
    return slot;
}

static LauncherEmulatorPlatform *emulator_platform_array_push(EmulatorPlatformArray *arr) {
    if (arr->count >= arr->capacity) {
        int new_capacity = (arr->capacity == 0) ? LNC_INITIAL_CAPACITY : arr->capacity * 2;
        LauncherEmulatorPlatform *grown =
            (LauncherEmulatorPlatform *)realloc(arr->items, (size_t)new_capacity * sizeof(LauncherEmulatorPlatform));
        if (!grown) {
            return NULL;
        }
        arr->items = grown;
        arr->capacity = new_capacity;
    }
    LauncherEmulatorPlatform *slot = &arr->items[arr->count];
    memset(slot, 0, sizeof(*slot));
    arr->count++;
    return slot;
}

static SDL_bool parse_bool_field(const char *s) {
    return (strcmp(s, "true") == 0) ? SDL_TRUE : SDL_FALSE;
}

/* Single combined scan: <EmulatorPlatform> blocks contain a child field
   literally named <Emulator>, so matching <EmulatorPlatform> first and
   jumping past whole blocks keeps a global "<Emulator>" search from
   misparsing that child as a block opening. */
static void scan_emulators_data(const char *data, EmulatorArray *emulators, EmulatorPlatformArray *platforms) {
    static const char EMU_PLATFORM_OPEN[] = "<EmulatorPlatform>";
    static const char EMU_PLATFORM_CLOSE[] = "</EmulatorPlatform>";
    static const char EMU_OPEN[] = "<Emulator>";
    static const char EMU_CLOSE[] = "</Emulator>";

    const size_t emu_platform_open_len = sizeof(EMU_PLATFORM_OPEN) - 1;
    const size_t emu_open_len = sizeof(EMU_OPEN) - 1;

    const char *p = data;
    while (*p) {
        if (strncmp(p, EMU_PLATFORM_OPEN, emu_platform_open_len) == 0) {
            const char *block_start = p + emu_platform_open_len;
            const char *block_end = strstr(block_start, EMU_PLATFORM_CLOSE);
            if (!block_end) {
                break;
            }

            char emulator_id[LAUNCHER_ID_MAX];
            char platform[LAUNCHER_PLATFORM_MAX];
            char command_line[LAUNCHER_CMDLINE_MAX];
            char is_default[8];

            xml_extract_field(block_start, block_end, "<Emulator>", "</Emulator>", emulator_id, sizeof(emulator_id));
            xml_extract_field(block_start, block_end, "<Platform>", "</Platform>", platform, sizeof(platform));
            xml_extract_field(block_start, block_end, "<CommandLine>", "</CommandLine>", command_line, sizeof(command_line));
            xml_extract_field(block_start, block_end, "<Default>", "</Default>", is_default, sizeof(is_default));

            if (emulator_id[0] && platform[0]) {
                LauncherEmulatorPlatform *slot = emulator_platform_array_push(platforms);
                if (slot) {
                    snprintf(slot->emulator_id, sizeof(slot->emulator_id), "%s", emulator_id);
                    snprintf(slot->platform, sizeof(slot->platform), "%s", platform);
                    snprintf(slot->command_line, sizeof(slot->command_line), "%s", command_line);
                    slot->is_default = parse_bool_field(is_default);
                }
            }

            p = block_end + (sizeof(EMU_PLATFORM_CLOSE) - 1);
        } else if (strncmp(p, EMU_OPEN, emu_open_len) == 0) {
            const char *block_start = p + emu_open_len;
            const char *block_end = strstr(block_start, EMU_CLOSE);
            if (!block_end) {
                break;
            }

            char id[LAUNCHER_ID_MAX];
            char application_path[LAUNCHER_PATH_MAX];
            char command_line[LAUNCHER_CMDLINE_MAX];
            char no_quotes[8];
            char file_name_only[8];

            xml_extract_field(block_start, block_end, "<ID>", "</ID>", id, sizeof(id));
            xml_extract_field(block_start, block_end, "<ApplicationPath>", "</ApplicationPath>",
                               application_path, sizeof(application_path));
            xml_extract_field(block_start, block_end, "<CommandLine>", "</CommandLine>", command_line, sizeof(command_line));
            xml_extract_field(block_start, block_end, "<NoQuotes>", "</NoQuotes>", no_quotes, sizeof(no_quotes));
            xml_extract_field(block_start, block_end, "<FileNameWithoutExtensionAndPath>",
                               "</FileNameWithoutExtensionAndPath>", file_name_only, sizeof(file_name_only));

            if (id[0] && application_path[0]) {
                LauncherEmulator *slot = emulator_array_push(emulators);
                if (slot) {
                    snprintf(slot->id, sizeof(slot->id), "%s", id);
                    snprintf(slot->application_path, sizeof(slot->application_path), "%s", application_path);
                    snprintf(slot->command_line, sizeof(slot->command_line), "%s", command_line);
                    slot->no_quotes = parse_bool_field(no_quotes);
                    slot->file_name_without_ext_and_path = parse_bool_field(file_name_only);
                }
            }

            p = block_end + (sizeof(EMU_CLOSE) - 1);
        } else {
            p++;
        }
    }
}

void launcher_load(const char *launchbox_dir, LauncherDatabase *out) {
    SDL_zerop(out);

    if (!launchbox_dir || launchbox_dir[0] == '\0') {
        SDL_Log("[launcher] No launchbox_dir configured, skipping Emulators.xml");
        return;
    }

    char root[LAUNCHER_PATH_MAX];
    snprintf(root, sizeof(root), "%s", launchbox_dir);
    size_t root_len = strlen(root);
    if (root_len > 0 && (root[root_len - 1] == '\\' || root[root_len - 1] == '/')) {
        root[root_len - 1] = '\0';
    }

    char file_path[LAUNCHER_PATH_MAX];
    snprintf(file_path, sizeof(file_path), "%s\\Data\\Emulators.xml", root);

    long len = 0;
    char *data = xml_read_entire_file(file_path, &len);
    if (!data) {
        SDL_Log("[launcher] WARNING: could not open '%s' -- games cannot be launched", file_path);
        return;
    }

    EmulatorArray emulators = {0};
    EmulatorPlatformArray platforms = {0};
    scan_emulators_data(data, &emulators, &platforms);
    free(data);

    snprintf(out->launchbox_dir, sizeof(out->launchbox_dir), "%s", root);
    out->emulators = emulators.items;
    out->emulator_count = emulators.count;
    out->platforms = platforms.items;
    out->platform_count = platforms.count;
    out->loaded = SDL_TRUE;

    SDL_Log("[launcher] Loaded '%s': %d emulator(s), %d platform mapping(s)",
            file_path, out->emulator_count, out->platform_count);
}

void launcher_free(LauncherDatabase *db) {
    free(db->emulators);
    free(db->platforms);
    db->emulators = NULL;
    db->platforms = NULL;
    db->emulator_count = 0;
    db->platform_count = 0;
    db->loaded = SDL_FALSE;
}

static const LauncherEmulator *find_emulator(const LauncherDatabase *db, const char *id) {
    for (int i = 0; i < db->emulator_count; i++) {
        if (strcmp(db->emulators[i].id, id) == 0) {
            return &db->emulators[i];
        }
    }
    return NULL;
}

static const LauncherEmulatorPlatform *find_default_platform(const LauncherDatabase *db, const char *platform) {
    for (int i = 0; i < db->platform_count; i++) {
        if (db->platforms[i].is_default && strcmp(db->platforms[i].platform, platform) == 0) {
            return &db->platforms[i];
        }
    }
    return NULL;
}

static const LauncherEmulatorPlatform *find_platform_mapping(const LauncherDatabase *db, const char *emulator_id,
                                                               const char *platform) {
    for (int i = 0; i < db->platform_count; i++) {
        if (strcmp(db->platforms[i].emulator_id, emulator_id) == 0 && strcmp(db->platforms[i].platform, platform) == 0) {
            return &db->platforms[i];
        }
    }
    return NULL;
}

/* "Roms\0.262\springer.zip" -> "springer". */
static void base_name_without_ext(const char *path, char *out, size_t out_cap) {
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '\\' || *p == '/') {
            base = p + 1;
        }
    }
    size_t len = strlen(base);
    const char *dot = NULL;
    for (const char *p = base; *p; p++) {
        if (*p == '.') {
            dot = p;
        }
    }
    size_t copy_len = dot ? (size_t)(dot - base) : len;
    if (copy_len >= out_cap) {
        copy_len = out_cap - 1;
    }
    memcpy(out, base, copy_len);
    out[copy_len] = '\0';
}

/* Containing directory of `path`; empty if no separator. */
static void directory_part(const char *path, char *out, size_t out_cap) {
    const char *last_sep = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '\\' || *p == '/') {
            last_sep = p;
        }
    }
    size_t len = last_sep ? (size_t)(last_sep - path) : 0;
    if (len >= out_cap) {
        len = out_cap - 1;
    }
    memcpy(out, path, len);
    out[len] = '\0';
}

/* Replaces the first "%romlocation%" with `replacement` (copies through
   unchanged when absent). */
static void substitute_romlocation(const char *template_str, const char *replacement, char *out, size_t out_cap) {
    static const char PLACEHOLDER[] = "%romlocation%";
    const char *pos = strstr(template_str, PLACEHOLDER);
    if (!pos) {
        snprintf(out, out_cap, "%s", template_str);
        return;
    }
    int prefix_len = (int)(pos - template_str);
    snprintf(out, out_cap, "%.*s%s%s", prefix_len, template_str, replacement, pos + (sizeof(PLACEHOLDER) - 1));
}

#ifdef _WIN32
static SDL_bool has_exe_extension(const char *path) {
    size_t len = strlen(path);
    if (len < 4) {
        return SDL_FALSE;
    }
    return SDL_strcasecmp(path + len - 4, ".exe") == 0 ? SDL_TRUE : SDL_FALSE;
}

/* Drive letter ("D:\...") or UNC ("\\server\..."). A Windows-platform
   ApplicationPath can be either absolute or launchbox_dir-relative --
   real data has shown both; don't assume one. */
static SDL_bool is_absolute_windows_path(const char *path) {
    if (path[0] == '\\' && path[1] == '\\') {
        return SDL_TRUE;
    }
    return (isalpha((unsigned char)path[0]) && path[1] == ':' &&
            (path[2] == '\\' || path[2] == '/')) ? SDL_TRUE : SDL_FALSE;
}

/* Spawns `full_command_line` (mutable, as CreateProcess requires when
   lpApplicationName is NULL). CREATE_NEW_PROCESS_GROUP is required: a
   console-subsystem child would otherwise share our console/process
   group, and a console control event on its exit can silently kill the
   launcher too -- observed in practice as the launcher vanishing after
   returning from a game, with nothing logged. CREATE_NO_WINDOW keeps that
   child's console off screen (mame.exe is subsystem CUI, so Windows would
   otherwise pop a visible one); it still gets a console of its own, so the
   isolation above is unaffected. GUI children ignore the flag. */
static SDL_bool spawn_process(char *full_command_line, const char *working_dir) {
    SDL_Log("[launcher] Launching: %s", full_command_line);
    SDL_Log("[launcher] Working directory: %s", working_dir);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    SDL_zero(si);
    si.cb = sizeof(si);
    SDL_zero(pi);

    BOOL ok = CreateProcessA(NULL, full_command_line, NULL, NULL, FALSE,
                              CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW,
                              NULL, working_dir, &si, &pi);

    if (!ok) {
        SDL_Log("[launcher] WARNING: CreateProcess failed (error %lu) for '%s'",
                (unsigned long)GetLastError(), full_command_line);
        return SDL_FALSE;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return SDL_TRUE;
}

/* Direct launch with no emulator and no arguments -- how LaunchBox's
   "Windows" platform works (ApplicationPath points straight at the exe).
   Working directory is the exe's own folder. */
static SDL_bool launch_windows_app(const LauncherDatabase *db, const LaunchboxVersion *ver) {
    char absolute_app_path[LAUNCHER_PATH_MAX * 2];
    if (is_absolute_windows_path(ver->rom_path)) {
        snprintf(absolute_app_path, sizeof(absolute_app_path), "%s", ver->rom_path);
    } else {
        snprintf(absolute_app_path, sizeof(absolute_app_path), "%s\\%s", db->launchbox_dir, ver->rom_path);
    }

    char working_dir[LAUNCHER_PATH_MAX * 2];
    directory_part(absolute_app_path, working_dir, sizeof(working_dir));

    char full_command_line[LAUNCHER_PATH_MAX * 2 + 2];
    snprintf(full_command_line, sizeof(full_command_line), "\"%s\"", absolute_app_path);

    SDL_Log("[launcher] '%s' has no resolvable emulator but ends in .exe -- launching directly", ver->label);
    return spawn_process(full_command_line, working_dir);
}

SDL_bool launcher_launch(const LauncherDatabase *db, const LaunchboxVersion *ver) {
    if (!db->loaded) {
        SDL_Log("[launcher] WARNING: cannot launch '%s' -- Data\\Emulators.xml was not loaded", ver->label);
        return SDL_FALSE;
    }

    const char *emulator_id = ver->emulator_id;
    const LauncherEmulatorPlatform *platform_mapping = NULL;

    if (emulator_id[0]) {
        /* Own emulator; mapping only consulted for a command-line override. */
        platform_mapping = find_platform_mapping(db, emulator_id, ver->platform);
    } else {
        platform_mapping = find_default_platform(db, ver->platform);
        if (platform_mapping) {
            emulator_id = platform_mapping->emulator_id;
        }
    }

    const LauncherEmulator *emu = emulator_id[0] ? find_emulator(db, emulator_id) : NULL;
    if (!emu) {
        /* No emulator at all -- maybe a directly launchable Windows app. */
        if (has_exe_extension(ver->rom_path)) {
            return launch_windows_app(db, ver);
        }
        SDL_Log("[launcher] WARNING: could not resolve an emulator for platform '%s' (game's emulator id: '%s') "
                "-- is there a Default EmulatorPlatform mapping for this platform in Emulators.xml?",
                ver->platform, ver->emulator_id[0] ? ver->emulator_id : "(none set)");
        return SDL_FALSE;
    }

    char absolute_app_path[LAUNCHER_PATH_MAX * 2];
    snprintf(absolute_app_path, sizeof(absolute_app_path), "%s\\%s", db->launchbox_dir, emu->application_path);

    char absolute_rom_path[LAUNCHER_PATH_MAX * 2];
    snprintf(absolute_rom_path, sizeof(absolute_rom_path), "%s\\%s", db->launchbox_dir, ver->rom_path);

    char rom_dir[LAUNCHER_PATH_MAX * 2];
    directory_part(absolute_rom_path, rom_dir, sizeof(rom_dir));
    char rom_dir_quoted[LAUNCHER_PATH_MAX * 2 + 2];
    snprintf(rom_dir_quoted, sizeof(rom_dir_quoted), "\"%s\"", rom_dir);

    const char *command_template =
        (platform_mapping && platform_mapping->command_line[0]) ? platform_mapping->command_line : emu->command_line;

    char substituted_command[LAUNCHER_CMDLINE_MAX * 2];
    substitute_romlocation(command_template, rom_dir_quoted, substituted_command, sizeof(substituted_command));

    char rom_argument[LAUNCHER_PATH_MAX * 2 + 2];
    if (emu->file_name_without_ext_and_path) {
        char base[LAUNCHER_PATH_MAX];
        base_name_without_ext(ver->rom_path, base, sizeof(base));
        snprintf(rom_argument, sizeof(rom_argument), emu->no_quotes ? "%s" : "\"%s\"", base);
    } else {
        snprintf(rom_argument, sizeof(rom_argument), emu->no_quotes ? "%s" : "\"%s\"", absolute_rom_path);
    }

    char full_command_line[LAUNCHER_CMDLINE_MAX * 4];
    snprintf(full_command_line, sizeof(full_command_line), "\"%s\" %s %s",
             absolute_app_path, substituted_command, rom_argument);

    char working_dir[LAUNCHER_PATH_MAX * 2];
    directory_part(absolute_app_path, working_dir, sizeof(working_dir));

    return spawn_process(full_command_line, working_dir);
}
#else
SDL_bool launcher_launch(const LauncherDatabase *db, const LaunchboxVersion *ver) {
    (void)db;
    SDL_Log("[launcher] WARNING: launching '%s' is only implemented for Windows", ver->label);
    return SDL_FALSE;
}
#endif
