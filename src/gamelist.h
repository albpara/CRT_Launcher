#ifndef CRT_GAMELIST_H
#define CRT_GAMELIST_H

#include <SDL.h>

#include "launchbox.h"

/* Always-present rows pinned above the favorites/game list -- a small,
   growable system menu (currently just a calibration placeholder; more
   settings-style entries are expected to join it later). They live in the
   same flat, scrollable row space as LaunchboxGameGroup rows (system rows
   first, then favorites, then the rest -- see selected_group below), not
   a separate screen, so scrolling up from the first favorite/game reveals
   them like any other row. */
#define GAMELIST_SYSTEM_ENTRY_COUNT 1
extern const char *const gamelist_system_entry_labels[GAMELIST_SYSTEM_ENTRY_COUNT];

/*
 * Pure navigation state for the on-screen game list. No rendering and no
 * launching lives here -- see render.c for drawing the list and the
 * version-picker/system modals, and main.c for what "launch" does.
 *
 * The list itself is always flat -- one row per system entry or
 * LaunchboxGameGroup, never anything nested inline. When the selected
 * group has multiple versions, Shift+Enter opens a modal (see
 * render_draw_modal_list in render.c) to pick one; while it's open,
 * selected_version tracks which row in *that* modal is focused, and
 * Up/Down/Left/Right all stay scoped to it -- the underlying list is inert
 * until the modal closes. system_modal_open is the equivalent flag for a
 * system entry's own modal.
 */
typedef struct {
    /* Index into the unified row space: 0..GAMELIST_SYSTEM_ENTRY_COUNT-1
       is a system row (gamelist_system_entry_labels[selected_group]);
       GAMELIST_SYSTEM_ENTRY_COUNT..+lb->group_count-1 is a real group, at
       lb->groups[selected_group - GAMELIST_SYSTEM_ENTRY_COUNT]. Use
       gamelist_selected_is_system() rather than comparing directly. */
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
} GameListState;

/* Starts the selection just past the system section (at the first
   favorite/game) whenever `lb` has at least one group, so a fresh launch
   shows the game list first -- the system rows are only revealed by
   scrolling up past the top. Falls back to the first system row if `lb`
   has zero groups (nothing else to land on). */
void gamelist_init(GameListState *state, const LaunchboxInfo *lb);

/* SDL_TRUE if the current selection is a system row rather than a real
   LaunchboxGameGroup. */
SDL_bool gamelist_selected_is_system(const GameListState *state);

/* Moves the selection by `delta` steps (negative = up, positive = down),
   wrapping across the whole unified row space (system rows, then groups)
   at the ends. If the version-picker modal is open, this instead wraps
   within its rows only (0..version_count-1) and never touches
   selected_group. No-op while a system entry's own modal is open
   (system_modal_open) or the exit confirmation is (exit_confirm_open) --
   neither has anything to navigate, and moving selected_group out from
   under the system modal specifically would corrupt render.c's
   gamelist_system_entry_labels[] lookup. Otherwise always active, even if
   `lb` has zero groups -- the system rows still need to be navigable
   then. */
void gamelist_move(GameListState *state, const LaunchboxInfo *lb, int delta);

/* Jumps to the start of the next (`direction` > 0) or previous
   (`direction` < 0) run of groups sharing the same first letter
   (case-insensitive), wrapping around at the ends, and closes the
   version-picker modal if it was open. Only ever compares adjacent
   entries, so it works fine against the favorites-first order described
   on LaunchboxInfo (a letter can then appear as two separate runs -- once
   in the favorites block, once in the rest -- and this just visits both in
   turn). No-op if `lb` has zero groups, the current selection is a system
   row, or a modal (system or exit-confirm) is open -- letter-jumping is a
   real-games-only feature. */
void gamelist_jump_letter(GameListState *state, const LaunchboxInfo *lb, int direction);

/* Opens or closes the version-picker modal for the selected group (bound
   to Shift+Enter, and to Esc while it's open). Opening focuses version 0;
   closing refocuses the group's own row. No-op if the group has only one
   version -- nothing to pick -- if `lb` has zero groups, if the current
   selection is a system row (system entries have their own modal --
   system_modal_open -- driven separately by main.c), or if the exit
   confirmation is open. */
void gamelist_toggle_expand(GameListState *state, const LaunchboxInfo *lb);

/* Adjusts scroll_offset (a row index in the same unified space as
   selected_group) so the selected row stays within a `visible_rows`-row
   window. Call every frame after layout determines how many rows fit.
   Only cares about selected_group -- the version-picker/system modals,
   when open, are drawn as their own overlay and never take up rows in
   this scrolling list. */
void gamelist_scroll_into_view(GameListState *state, const LaunchboxInfo *lb, int visible_rows);

#endif /* CRT_GAMELIST_H */
