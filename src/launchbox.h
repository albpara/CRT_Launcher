#ifndef CRT_LAUNCHBOX_H
#define CRT_LAUNCHBOX_H

#include <SDL.h>

#define LAUNCHBOX_TITLE_MAX 64
#define LAUNCHBOX_VERSION_LABEL_MAX 48
#define LAUNCHBOX_ROM_PATH_MAX 512
#define LAUNCHBOX_ID_MAX 40
#define LAUNCHBOX_PLATFORM_MAX 64

typedef enum {
    LAUNCHBOX_STATUS_NOT_CONFIGURED, /* config.ini has no [launchbox] launchbox_dir */
    LAUNCHBOX_STATUS_DIR_NOT_FOUND,  /* <launchbox_dir>\Data\Platforms doesn't exist */
    LAUNCHBOX_STATUS_NO_PLATFORMS,   /* Data\Platforms exists but has no *.xml files */
    LAUNCHBOX_STATUS_LOADED          /* at least one platform XML was scanned */
} LaunchboxStatus;

/* One physical "way to launch this game" -- either a <Game> block's own
   ApplicationPath, or one of its <AdditionalApplication> entries (LaunchBox's
   mechanism for regional/hardware variants, e.g. a game's Japan/USA/World
   ROMs). Several of these can belong to the same LaunchboxGameGroup. */
typedef struct {
    char label[LAUNCHBOX_VERSION_LABEL_MAX]; /* this entry's own <Version> if set (e.g. "(World
                                                  940223)"), else <Region> (e.g. "North America"),
                                                  else the ROM filename minus extension (e.g.
                                                  "atetris") -- same fallback order whether this
                                                  entry came from a <Game> block or one of its
                                                  <AdditionalApplication> siblings, see
                                                  build_version_label in launchbox.c */
    char rom_path[LAUNCHBOX_ROM_PATH_MAX];   /* this entry's own <ApplicationPath>, relative to
                                                  launchbox_dir, e.g. "Roms\0.262\springer.zip" */
    char emulator_id[LAUNCHBOX_ID_MAX];      /* this entry's own emulator (<Emulator> on a <Game>,
                                                  <EmulatorId> on an <AdditionalApplication>, falling
                                                  back to the parent Game's if an AdditionalApplication
                                                  doesn't set its own); empty means "use the platform's
                                                  default emulator" -- see launcher.c */
    char platform[LAUNCHBOX_PLATFORM_MAX];   /* e.g. "Arcade" -- the platform XML filename this entry
                                                  came from, needed to resolve the default emulator */
} LaunchboxVersion;

/* One canonical game -- a title, plus the run of LaunchboxVersion entries
   (in LaunchboxInfo.versions[version_start .. version_start+version_count))
   grouped under it (see launchbox_load for how). version_count is always
   >= 1; > 1 means the game has multiple selectable versions/clones. */
typedef struct {
    char title[LAUNCHBOX_TITLE_MAX];
    int version_start;
    int version_count;
    SDL_bool is_favorite;   /* from the primary <Game>'s <Favorite> field */
} LaunchboxGameGroup;

typedef struct {
    LaunchboxStatus status;
    int platform_count;             /* number of *.xml files scanned in Data\Platforms --
                                        also the length of platform_names below */

    /* One entry per distinct platform XML scanned (e.g. "Arcade", "SNES"),
       heap-allocated, in scan order (not sorted). What gamelist.c's
       platform-filter checkboxes list and match a LaunchboxGameGroup's
       platform against -- see gamelist_recompute_visible_groups. */
    char (*platform_names)[LAUNCHBOX_PLATFORM_MAX];

    /* One entry per unique title, heap-allocated. Ordered favorites-first
       (see favorite_count below), each of those two blocks alphabetical by
       title -- see the partitioning step at the end of launchbox_load. */
    LaunchboxGameGroup *groups;
    int group_count;
    int favorite_count;             /* groups[0..favorite_count) are the favorites;
                                        groups[favorite_count..group_count) are the rest */

    LaunchboxVersion *versions;     /* flat backing array referenced by groups[i].version_start, heap-allocated */
    int version_count;              /* total launchable entries across all groups (<Game> blocks
                                        plus every matched <AdditionalApplication>) */
} LaunchboxInfo;

/*
 * Points at a LaunchBox install root (e.g. "E:\LaunchBox") rather than a
 * single export file -- that's the same root launcher.c separately reads
 * Data\Emulators.xml from, so config.ini only needs to name the install
 * once. Reads by this function are hard-scoped to "<launchbox_dir>\Data\Platforms":
 * only *.xml files directly inside that one folder are ever opened (no
 * recursion, nothing else under the LaunchBox install is touched).
 *
 * For each platform XML found, reads every <Game>...</Game> block (a
 * bounded search within each block, not a global one -- see launchbox.c)
 * for its Title, DatabaseID, ApplicationPath, ID, Emulator, Favorite,
 * Region, and Version (the last two feed its version-picker label the same
 * way as an AdditionalApplication's, see LaunchboxVersion.label). It then
 * reads every <AdditionalApplication>...</AdditionalApplication> block --
 * LaunchBox's *other* mechanism for "this game has multiple versions",
 * used for things like per-region ROMs where each version is its own
 * sibling element rather than a separate <Game> -- and resolves each
 * one's <GameID> back to the <Game> it belongs to via that ID, also
 * reading its ApplicationPath and EmulatorId (note the different field
 * name from <Game>'s <Emulator> -- LaunchBox itself isn't consistent
 * here). The platform name (e.g. "Arcade") comes from the XML filename,
 * not a field inside it. All of this -- ApplicationPath, emulator, and
 * platform -- is what launcher.c needs to actually start a game later.
 *
 * All resulting entries (one per <Game>, one per matched
 * <AdditionalApplication>) are sorted by title, then consecutive entries
 * sharing the same effective grouping key are collapsed into one
 * LaunchboxGameGroup with multiple LaunchboxVersion entries -- covering
 * both a real shared DatabaseID (how LaunchBox marks separate <Game> clone
 * entries as "the same game", e.g. six arcade "Tetris" ROMs) and an
 * AdditionalApplication's resolved parent (e.g. SSF2T's eight regional
 * variants). See the comment on RawGame.database_id in launchbox.c for
 * exactly how that key is chosen. The UI then shows one row per game and
 * lets Shift+Enter unfold the specific versions.
 *
 * After grouping, groups whose primary <Game> has <Favorite>true</Favorite>
 * are moved to the front of the list (partition_favorites_first in
 * launchbox.c) -- the alphabetical order from the title sort is preserved
 * within the favorites block and within the rest, just split into those two
 * runs instead of one.
 *
 * This is deliberately NOT a real XML parser: no nesting beyond a single
 * block, no attributes, no CDATA, no schema validation, and only a
 * handful of fields are read -- enough to display a groupable list and
 * launch it (see launcher.c), not the eventual LaunchBox import pipeline
 * (that would need a proper parser and full per-game metadata: box art,
 * descriptions, per-game overrides beyond emulator selection, etc.).
 *
 * Never fails loudly -- a missing/empty config or folder just yields a
 * status the renderer can display, logged to the console either way.
 */
void launchbox_load(const char *launchbox_dir, LaunchboxInfo *out);

/* Frees `groups`, `versions`, and `platform_names`. Safe to call
   regardless of `status` (including on an info struct that was never
   successfully loaded). */
void launchbox_free(LaunchboxInfo *info);

#endif /* CRT_LAUNCHBOX_H */
