#include "gamelist.h"

#include <ctype.h>

const char *const gamelist_system_entry_labels[GAMELIST_SYSTEM_ENTRY_COUNT] = {
    "CALIBRATE CONTROLS",
};

static char first_letter(const char *title) {
    return (char)toupper((unsigned char)title[0]);
}

/* Total rows in the unified space: system entries plus real groups. Always
   >= GAMELIST_SYSTEM_ENTRY_COUNT, even with zero LaunchBox games -- the
   system rows must stay navigable regardless. */
static int gamelist_total_rows(const LaunchboxInfo *lb) {
    return GAMELIST_SYSTEM_ENTRY_COUNT + lb->group_count;
}

SDL_bool gamelist_selected_is_system(const GameListState *state) {
    return state->selected_group < GAMELIST_SYSTEM_ENTRY_COUNT ? SDL_TRUE : SDL_FALSE;
}

void gamelist_init(GameListState *state, const LaunchboxInfo *lb) {
    state->selected_group = (lb->group_count > 0) ? GAMELIST_SYSTEM_ENTRY_COUNT : 0;
    state->selected_version = -1;
    state->system_modal_open = SDL_FALSE;
    state->scroll_offset = state->selected_group;
}

void gamelist_move(GameListState *state, const LaunchboxInfo *lb, int delta) {
    if (delta == 0) {
        return;
    }

    if (state->selected_version >= 0) {
        /* Modal open -- stay scoped to its rows, wrapping at the ends.
           The underlying game list doesn't move while it's up. Only
           reachable for a real group (see gamelist_toggle_expand), so the
           offset below is always valid. */
        int group_index = state->selected_group - GAMELIST_SYSTEM_ENTRY_COUNT;
        int version_count = lb->groups[group_index].version_count;
        int next = (state->selected_version + delta) % version_count;
        if (next < 0) {
            next += version_count;
        }
        state->selected_version = next;
        return;
    }

    int total = gamelist_total_rows(lb);
    int next = (state->selected_group + delta) % total;
    if (next < 0) {
        next += total;
    }
    state->selected_group = next;
}

void gamelist_jump_letter(GameListState *state, const LaunchboxInfo *lb, int direction) {
    if (lb->group_count <= 0 || gamelist_selected_is_system(state)) {
        return;
    }

    int n = lb->group_count;
    int idx = state->selected_group - GAMELIST_SYSTEM_ENTRY_COUNT;
    char current = first_letter(lb->groups[idx].title);

    if (direction > 0) {
        int i = idx;
        for (int steps = 0; steps < n; steps++) {
            i = (i + 1) % n;
            if (first_letter(lb->groups[i].title) != current) {
                break;
            }
        }
        state->selected_group = i + GAMELIST_SYSTEM_ENTRY_COUNT;
    } else if (direction < 0) {
        /* Walk back to the start of the current letter's run first, so
           "previous letter" always means a different letter -- not just
           one row up within the same run. */
        int group_start = idx;
        while (group_start > 0 && first_letter(lb->groups[group_start - 1].title) == current) {
            group_start--;
        }

        int prev_last = (group_start == 0) ? (n - 1) : (group_start - 1);
        char prev_letter = first_letter(lb->groups[prev_last].title);

        int prev_start = prev_last;
        while (prev_start > 0 && first_letter(lb->groups[prev_start - 1].title) == prev_letter) {
            prev_start--;
        }

        state->selected_group = prev_start + GAMELIST_SYSTEM_ENTRY_COUNT;
    }

    state->selected_version = -1; /* letter-jumping always closes the modal */
}

void gamelist_toggle_expand(GameListState *state, const LaunchboxInfo *lb) {
    if (lb->group_count <= 0 || gamelist_selected_is_system(state)) {
        return;
    }

    int group_index = state->selected_group - GAMELIST_SYSTEM_ENTRY_COUNT;

    if (state->selected_version < 0) {
        if (lb->groups[group_index].version_count > 1) {
            state->selected_version = 0;
        }
    } else {
        state->selected_version = -1;
    }
}

void gamelist_scroll_into_view(GameListState *state, const LaunchboxInfo *lb, int visible_rows) {
    int total = gamelist_total_rows(lb);

    if (visible_rows <= 0) {
        state->scroll_offset = 0;
        return;
    }

    if (state->selected_group < state->scroll_offset) {
        state->scroll_offset = state->selected_group;
    } else if (state->selected_group >= state->scroll_offset + visible_rows) {
        state->scroll_offset = state->selected_group - visible_rows + 1;
    }

    int max_offset = total - visible_rows;
    if (max_offset < 0) {
        max_offset = 0;
    }
    if (state->scroll_offset > max_offset) {
        state->scroll_offset = max_offset;
    }
    if (state->scroll_offset < 0) {
        state->scroll_offset = 0;
    }
}
