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
/* Fits either a numeric DatabaseID or a full <ID> GUID (the fallback key). */
#define LB_DB_ID_MAX LB_GUID_MAX

/* One launchable entry straight off the XML, before grouping -- a <Game>
   block's own ApplicationPath or one of its <AdditionalApplication>s.
   `database_id` is the effective grouping key: the real <DatabaseID> when
   present (how LaunchBox marks clones as "the same game"), else the
   game's own unique <ID> so its AdditionalApplications still merge.
   `is_primary` marks <Game>-block entries; compare_raw_games sorts them
   first so version_start always points at the default launch target. */
typedef struct {
    char title[LAUNCHBOX_TITLE_MAX];
    char database_id[LB_DB_ID_MAX];
    char label[LAUNCHBOX_VERSION_LABEL_MAX];
    char rom_path[LAUNCHBOX_ROM_PATH_MAX];
    char emulator_id[LAUNCHBOX_ID_MAX];
    char platform[LAUNCHBOX_PLATFORM_MAX];
    SDL_bool is_primary;
    SDL_bool is_favorite; /* from the primary <Game>'s <Favorite> only */
} RawGame;

typedef struct {
    RawGame *items;
    int count;
    int capacity;
} RawGameArray;

typedef char PlatformName[LAUNCHBOX_PLATFORM_MAX];

typedef struct {
    PlatformName *items;
    int count;
    int capacity;
} PlatformNameArray;

/* Per-file <ID> -> (title, DatabaseID, Emulator) index for resolving
   <AdditionalApplication><GameID> links. LaunchBox never links across
   files. */
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

/* Push-and-grow; returns a zeroed slot, or NULL on allocation failure. */
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

static PlatformName *platform_name_array_push(PlatformNameArray *arr) {
    if (arr->count >= arr->capacity) {
        int new_capacity = (arr->capacity == 0) ? 16 : arr->capacity * 2;
        PlatformName *grown = (PlatformName *)realloc(arr->items, (size_t)new_capacity * sizeof(PlatformName));
        if (!grown) {
            return NULL;
        }
        arr->items = grown;
        arr->capacity = new_capacity;
    }

    PlatformName *slot = &arr->items[arr->count];
    arr->count++;
    return slot;
}

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

/* Binary search; `index` must be sorted by compare_game_index_by_id. */
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

/* Real LaunchBox data contains titles with stray leading spaces, which
   would sort first and render indented. */
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
    /* Windows' 8.3-era wildcard matching lets "*.xml" match "Foo.xml.bkp"
       backups, so check the suffix explicitly. */
    return SDL_strcasecmp(filename + len - 4, ".xml") == 0 ? SDL_TRUE : SDL_FALSE;
}

/* "Arcade.xml" -> "Arcade" (the platform name LaunchBox uses everywhere).
   Caller has already verified the ".xml" suffix. */
static void platform_name_from_filename(const char *filename, char *out, size_t out_cap) {
    size_t len = strlen(filename);
    size_t copy_len = len - 4;
    if (copy_len >= out_cap) {
        copy_len = out_cap - 1;
    }
    memcpy(out, filename, copy_len);
    out[copy_len] = '\0';
}

/* "Roms\0.262\atetris.zip" -> "atetris". */
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

/* Version label preference: <Version> text, else <Region>, else the ROM
   filename. Same order for <Game> and <AdditionalApplication> entries. */
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

/* Pushes one RawGame per <Game> block and records each <ID> in `index`
   (sorted later by the caller) for AdditionalApplication resolution.
   Returns the number of blocks found. */
static int extract_games(const char *data, const char *platform, RawGameArray *arr, GameIndexArray *index) {
    int found = 0;
    const char *p = data;

    while ((p = strstr(p, "<Game>")) != NULL) {
        p += 6;
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
        char region[LB_FIELD_MAX];
        char version[LB_FIELD_MAX];

        xml_extract_field(p, block_end, "<Title>", "</Title>", title, sizeof(title));
        xml_extract_field(p, block_end, "<DatabaseID>", "</DatabaseID>", database_id, sizeof(database_id));
        xml_extract_field(p, block_end, "<ApplicationPath>", "</ApplicationPath>", rom_path, sizeof(rom_path));
        xml_extract_field(p, block_end, "<ID>", "</ID>", id, sizeof(id));
        xml_extract_field(p, block_end, "<Emulator>", "</Emulator>", emulator_id, sizeof(emulator_id));
        xml_extract_field(p, block_end, "<Favorite>", "</Favorite>", favorite, sizeof(favorite));
        xml_extract_field(p, block_end, "<Region>", "</Region>", region, sizeof(region));
        xml_extract_field(p, block_end, "<Version>", "</Version>", version, sizeof(version));
        trim_inplace(title);
        if (title[0] == '\0') {
            /* Blank titles exist in real data; show them rather than drop. */
            snprintf(title, sizeof(title), "NO NAME");
        }

        {
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
            build_version_label(region, version, rom_path, slot->label, sizeof(slot->label));
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
            }
        }

        p = block_end + 7;
    }

    return found;
}

/* <AdditionalApplication> blocks are siblings of <Game> -- LaunchBox's
   mechanism for regional/alternate versions. Each resolves via <GameID>
   to its parent and is pushed with the parent's title/key so it merges
   into the same group. Emulator comes from this entry's <EmulatorId>
   (different field name than <Game>'s <Emulator>), falling back to the
   parent's. Unresolvable entries are skipped. */
static int extract_additional_apps(const char *data, const char *platform,
                                    const GameIndexArray *index, RawGameArray *arr) {
    static const char OPEN_TAG[] = "<AdditionalApplication>";
    static const char CLOSE_TAG[] = "</AdditionalApplication>";

    int found = 0;
    const char *p = data;

    while ((p = strstr(p, OPEN_TAG)) != NULL) {
        p += sizeof(OPEN_TAG) - 1;
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
                found++;
            }
        }

        p = block_end + (sizeof(CLOSE_TAG) - 1);
    }

    return found;
}

/* Title (case-insensitive), then grouping key, then primary-first, then
   label. The key tier keeps same-titled but unrelated games from
   interleaving (real data has such duplicates); the primary tier
   guarantees version_start points at the game's own default entry. */
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

/* Collapses consecutive same-key entries of the sorted array into
   group + version records. Adjacency relies on same-key entries sharing a
   title (true of all observed LaunchBox data; guaranteed for
   AdditionalApplication entries, which copy the parent's title). */
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
        /* items[i] is the primary entry (sort order), so its Favorite flag
           is the real one. */
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

/* Stable partition: favorites first, alphabetical order preserved within
   each half. versions[] and version_start indices stay valid. On
   allocation failure, keeps plain alphabetical order. */
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
    PlatformNameArray platform_names = {0};

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

        PlatformName *name_slot = platform_name_array_push(&platform_names);
        if (name_slot) {
            snprintf(*name_slot, sizeof(*name_slot), "%s", platform);
        }

        long len = 0;
        char *data = xml_read_entire_file(file_path, &len);
        if (!data) {
            SDL_Log("[launchbox] WARNING: could not open '%s', skipping", file_path);
            continue;
        }

        /* Two passes: all <Game>s (building the per-file ID index), then
           all <AdditionalApplication>s resolved against it. */
        GameIndexArray index = {0};
        int games_found = extract_games(data, platform, &raw, &index);

        qsort(index.items, (size_t)index.count, sizeof(GameIndexEntry), compare_game_index_by_id);
        int versions_found = extract_additional_apps(data, platform, &index, &raw);
        free(index.items);

        free(data);

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

    out->platform_names = platform_names.items;
    out->platform_count = platform_names.count;
    out->status = (out->platform_count > 0) ? LAUNCHBOX_STATUS_LOADED : LAUNCHBOX_STATUS_NO_PLATFORMS;
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
    free(info->platform_names);
    info->groups = NULL;
    info->versions = NULL;
    info->platform_names = NULL;
    info->group_count = 0;
    info->version_count = 0;
    info->platform_count = 0;
}
