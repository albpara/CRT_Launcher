#ifndef CRT_LAUNCHBOX_H
#define CRT_LAUNCHBOX_H

#include <SDL.h>

#define LAUNCHBOX_TITLE_MAX 64
#define LAUNCHBOX_VERSION_LABEL_MAX 48
#define LAUNCHBOX_ROM_PATH_MAX 512
#define LAUNCHBOX_ID_MAX 40
#define LAUNCHBOX_PLATFORM_MAX 64

typedef enum {
    LAUNCHBOX_STATUS_NOT_CONFIGURED, /* no launchbox_dir */
    LAUNCHBOX_STATUS_DIR_NOT_FOUND,  /* <dir>\Data\Platforms missing */
    LAUNCHBOX_STATUS_NO_PLATFORMS,   /* no *.xml files inside it */
    LAUNCHBOX_STATUS_LOADED
} LaunchboxStatus;

/* One way to launch a game -- a <Game>'s own ApplicationPath or one of
   its <AdditionalApplication> variants. */
typedef struct {
    char label[LAUNCHBOX_VERSION_LABEL_MAX]; /* <Version>, else <Region>, else ROM
                                                  filename -- see build_version_label */
    char rom_path[LAUNCHBOX_ROM_PATH_MAX];   /* <ApplicationPath>, usually relative to
                                                  launchbox_dir */
    char emulator_id[LAUNCHBOX_ID_MAX];      /* empty = use the platform's default */
    char platform[LAUNCHBOX_PLATFORM_MAX];   /* platform XML filename, e.g. "Arcade" */
} LaunchboxVersion;

/* One unique title with its run of versions in LaunchboxInfo.versions
   [version_start .. version_start+version_count). version_count >= 1. */
typedef struct {
    char title[LAUNCHBOX_TITLE_MAX];
    int version_start;
    int version_count;
    SDL_bool is_favorite;
} LaunchboxGameGroup;

typedef struct {
    LaunchboxStatus status;
    int platform_count;   /* platform XMLs scanned; length of platform_names */

    /* Distinct platform names in scan order, heap-allocated. Drives the
       platform-filter checkboxes (see gamelist.c). */
    char (*platform_names)[LAUNCHBOX_PLATFORM_MAX];

    /* One per unique title, favorites first, each block alphabetical. */
    LaunchboxGameGroup *groups;
    int group_count;
    int favorite_count;   /* groups[0..favorite_count) are favorites */

    LaunchboxVersion *versions;  /* flat backing array, heap-allocated */
    int version_count;
} LaunchboxInfo;

/* Scans "<launchbox_dir>\Data\Platforms\*.xml" ONLY -- nothing else under
   the install is ever read. Extracts each <Game>'s Title, DatabaseID,
   ApplicationPath, ID, Emulator, Favorite, Region, and Version, plus every
   <AdditionalApplication> resolved to its parent via <GameID>. Entries are
   title-sorted and clones/variants collapsed into groups via DatabaseID
   (or the game's <ID> when absent), favorites partitioned first. Not a
   real XML parser -- bounded substring search (see xml_util.c). Never
   fails loudly; a missing dir just yields a status. */
void launchbox_load(const char *launchbox_dir, LaunchboxInfo *out);

/* Frees all heap arrays. Safe on a never-loaded struct. */
void launchbox_free(LaunchboxInfo *info);

#endif /* CRT_LAUNCHBOX_H */
