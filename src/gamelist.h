#ifndef CRT_GAMELIST_H
#define CRT_GAMELIST_H

#include <SDL.h>
#include <stddef.h>

#include "launchbox.h"

/* Always-present rows pinned above the platform/game list -- a small,
   growable system menu (currently just a calibration placeholder; more
   settings-style entries are expected to join it later). They live in the
   same flat, scrollable row space as the platform toggle rows and
   LaunchboxGameGroup rows (system rows first, then platforms, then
   favorites, then the rest -- see selected_group below), not a separate
   screen, so scrolling up from the first favorite/game reveals them like
   any other row. */
#define GAMELIST_SYSTEM_ENTRY_COUNT 1
extern const char *const gamelist_system_entry_labels[GAMELIST_SYSTEM_ENTRY_COUNT];

/*
 * Pure navigation state for the on-screen game list. No rendering and no
 * launching lives here -- see render.c for drawing the list and the
 * version-picker/system modals, and main.c for what "launch" does.
 *
 * The unified row space has three pinned sections, in order: system rows,
 * one toggleable row per platform LaunchBox reported, then real game
 * rows -- but that last section isn't just lb->groups verbatim anymore:
 * it's filtered down to whichever platforms are checked (visible_group_*
 * below), so unchecking a platform hides its games without touching
 * LaunchboxInfo itself.
 *
 * When the selected group has multiple versions, Shift+Enter opens a
 * modal (see render_draw_modal_list in render.c) to pick one; while it's
 * open, selected_version tracks which row in *that* modal is focused, and
 * Up/Down/Left/Right all stay scoped to it -- the underlying list is inert
 * until the modal closes. system_modal_open is the equivalent flag for a
 * system entry's own modal.
 */
typedef struct {
    /* Index into the unified row space:
       0 .. GAMELIST_SYSTEM_ENTRY_COUNT-1                          -> system row
       .. + lb->platform_count-1                                   -> platform toggle row
       .. + visible_group_count-1                                  -> a real group, at
                                                                        lb->groups[visible_group_indices[i]]
       Use gamelist_selected_is_system()/gamelist_selected_is_platform()/
       gamelist_selected_group() rather than comparing or indexing
       directly. */
    int selected_group;
    int selected_version;  /* -1 = the version-picker modal is closed;
                               0..version_count-1 = that row is focused
                               within the open modal. Only meaningful when
                               the selection is a real group. */
    SDL_bool system_modal_open; /* the focused system entry's own modal --
                                    placeholder for now, no real behavior
                                    wired up yet */
    char system_modal_status[64]; /* freeform text the system modal shows as
                                      its body -- main.c owns what goes in
                                      here (currently the Calibrate Controls
                                      flow's "press input for X" prompts and
                                      its completion message), render.c just
                                      displays it */
    char system_modal_hint[64];   /* optional second, un-highlighted line
                                      below system_modal_status -- empty
                                      string means "don't show one". Used
                                      for the "ESC WILL EXIT CALIBRATION"
                                      reminder while a calibration step is
                                      active; main.c clears it for the
                                      completion message, where Esc no
                                      longer has that special meaning */
    SDL_bool exit_confirm_open; /* SDL_TRUE while the "really quit?"
                                    confirmation (triggered by BACK at the
                                    top level of the main list) is up --
                                    distinct from system_modal_open, since
                                    this isn't tied to any system-menu row */
    int scroll_offset;     /* row index (same unified space as selected_group)
                               of the topmost row currently drawn */

    /* Heap array, lb->platform_count entries, parallel to
       lb->platform_names[] -- SDL_TRUE means games from that platform
       currently show up in the filtered list below. NULL if
       lb->platform_count is 0 (nothing to allocate). Initialized from
       config.ini's selected_platforms value by gamelist_init(); flipped
       one at a time by gamelist_toggle_platform(). */
    SDL_bool *platform_selected;

    /* Heap array, up to lb->group_count entries -- indices into
       lb->groups[], in lb->groups' own order (favorites-first, then
       alphabetical), naming exactly the groups whose platform is
       currently checked in platform_selected. This is what every
       navigation function actually walks for the "real game" section of
       the row space, instead of lb->groups directly -- see
       gamelist_recompute_visible_groups() in gamelist.c. NULL if
       lb->group_count is 0. */
    int *visible_group_indices;
    int visible_group_count;   /* how many of visible_group_indices are populated */
} GameListState;

/* Starts the selection on the first visible game row, so a fresh launch
   shows the game list first -- the system and platform rows are only
   revealed by scrolling up past the top. Falls back to the first system
   row if there isn't a single visible game (no LaunchBox data at all, or
   every platform happens to be unchecked). Allocates platform_selected
   (resolved from `selected_platforms_csv` -- config.ini's raw
   selected_platforms value: "All", "None", or a comma-separated platform
   name list, see AppConfig.selected_platforms) and visible_group_indices;
   both are owned by `state` until gamelist_free(). */
void gamelist_init(GameListState *state, const LaunchboxInfo *lb, const char *selected_platforms_csv);

/* Frees platform_selected and visible_group_indices. Safe to call
   regardless of prior state, including one gamelist_init() never
   allocated anything into (lb->platform_count == 0 and/or
   lb->group_count == 0). */
void gamelist_free(GameListState *state);

/* SDL_TRUE if the current selection is a system row rather than a
   platform row or a real LaunchboxGameGroup. */
SDL_bool gamelist_selected_is_system(const GameListState *state);

/* SDL_TRUE if the current selection is one of the platform toggle rows
   (as opposed to a system row or a real LaunchboxGameGroup). */
SDL_bool gamelist_selected_is_platform(const GameListState *state, const LaunchboxInfo *lb);

/* Index into lb->platform_names[]/state->platform_selected[] for the
   currently selected platform row. Only meaningful when
   gamelist_selected_is_platform() is true. */
int gamelist_selected_platform_index(const GameListState *state);

/* The LaunchboxGameGroup the current selection points at, translating
   through visible_group_indices -- or NULL if the selection is a system
   row, a platform row, or (defensively) out of range. Centralizes the
   "selected_group -> real lb->groups[] entry" lookup so main.c and
   render.c don't each duplicate the row-space arithmetic. */
const LaunchboxGameGroup *gamelist_selected_group(const GameListState *state, const LaunchboxInfo *lb);

/* Flips state->platform_selected[platform_index] and recomputes
   visible_group_indices to match. No-op if platform_index is out of
   range or platform_selected wasn't allocated. Doesn't move
   selected_group -- the only caller (main.c, on SELECT) only ever invokes
   this while a platform row is itself the current selection, so there's
   nothing to re-clamp. Callers are responsible for persisting the new
   selection (see gamelist_format_platform_selection and
   config_save_selected_platforms). */
void gamelist_toggle_platform(GameListState *state, const LaunchboxInfo *lb, int platform_index);

/* Formats the current platform selection the same way config.ini's
   selected_platforms key expects it -- "All" if every platform is
   checked, "None" if none are, else a comma-separated list of the
   checked platforms' names (in lb->platform_names order). Inverse of the
   parsing gamelist_init() does on config.ini's raw value. Writes "All"
   if platform_selected wasn't allocated (lb->platform_count == 0). */
void gamelist_format_platform_selection(const GameListState *state, const LaunchboxInfo *lb,
                                         char *out, size_t out_cap);

/* Moves the selection by `delta` steps (negative = up, positive = down),
   wrapping across the whole unified row space (system rows, then platform
   rows, then visible groups) at the ends. If the version-picker modal is
   open, this instead wraps within its rows only (0..version_count-1) and
   never touches selected_group. No-op while a system entry's own modal is
   open (system_modal_open) or the exit confirmation is (exit_confirm_open)
   -- neither has anything to navigate, and moving selected_group out from
   under the system modal specifically would corrupt render.c's
   gamelist_system_entry_labels[] lookup. Otherwise always active, even if
   there isn't a single visible group -- the system and platform rows
   still need to be navigable then. */
void gamelist_move(GameListState *state, const LaunchboxInfo *lb, int delta);

/* Jumps to the start of the next (`direction` > 0) or previous
   (`direction` < 0) run of visible groups sharing the same first letter
   (case-insensitive), wrapping around at the ends, and closes the
   version-picker modal if it was open. Only ever compares adjacent
   entries within visible_group_indices, so it works fine against the
   favorites-first order described on LaunchboxInfo (a letter can then
   appear as two separate runs -- once in the favorites block, once in the
   rest -- and this just visits both in turn). No-op if there isn't a
   single visible group, the current selection is a system or platform
   row, or a modal (system or exit-confirm) is open -- letter-jumping is a
   real-games-only feature. */
void gamelist_jump_letter(GameListState *state, const LaunchboxInfo *lb, int direction);

/* Opens or closes the version-picker modal for the selected group (bound
   to Shift+Enter, and to Esc while it's open). Opening focuses version 0;
   closing refocuses the group's own row. No-op if the group has only one
   version -- nothing to pick -- if there isn't a real group selected
   (gamelist_selected_group() returns NULL: a system row, a platform row,
   or nothing visible), if the current selection is a system row (system
   entries have their own modal -- system_modal_open -- driven separately
   by main.c), or if the exit confirmation is open. */
void gamelist_toggle_expand(GameListState *state, const LaunchboxInfo *lb);

/* Adjusts scroll_offset (a row index in the same unified space as
   selected_group) so the selected row stays within a `visible_rows`-row
   window. Call every frame after layout determines how many rows fit.
   Only cares about selected_group -- the version-picker/system modals,
   when open, are drawn as their own overlay and never take up rows in
   this scrolling list. */
void gamelist_scroll_into_view(GameListState *state, const LaunchboxInfo *lb, int visible_rows);

#endif /* CRT_GAMELIST_H */
