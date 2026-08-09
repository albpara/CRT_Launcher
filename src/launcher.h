#ifndef CRT_LAUNCHER_H
#define CRT_LAUNCHER_H

#include <SDL.h>

#include "launchbox.h"

#define LAUNCHER_ID_MAX 40
#define LAUNCHER_PATH_MAX 512
#define LAUNCHER_CMDLINE_MAX 512
#define LAUNCHER_PLATFORM_MAX 64

/* One <Emulator> from Data\Emulators.xml. */
typedef struct {
    char id[LAUNCHER_ID_MAX];
    char application_path[LAUNCHER_PATH_MAX]; /* relative to launchbox_dir */
    char command_line[LAUNCHER_CMDLINE_MAX];  /* base template, may contain %romlocation% */
    SDL_bool no_quotes;                       /* append the ROM argument unquoted */
    SDL_bool file_name_without_ext_and_path;  /* ROM argument is the bare filename */
} LauncherEmulator;

/* One <EmulatorPlatform>: maps an emulator to a platform, optionally
   overriding its command line (e.g. RetroArch core selection).
   `is_default` marks the emulator a game without its own should use. */
typedef struct {
    char emulator_id[LAUNCHER_ID_MAX];
    char platform[LAUNCHER_PLATFORM_MAX];
    char command_line[LAUNCHER_CMDLINE_MAX]; /* empty = use the emulator's own */
    SDL_bool is_default;
} LauncherEmulatorPlatform;

typedef struct {
    SDL_bool loaded;
    char launchbox_dir[LAUNCHER_PATH_MAX];

    LauncherEmulator *emulators;
    int emulator_count;

    LauncherEmulatorPlatform *platforms;
    int platform_count;
} LauncherDatabase;

/* Parses "<launchbox_dir>\Data\Emulators.xml" once (nothing else is
   read). On failure just leaves `loaded` false -- launcher_launch() then
   warns instead of crashing. */
void launcher_load(const char *launchbox_dir, LauncherDatabase *out);

void launcher_free(LauncherDatabase *db);

/* Resolves the emulator for `ver` (its own emulator_id, else the
   platform's default), builds the command line (%romlocation% + ROM
   argument per the emulator's quoting rules), and spawns it. If no
   emulator resolves and rom_path ends in ".exe", launches it directly
   instead (LaunchBox's "Windows" platform works this way; the path may be
   absolute or launchbox_dir-relative). Fire-and-forget: does not wait or
   verify the game loaded. No AutoHotkey/pause/achievement extras. */
SDL_bool launcher_launch(const LauncherDatabase *db, const LaunchboxVersion *ver);

#endif /* CRT_LAUNCHER_H */
