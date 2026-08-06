#include "launchbox.h"

#include <SDL.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xml_util.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define LB_PATH_MAX 1024
#define LB_INITIAL_CAPACITY 1024
#define LB_GUID_MAX 40
#define LB_FIELD_MAX 48
/* Holds either a real (short, numeric) DatabaseID or, when a game has none,
   the fallback described below `database_id` on RawGame -- sized to fit a
   full GUID either way. */
#define LB_DB_ID_MAX LB_GUID_MAX

/* One physical "way to launch this game" straight off the XML, before
   grouping -- either a <Game> block's own primary ApplicationPath, or one
   of its <AdditionalApplication> entries (see extract_additional_apps).
   Intermediate state only -- never exposed outside this file.

   `database_id` holds the *effective grouping key*, not always the literal
   <DatabaseID> field: when a <Game> has one, that's used (it's how real
   LaunchBox databases mark clones/regions as "the same game" -- e.g. six
   arcade Tetris ROMs sharing one DatabaseID); when a <Game> has none, its
   own <ID> is used instead, purely so it and its <AdditionalApplication>
   versions still merge into one group despite lacking a DatabaseID. Since
   <ID> is unique per game, that fallback can never accidentally merge two
   unrelated games the way comparing raw title text could.

   `is_primary` marks entries that came from a <Game> block itself (as
   opposed to one of its <AdditionalApplication> entries). compare_raw_games
   sorts primary entries first within a group, so
   LaunchboxGameGroup.version_start always points at the game's own
   default ApplicationPath -- that's what a plain Enter on a collapsed
   multi-version row launches, before the user picks a specific version
   with Shift+Enter. */
typedef struct {
    char title[LAUNCHBOX_TITLE_MAX];
    char database_id[LB_DB_ID_MAX];
    char label[LAUNCHBOX_VERSION_LABEL_MAX];
    char rom_path[LAUNCHBOX_ROM_PATH_MAX];
    char emulator_id[LAUNCHBOX_ID_MAX];
    char platform[LAUNCHBOX_PLATFORM_MAX];
    SDL_bool is_primary;
    SDL_bool is_favorite; /* from <Favorite> on the primary <Game> block only --
                              AdditionalApplication entries don't carry their own
                              and don't need to; build_groups() reads this off the
                              group's primary entry. */
} RawGame;

/* Grows as <Game> and matched <AdditionalApplication> records are found
   while scanning. */
typedef struct {
    RawGame *items;
    int count;
    int capacity;
} RawGameArray;

/* One <Game>'s <ID> -> (title, DatabaseID, Emulator), built while scanning
   <Game> blocks in a file and used to resolve which game each
   <AdditionalApplication> in that same file belongs to (they link back via
   <AdditionalApplication><GameID>, which matches this <ID>). Scoped to a
   single platform XML file -- LaunchBox never links across files. */
typedef struct {
    char id[LB_GUID_MAX];
    char title[LAUNCHBOX_TITLE_MAX];
    char database_id[LB_DB_ID_MAX];
    char emulator_id[LAUNCHBOX_ID_MAX];
} GameIndexEntry;

typedef struct {
    GameIndexEntry *items;
    int count;
    int capacity;
} GameIndexArray;

/* Appends one (zeroed) record to `arr`, growing (doubling) the backing
   array as needed, and returns a pointer to it for the caller to fill in.
   Returns NULL only on allocation failure, in which case the array is left
   untouched and the caller should stop adding more. */
static RawGame *raw_game_array_push(RawGameArray *arr) {
    if (arr->count >= arr->capacity) {
        int new_capacity = (arr->capacity == 0) ? LB_INITIAL_CAPACITY : arr->capacity * 2;
        RawGame *grown = (RawGame *)realloc(arr->items, (size_t)new_capacity * sizeof(RawGame));
        if (!grown) {
            return NULL;
        }
        arr->items = grown;
        arr->capacity = new_capacity;
    }

    RawGame *slot = &arr->items[arr->count];
    memset(slot, 0, sizeof(*slot));
    arr->count++;
    return slot;
}

/* Same push-and-grow pattern as raw_game_array_push, for the per-file
   Game-ID index. */
static GameIndexEntry *game_index_push(GameIndexArray *arr) {
    if (arr->count >= arr->capacity) {
        int new_capacity = (arr->capacity == 0) ? LB_INITIAL_CAPACITY : arr->capacity * 2;
        GameIndexEntry *grown = (GameIndexEntry *)realloc(arr->items, (size_t)new_capacity * sizeof(GameIndexEntry));
        if (!grown) {
            return NULL;
        }
        arr->items = grown;
        arr->capacity = new_capacity;
    }

    GameIndexEntry *slot = &arr->items[arr->count];
    memset(slot, 0, sizeof(*slot));
    arr->count++;
    return slot;
}

static int compare_game_index_by_id(const void *a, const void *b) {
    const GameIndexEntry *ea = (const GameIndexEntry *)a;
    const GameIndexEntry *eb = (const GameIndexEntry *)b;
    return strcmp(ea->id, eb->id);
}

/* Binary search; `index` must already be sorted by compare_game_index_by_id. */
static const GameIndexEntry *game_index_find(const GameIndexArray *index, const char *id) {
    int lo = 0;
    int hi = index->count - 1;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int c = strcmp(index->items[mid].id, id);
        if (c == 0) {
            return &index->items[mid];
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    return NULL;
}

/* Strips leading/trailing whitespace in place. Real LaunchBox data has been
   observed to contain titles with a stray leading space (e.g. " Some
   Game") -- left alone, that shows up as a visibly indented row and sorts
   ahead of everything else (a leading space compares less than any
   letter). */
static void trim_inplace(char *s) {
    char *start = s;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    size_t len = strlen(start);
    while (len > 0 && isspace((unsigned char)start[len - 1])) {
        len--;
    }

    memmove(s, start, len);
    s[len] = '\0';
}

/* Strips a trailing '\' or '/' so "E:\LaunchBox\" and "E:\LaunchBox" both
   produce a clean "E:\LaunchBox\Data\Platforms" join. */
static void trim_trailing_slash(char *s) {
    size_t len = strlen(s);
    if (len > 0 && (s[len - 1] == '\\' || s[len - 1] == '/')) {
        s[len - 1] = '\0';
    }
}

static SDL_bool has_xml_extension(const char *filename) {
    size_t len = strlen(filename);
    if (len < 4) {
        return SDL_FALSE;
    }
    /* Explicit suffix check rather than trusting the FindFirstFile glob --
       Windows' legacy 8.3-era wildcard matching can let "*.xml" match
       something like "Foo.xml.bkp", which LaunchBox leaves lying around
       as backups and which we must not scan as a platform database. */
    return SDL_strcasecmp(filename + len - 4, ".xml") == 0 ? SDL_TRUE : SDL_FALSE;
}

/* "Arcade.xml" -> "Arcade" -- the platform name LaunchBox uses everywhere
   else (Data\Emulators.xml's <Platform> values, a <Game>'s own <Platform>
   field) is just the filename without its extension. `filename` must
   already have been confirmed to end in ".xml" (has_xml_extension). */
static void platform_name_from_filename(const char *filename, char *out, size_t out_cap) {
    size_t len = strlen(filename);
    size_t copy_len = len - 4; /* strip ".xml" */
    if (copy_len >= out_cap) {
        copy_len = out_cap - 1;
    }
    memcpy(out, filename, copy_len);
    out[copy_len] = '\0';
}

/* Strips directory and extension from a ROM path like
   "Roms\0.262\atetris.zip" down to just "atetris". Used as a version
   label fallback when nothing more descriptive (Region/Version, see
   build_version_label) is available. */
static void filename_without_ext(const char *path, char *out, size_t out_cap) {
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

/* Picks the most descriptive label available for an <AdditionalApplication>
   version: LaunchBox's own <Version> text (e.g. "(World 940223)") if
   present, else <Region> (e.g. "World"), else the ROM filename. */
static void build_version_label(const char *region, const char *version, const char *rom_path,
                                 char *out, size_t out_cap) {
    if (version[0]) {
        snprintf(out, out_cap, "%s", version);
    } else if (region[0]) {
        snprintf(out, out_cap, "%s", region);
    } else {
        filename_without_ext(rom_path, out, out_cap);
    }
}

/* Walks every <Game>...</Game> block in `data` and pushes one RawGame per
   block that has a Title (blocks without one, if any, are skipped -- there
   is nothing to show or group). Also records each game's <ID> -> (title,
   DatabaseID, Emulator) in `index` (sorted later by the caller) so that
   extract_additional_apps() can resolve which game its entries belong to.
   Returns how many <Game> blocks were found in this file. */
static int extract_games(const char *data, const char *platform, RawGameArray *arr, GameIndexArray *index) {
    int found = 0;
    const char *p = data;

    while ((p = strstr(p, "<Game>")) != NULL) {
        p += 6; /* skip "<Game>" */
        const char *block_end = strstr(p, "</Game>");
        if (!block_end) {
            break;
        }

        char title[LAUNCHBOX_TITLE_MAX];
        char database_id[LB_DB_ID_MAX];
        char rom_path[LAUNCHBOX_ROM_PATH_MAX];
        char id[LB_GUID_MAX];
        char emulator_id[LAUNCHBOX_ID_MAX];
        char favorite[8];

        xml_extract_field(p, block_end, "<Title>", "</Title>", title, sizeof(title));
        xml_extract_field(p, block_end, "<DatabaseID>", "</DatabaseID>", database_id, sizeof(database_id));
        xml_extract_field(p, block_end, "<ApplicationPath>", "</ApplicationPath>", rom_path, sizeof(rom_path));
        xml_extract_field(p, block_end, "<ID>", "</ID>", id, sizeof(id));
        xml_extract_field(p, block_end, "<Emulator>", "</Emulator>", emulator_id, sizeof(emulator_id));
        xml_extract_field(p, block_end, "<Favorite>", "</Favorite>", favorite, sizeof(favorite));
        trim_inplace(title);
        if (title[0] == '\0') {
            /* Some real LaunchBox entries have a blank or whitespace-only
               Title -- still a launchable game, just unlabeled, so show it
               rather than silently dropping it from the list. */
            snprintf(title, sizeof(title), "NO NAME");
        }

        {
            /* Effective grouping key -- see the comment on RawGame.database_id. */
            const char *group_key = database_id[0] ? database_id : id;

            RawGame *slot = raw_game_array_push(arr);
            if (!slot) {
                SDL_Log("[launchbox] WARNING: out of memory loading titles, truncating scan");
                break;
            }
            snprintf(slot->title, sizeof(slot->title), "%s", title);
            snprintf(slot->database_id, sizeof(slot->database_id), "%s", group_key);
            snprintf(slot->rom_path, sizeof(slot->rom_path), "%s", rom_path);
            snprintf(slot->emulator_id, sizeof(slot->emulator_id), "%s", emulator_id);
            snprintf(slot->platform, sizeof(slot->platform), "%s", platform);
            filename_without_ext(rom_path, slot->label, sizeof(slot->label));
            slot->is_primary = SDL_TRUE;
            slot->is_favorite = (SDL_strcasecmp(favorite, "true") == 0) ? SDL_TRUE : SDL_FALSE;
            found++;

            if (id[0]) {
                GameIndexEntry *entry = game_index_push(index);
                if (entry) {
                    snprintf(entry->id, sizeof(entry->id), "%s", id);
                    snprintf(entry->title, sizeof(entry->title), "%s", title);
                    snprintf(entry->database_id, sizeof(entry->database_id), "%s", group_key);
                    snprintf(entry->emulator_id, sizeof(entry->emulator_id), "%s", emulator_id);
                }
                /* If the index push failed (OOM), this game's Id just won't
                   resolve any AdditionalApplication entries -- the game
                   itself is still in `arr` and shows up fine, just without
                   its alternate versions. */
            }
        }

        p = block_end + 7; /* skip "</Game>" */
    }

    return found;
}

/* Walks every <AdditionalApplication>...</AdditionalApplication> block in
   `data` -- these are siblings of <Game>, not nested inside it, and are
   LaunchBox's mechanism for "alternate ways to launch the same game" (e.g.
   regional ROM variants). Each one's <GameID> is looked up in `index`
   (already built and sorted by the caller); on a match, a RawGame is
   pushed with the *parent* game's title/DatabaseID but this entry's own
   ApplicationPath and Region/Version-derived label, so it naturally merges
   into that game's group in build_groups(). The emulator comes from this
   entry's own <EmulatorId> (note: not <Emulator> -- LaunchBox uses a
   different field name here than on <Game>), falling back to the parent's
   emulator if this entry doesn't specify one. Entries whose GameID doesn't
   resolve (the parent <Game> was missing, malformed, or in a file this
   scanner didn't read) are silently skipped. Returns how many were
   matched. */
static int extract_additional_apps(const char *data, const char *platform,
                                    const GameIndexArray *index, RawGameArray *arr) {
    static const char OPEN_TAG[] = "<AdditionalApplication>";
    static const char CLOSE_TAG[] = "</AdditionalApplication>";

    int found = 0;
    const char *p = data;

    while ((p = strstr(p, OPEN_TAG)) != NULL) {
        p += sizeof(OPEN_TAG) - 1; /* -1 for the implicit trailing '\0' */
        const char *block_end = strstr(p, CLOSE_TAG);
        if (!block_end) {
            break;
        }

        char game_id[LB_GUID_MAX];

        xml_extract_field(p, block_end, "<GameID>", "</GameID>", game_id, sizeof(game_id));

        if (game_id[0]) {
            const GameIndexEntry *parent = game_index_find(index, game_id);
            if (parent) {
                char region[LB_FIELD_MAX];
                char version[LB_FIELD_MAX];
                char rom_path[LAUNCHBOX_ROM_PATH_MAX];
                char emulator_id[LAUNCHBOX_ID_MAX];

                xml_extract_field(p, block_end, "<Region>", "</Region>", region, sizeof(region));
                xml_extract_field(p, block_end, "<Version>", "</Version>", version, sizeof(version));
                xml_extract_field(p, block_end, "<ApplicationPath>", "</ApplicationPath>", rom_path, sizeof(rom_path));
                xml_extract_field(p, block_end, "<EmulatorId>", "</EmulatorId>", emulator_id, sizeof(emulator_id));

                RawGame *slot = raw_game_array_push(arr);
                if (!slot) {
                    SDL_Log("[launchbox] WARNING: out of memory loading versions, truncating scan");
                    break;
                }
                snprintf(slot->title, sizeof(slot->title), "%s", parent->title);
                snprintf(slot->database_id, sizeof(slot->database_id), "%s", parent->database_id);
                snprintf(slot->rom_path, sizeof(slot->rom_path), "%s", rom_path);
                snprintf(slot->emulator_id, sizeof(slot->emulator_id), "%s",
                         emulator_id[0] ? emulator_id : parent->emulator_id);
                snprintf(slot->platform, sizeof(slot->platform), "%s", platform);
                build_version_label(region, version, rom_path, slot->label, sizeof(slot->label));
                /* slot->is_primary is already SDL_FALSE -- raw_game_array_push() zeroes new slots. */
                found++;
            }
        }

        p = block_end + (sizeof(CLOSE_TAG) - 1);
    }

    return found;
}

/* Case-insensitive by title so "zelda" and "Zelda" land next to each
   other, THEN by grouping key, THEN primary-before-additional, THEN by
   label. The grouping-key tier is what build_groups()'s adjacency scan
   depends on -- without it, two unrelated games that happen to share a
   title (confirmed to exist in real data: 29 duplicate titles in
   Arcade.xml alone are not all clone groups like Tetris) could sort as
   title-A, title-B, title-A, interleaved by label, splitting title-A's
   own versions into two separate groups instead of leaving them adjacent.
   The primary tier guarantees version_start (see RawGame.is_primary)
   without needing the label text to cooperate -- an AdditionalApplication
   labeled e.g. "(Asia 940223)" could otherwise sort before the primary
   entry's own ROM-filename label alphabetically. Doesn't special-case
   articles ("The", "A") the way LaunchBox's own SortTitle field would;
   that's real metadata this scanner doesn't read. */
static int compare_raw_games(const void *a, const void *b) {
    const RawGame *ga = (const RawGame *)a;
    const RawGame *gb = (const RawGame *)b;
    int c = SDL_strcasecmp(ga->title, gb->title);
    if (c != 0) {
        return c;
    }
    c = strcmp(ga->database_id, gb->database_id);
    if (c != 0) {
        return c;
    }
    if (ga->is_primary != gb->is_primary) {
        return ga->is_primary ? -1 : 1;
    }
    return SDL_strcasecmp(ga->label, gb->label);
}

/* Collapses consecutive RawGame entries that share a non-empty grouping
   key (RawGame.database_id -- see its doc comment for what that key
   actually is) into LaunchboxGameGroup + LaunchboxVersion entries in
   `out`. This is what unifies both of LaunchBox's "alternate version"
   mechanisms into one row: separate <Game> entries sharing a real
   DatabaseID (e.g. six arcade "Tetris" ROMs), and a <Game> plus its
   <AdditionalApplication> entries (e.g. SSF2T's regional variants), which
   extract_additional_apps() already tagged with their parent's key.
   Entries with an empty key are never merged with anything -- not even
   each other -- but in practice that only happens if a <Game> block is
   missing both <DatabaseID> and <ID>, which real LaunchBox data doesn't
   do.

   This is a linear scan over the *already-sorted-by-title* array, not a
   hash-based group-by: it assumes entries sharing a grouping key also
   share a Title, and therefore end up adjacent after the title sort. For
   DatabaseID-based grouping that's an assumption about the data (true of
   every real LaunchBox database observed); for AdditionalApplication-based
   grouping it's guaranteed, since the title is copied directly from the
   resolved parent. If a same-key-different-title case ever occurred, the
   later entry would incorrectly start a new group instead of joining the
   first one -- this scanner has no way to detect or correct that. */
static void build_groups(RawGameArray *raw, LaunchboxInfo *out) {
    if (raw->count == 0) {
        return;
    }

    LaunchboxVersion *versions = (LaunchboxVersion *)malloc((size_t)raw->count * sizeof(LaunchboxVersion));
    LaunchboxGameGroup *groups = (LaunchboxGameGroup *)malloc((size_t)raw->count * sizeof(LaunchboxGameGroup));
    if (!versions || !groups) {
        SDL_Log("[launchbox] WARNING: out of memory building game groups");
        free(versions);
        free(groups);
        return;
    }

    int version_count = 0;
    int group_count = 0;

    int i = 0;
    while (i < raw->count) {
        int j = i + 1;
        if (raw->items[i].database_id[0] != '\0') {
            while (j < raw->count && strcmp(raw->items[j].database_id, raw->items[i].database_id) == 0) {
                j++;
            }
        }

        LaunchboxGameGroup *grp = &groups[group_count++];
        snprintf(grp->title, sizeof(grp->title), "%s", raw->items[i].title);
        grp->version_start = version_count;
        grp->version_count = j - i;
        /* raw->items[i] is guaranteed to be the group's primary <Game> entry
           (compare_raw_games sorts is_primary before additional-app entries
           within a shared grouping key), so its Favorite flag is the one
           that actually came from LaunchBox, not an AdditionalApplication
           that never carries its own. */
        grp->is_favorite = raw->items[i].is_favorite;

        for (int k = i; k < j; k++) {
            LaunchboxVersion *ver = &versions[version_count];
            snprintf(ver->label, sizeof(ver->label), "%s", raw->items[k].label);
            snprintf(ver->rom_path, sizeof(ver->rom_path), "%s", raw->items[k].rom_path);
            snprintf(ver->emulator_id, sizeof(ver->emulator_id), "%s", raw->items[k].emulator_id);
            snprintf(ver->platform, sizeof(ver->platform), "%s", raw->items[k].platform);
            version_count++;
        }

        i = j;
    }

    out->groups = groups;
    out->group_count = group_count;
    out->versions = versions;
    out->version_count = version_count;
}

/* Stable-partitions out->groups so every favorite comes before every
   non-favorite, preserving the existing (alphabetical-by-title) relative
   order within each half -- build_groups() is called first and already
   produced a title-sorted array, so this just splits it into two still-
   sorted runs rather than re-sorting anything. Only touches the groups
   array (title/version_start/version_count/is_favorite); versions[] and
   the version_start indices inside each group stay valid since nothing
   about the versions themselves moves. Sets out->favorite_count. On
   allocation failure, leaves the plain alphabetical order in place and
   favorite_count at 0 -- worse UX (no favorites-first sort) but not a
   crash. */
static void partition_favorites_first(LaunchboxInfo *out) {
    if (out->group_count == 0) {
        return;
    }

    LaunchboxGameGroup *reordered =
        (LaunchboxGameGroup *)malloc((size_t)out->group_count * sizeof(LaunchboxGameGroup));
    if (!reordered) {
        SDL_Log("[launchbox] WARNING: out of memory sorting favorites first, leaving alphabetical order");
        return;
    }

    int favorite_count = 0;
    for (int i = 0; i < out->group_count; i++) {
        if (out->groups[i].is_favorite) {
            reordered[favorite_count++] = out->groups[i];
        }
    }
    int idx = favorite_count;
    for (int i = 0; i < out->group_count; i++) {
        if (!out->groups[i].is_favorite) {
            reordered[idx++] = out->groups[i];
        }
    }

    free(out->groups);
    out->groups = reordered;
    out->favorite_count = favorite_count;
}

#ifdef _WIN32
static void scan_platforms_dir(const char *platforms_dir, LaunchboxInfo *out) {
    char pattern[LB_PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s\\*.xml", platforms_dir);

    WIN32_FIND_DATAA fd;
    HANDLE find = FindFirstFileA(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE) {
        out->status = LAUNCHBOX_STATUS_NO_PLATFORMS;
        SDL_Log("[launchbox] WARNING: no *.xml platform files found in '%s'", platforms_dir);
        return;
    }

    RawGameArray raw = {0};
    int platform_count = 0;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if (!has_xml_extension(fd.cFileName)) {
            continue;
        }

        char file_path[LB_PATH_MAX];
        snprintf(file_path, sizeof(file_path), "%s\\%s", platforms_dir, fd.cFileName);

        char platform[LAUNCHBOX_PLATFORM_MAX];
        platform_name_from_filename(fd.cFileName, platform, sizeof(platform));

        long len = 0;
        char *data = xml_read_entire_file(file_path, &len);
        if (!data) {
            SDL_Log("[launchbox] WARNING: could not open '%s', skipping", file_path);
            continue;
        }

        /* Two passes over the same file: first every <Game>, building a
           per-file GameID index alongside; then every
           <AdditionalApplication>, which needs that index (sorted first)
           to resolve which game each one belongs to. */
        GameIndexArray index = {0};
        int games_found = extract_games(data, platform, &raw, &index);

        qsort(index.items, (size_t)index.count, sizeof(GameIndexEntry), compare_game_index_by_id);
        int versions_found = extract_additional_apps(data, platform, &index, &raw);
        free(index.items);

        free(data);

        platform_count++;
        SDL_Log("[launchbox] Scanned '%s': %d game(s), %d additional version(s)",
                file_path, games_found, versions_found);
    } while (FindNextFileA(find, &fd));

    FindClose(find);

    if (raw.count > 0) {
        qsort(raw.items, (size_t)raw.count, sizeof(RawGame), compare_raw_games);
        build_groups(&raw, out);
        partition_favorites_first(out);
    }
    free(raw.items);

    out->platform_count = platform_count;
    out->status = (platform_count > 0) ? LAUNCHBOX_STATUS_LOADED : LAUNCHBOX_STATUS_NO_PLATFORMS;
}
#endif /* _WIN32 */

void launchbox_load(const char *launchbox_dir, LaunchboxInfo *out) {
    SDL_zerop(out);

    if (!launchbox_dir || launchbox_dir[0] == '\0') {
        out->status = LAUNCHBOX_STATUS_NOT_CONFIGURED;
        SDL_Log("[launchbox] No launchbox_dir configured, skipping");
        return;
    }

#ifdef _WIN32
    char root[LB_PATH_MAX];
    snprintf(root, sizeof(root), "%s", launchbox_dir);
    trim_trailing_slash(root);

    char platforms_dir[LB_PATH_MAX];
    snprintf(platforms_dir, sizeof(platforms_dir), "%s\\Data\\Platforms", root);

    DWORD attrs = GetFileAttributesA(platforms_dir);
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        out->status = LAUNCHBOX_STATUS_DIR_NOT_FOUND;
        SDL_Log("[launchbox] WARNING: '%s' not found -- is launchbox_dir set to the LaunchBox "
                "install root (the folder containing LaunchBox.exe)?", platforms_dir);
        return;
    }

    SDL_Log("[launchbox] Scanning platform database at '%s' (reads are restricted to this folder)",
            platforms_dir);
    scan_platforms_dir(platforms_dir, out);

    if (out->status == LAUNCHBOX_STATUS_LOADED) {
        int multi_version_groups = 0;
        for (int i = 0; i < out->group_count; i++) {
            if (out->groups[i].version_count > 1) {
                multi_version_groups++;
            }
        }
        SDL_Log("[launchbox] Done: %d platform(s), %d game(s) total, %d unique title(s) "
                "(%d with multiple versions, %d favorite(s))",
                out->platform_count, out->version_count, out->group_count, multi_version_groups,
                out->favorite_count);
    } else if (out->status == LAUNCHBOX_STATUS_NO_PLATFORMS) {
        SDL_Log("[launchbox] WARNING: '%s' has no readable *.xml platform files", platforms_dir);
    }
#else
    out->status = LAUNCHBOX_STATUS_DIR_NOT_FOUND;
    SDL_Log("[launchbox] WARNING: launchbox_dir scanning is only implemented for Windows");
#endif
}

void launchbox_free(LaunchboxInfo *info) {
    free(info->groups);
    free(info->versions);
    info->groups = NULL;
    info->versions = NULL;
    info->group_count = 0;
    info->version_count = 0;
}
