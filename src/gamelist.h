#ifndef CRT_GAMELIST_H
#define CRT_GAMELIST_H

#include <SDL.h>
#include <stddef.h>

#include "launchbox.h"

/* System-menu rows pinned above the platform/game rows, in the same flat
   scrollable row space (revealed by scrolling up past the top). */
#define GAMELIST_SYSTEM_ENTRY_COUNT 2
extern const char *const gamelist_system_entry_labels[GAMELIST_SYSTEM_ENTRY_COUNT];

/* The startup-toggle row: render.c overrides its label each frame from
   live registry state (startup_is_enabled), and main.c dispatches SELECT
   straight to startup_enable()/startup_disable() -- no modal. */
#define GAMELIST_SYSTEM_ENTRY_STARTUP 1

/* Pure navigation state -- no rendering, no launching. The unified row
   space is: system rows, then one toggle row per platform, then the
   *filtered* game rows (visible_group_indices, not lb->groups directly).
   While the version-picker modal is open (selected_version >= 0),
   navigation is scoped to it and the list underneath is inert. */
typedef struct {
    /* Index into the unified row space. Use the helpers below instead of
       comparing/indexing directly. */
    int selected_group;
    int selected_version;  /* -1 = version-picker closed, else its focused row */
    SDL_bool system_modal_open;
    char system_modal_status[64]; /* modal body text, owned by main.c */
    char system_modal_hint[64];   /* optional second line; empty = none */
    SDL_bool exit_confirm_open;   /* "really quit?" modal */
    int scroll_offset;            /* topmost drawn row */

    /* Per-platform visibility, parallel to lb->platform_names. NULL when
       there are no platforms. */
    SDL_bool *platform_selected;

    /* Indices into lb->groups for the currently visible games, in
       lb->groups order. What every navigation function walks. */
    int *visible_group_indices;
    int visible_group_count;
} GameListState;

/* Starts on the first visible game (system/platform rows hidden above the
   top), or the first system row if nothing is visible. Allocates
   platform_selected (resolved from config.ini's raw selected_platforms
   value) and visible_group_indices; freed by gamelist_free(). */
void gamelist_init(GameListState *state, const LaunchboxInfo *lb, const char *selected_platforms_csv);

void gamelist_free(GameListState *state);

SDL_bool gamelist_selected_is_system(const GameListState *state);

SDL_bool gamelist_selected_is_platform(const GameListState *state, const LaunchboxInfo *lb);

/* Index into lb->platform_names/platform_selected for the selected
   platform row. Only meaningful when gamelist_selected_is_platform(). */
int gamelist_selected_platform_index(const GameListState *state);

/* The group the selection points at (via visible_group_indices), or NULL
   for system/platform rows or out-of-range. */
const LaunchboxGameGroup *gamelist_selected_group(const GameListState *state, const LaunchboxInfo *lb);

/* Flips one platform checkbox and recomputes the visible set. Callers
   persist the change (gamelist_format_platform_selection +
   config_save_selected_platforms). */
void gamelist_toggle_platform(GameListState *state, const LaunchboxInfo *lb, int platform_index);

/* Formats the selection as config.ini expects: "All", "None", or a
   comma-separated name list. */
void gamelist_format_platform_selection(const GameListState *state, const LaunchboxInfo *lb,
                                         char *out, size_t out_cap);

/* Moves the selection by `delta` (or within the version-picker modal when
   open). The games wrap on themselves -- past the last one comes back to
   the first, not into the settings rows; those are reached by going up
   from the first game, and stop at the top. No-op while the system modal
   or exit confirmation is up -- moving selected_group under an open
   system modal would corrupt render.c's label lookup. */
void gamelist_move(GameListState *state, const LaunchboxInfo *lb, int delta);

/* Jumps to the previous/next first-letter run among visible games,
   wrapping. Games-only: no-op on system/platform rows or under a modal. */
void gamelist_jump_letter(GameListState *state, const LaunchboxInfo *lb, int direction);

/* Opens/closes the version picker for the selected group. No-op for
   single-version groups, non-game rows, or under another modal. */
void gamelist_toggle_expand(GameListState *state, const LaunchboxInfo *lb);

/* Keeps the selected row within the `visible_rows` window. Call each
   frame after layout. */
void gamelist_scroll_into_view(GameListState *state, const LaunchboxInfo *lb, int visible_rows);

#endif /* CRT_GAMELIST_H */
