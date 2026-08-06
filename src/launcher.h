#ifndef CRT_LAUNCHER_H
#define CRT_LAUNCHER_H

#include <SDL.h>

#include "launchbox.h"

#define LAUNCHER_ID_MAX 40
#define LAUNCHER_PATH_MAX 512
#define LAUNCHER_CMDLINE_MAX 512
#define LAUNCHER_PLATFORM_MAX 64

/* One <Emulator> from Data\Emulators.xml -- an emulator LaunchBox knows
   how to run, e.g. "MAME 0,262" or "Retroarch". */
typedef struct {
    char id[LAUNCHER_ID_MAX];
    char application_path[LAUNCHER_PATH_MAX]; /* relative to launchbox_dir, e.g. "Emulators\MAME 0,262\mame.exe" */
    char command_line[LAUNCHER_CMDLINE_MAX];  /* base argument template, may contain a %romlocation% placeholder */
    SDL_bool no_quotes;                       /* if true, the ROM argument is appended unquoted */
    SDL_bool file_name_without_ext_and_path;  /* if true, the ROM argument is just its base filename
                                                  (no directory, no extension) rather than a full path */
} LauncherEmulator;

/* One <EmulatorPlatform> from Data\Emulators.xml -- ties an Emulator to a
   platform, optionally overriding its base command line (e.g. RetroArch's
   per-platform core: `-L "cores\fbneo_libretro.dll" -f`). A platform can
   have several of these (multiple emulators can support it); `is_default`
   marks which one a game with no emulator of its own should use. */
typedef struct {
    char emulator_id[LAUNCHER_ID_MAX];
    char platform[LAUNCHER_PLATFORM_MAX];
    char command_line[LAUNCHER_CMDLINE_MAX]; /* override; empty means "use the Emulator's own command_line" */
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

/*
 * Reads "<launchbox_dir>\Data\Emulators.xml" -- nothing else is read; same
 * Data\-only scoping as launchbox.c. Parses every <Emulator> and
 * <EmulatorPlatform> block (bounded field search within each, not a
 * global one -- see launcher.c for how the two are told apart despite
 * <EmulatorPlatform> containing a child field that's *also* literally
 * named <Emulator>).
 *
 * Never fails loudly -- a missing/unreadable file just leaves `loaded`
 * SDL_FALSE, and launcher_launch() will log a warning and do nothing
 * rather than crash or guess.
 */
void launcher_load(const char *launchbox_dir, LauncherDatabase *out);

void launcher_free(LauncherDatabase *db);

/*
 * Resolves which emulator to run `ver` with -- its own emulator_id if set,
 * else the platform's default EmulatorPlatform -- builds the command line
 * (substituting %romlocation% with the ROM's containing folder, and
 * appending the ROM itself per the resolved emulator's quoting/path
 * rules), and spawns it via CreateProcess. Returns SDL_TRUE if the
 * process was successfully started; this does not wait for it to exit or
 * verify the game actually loaded.
 *
 * This handles the %romlocation% + bare-romname pattern MAME uses and the
 * full-path pattern most other emulators use, but does NOT replicate
 * LaunchBox's own launch behavior beyond that: no AutoHotkey nag-screen
 * scripts, no pause/save-state handling, no achievements login. See the
 * README for what's explicitly out of scope.
 */
SDL_bool launcher_launch(const LauncherDatabase *db, const LaunchboxVersion *ver);

#endif /* CRT_LAUNCHER_H */
