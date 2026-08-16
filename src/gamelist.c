#include "gamelist.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const gamelist_system_entry_labels[GAMELIST_SYSTEM_ENTRY_COUNT] = {
    "CALIBRATE CONTROLS",
    "ADD TO STARTUP", /* fallback only -- render.c overrides with live state */
};

static char first_letter(const char *title) {
    return (char)toupper((unsigned char)title[0]);
}

static int platform_rows_start(void) {
    return GAMELIST_SYSTEM_ENTRY_COUNT;
}

static int game_rows_start(const LaunchboxInfo *lb) {
    return GAMELIST_SYSTEM_ENTRY_COUNT + lb->platform_count;
}

static int gamelist_total_rows(const LaunchboxInfo *lb, const GameListState *state) {
    return game_rows_start(lb) + state->visible_group_count;
}

SDL_bool gamelist_selected_is_system(const GameListState *state) {
    return state->selected_group < GAMELIST_SYSTEM_ENTRY_COUNT ? SDL_TRUE : SDL_FALSE;
}

SDL_bool gamelist_selected_is_platform(const GameListState *state, const LaunchboxInfo *lb) {
    return (state->selected_group >= platform_rows_start() &&
            state->selected_group < game_rows_start(lb)) ? SDL_TRUE : SDL_FALSE;
}

int gamelist_selected_platform_index(const GameListState *state) {
    return state->selected_group - platform_rows_start();
}

const LaunchboxGameGroup *gamelist_selected_group(const GameListState *state, const LaunchboxInfo *lb) {
    int idx = state->selected_group - game_rows_start(lb);
    if (idx < 0 || idx >= state->visible_group_count) {
        return NULL;
    }
    return &lb->groups[state->visible_group_indices[idx]];
}

/* Case-insensitive comma-separated token match, spaces trimmed. */
static SDL_bool platform_name_in_csv(const char *csv, const char *name) {
    size_t name_len = strlen(name);
    const char *p = csv;

    while (*p) {
        while (*p == ' ' || *p == ',') {
            p++;
        }
        const char *start = p;
        while (*p && *p != ',') {
            p++;
        }
        const char *end = p;
        while (end > start && end[-1] == ' ') {
            end--;
        }

        size_t tok_len = (size_t)(end - start);
        if (tok_len == name_len && SDL_strncasecmp(start, name, tok_len) == 0) {
            return SDL_TRUE;
        }
    }

    return SDL_FALSE;
}

/* "All", "None", or a name list -> per-platform bools. Unknown names in
   the CSV are ignored. */
static void gamelist_parse_platform_selection(const char *csv, const LaunchboxInfo *lb, SDL_bool *out) {
    SDL_bool all = (!csv || !csv[0] || SDL_strcasecmp(csv, "All") == 0) ? SDL_TRUE : SDL_FALSE;
    SDL_bool none = (!all && SDL_strcasecmp(csv, "None") == 0) ? SDL_TRUE : SDL_FALSE;

    for (int i = 0; i < lb->platform_count; i++) {
        if (all) {
            out[i] = SDL_TRUE;
        } else if (none) {
            out[i] = SDL_FALSE;
        } else {
            out[i] = platform_name_in_csv(csv, lb->platform_names[i]);
        }
    }
}

/* Rebuilds the visible set from platform_selected. A group's platform is
   its primary version's (the entry guaranteed to carry real data). Runs
   only on init and toggles, never per-frame. */
static void gamelist_recompute_visible_groups(GameListState *state, const LaunchboxInfo *lb) {
    state->visible_group_count = 0;
    if (!state->visible_group_indices) {
        return;
    }

    for (int i = 0; i < lb->group_count; i++) {
        const LaunchboxGameGroup *grp = &lb->groups[i];
        SDL_bool visible = SDL_TRUE;

        if (state->platform_selected) {
            const char *platform = lb->versions[grp->version_start].platform;
            visible = SDL_FALSE;
            for (int p = 0; p < lb->platform_count; p++) {
                if (SDL_strcasecmp(lb->platform_names[p], platform) == 0) {
                    visible = state->platform_selected[p];
                    break;
                }
            }
        }

        if (visible) {
            state->visible_group_indices[state->visible_group_count++] = i;
        }
    }
}

void gamelist_init(GameListState *state, const LaunchboxInfo *lb, const char *selected_platforms_csv) {
    state->selected_version = -1;
    state->system_modal_open = SDL_FALSE;
    state->system_modal_status[0] = '\0';
    state->system_modal_hint[0] = '\0';
    state->exit_confirm_open = SDL_FALSE;

    state->platform_selected = NULL;
    if (lb->platform_count > 0) {
        state->platform_selected = (SDL_bool *)malloc((size_t)lb->platform_count * sizeof(SDL_bool));
        if (state->platform_selected) {
            gamelist_parse_platform_selection(selected_platforms_csv, lb, state->platform_selected);
        }
    }

    state->visible_group_indices = NULL;
    if (lb->group_count > 0) {
        state->visible_group_indices = (int *)malloc((size_t)lb->group_count * sizeof(int));
    }
    gamelist_recompute_visible_groups(state, lb);

    state->selected_group = (state->visible_group_count > 0) ? game_rows_start(lb) : 0;
    state->scroll_offset = state->selected_group;
}

void gamelist_free(GameListState *state) {
    free(state->platform_selected);
    free(state->visible_group_indices);
    state->platform_selected = NULL;
    state->visible_group_indices = NULL;
    state->visible_group_count = 0;
}

void gamelist_toggle_platform(GameListState *state, const LaunchboxInfo *lb, int platform_index) {
    if (!state->platform_selected || platform_index < 0 || platform_index >= lb->platform_count) {
        return;
    }
    state->platform_selected[platform_index] = state->platform_selected[platform_index] ? SDL_FALSE : SDL_TRUE;
    gamelist_recompute_visible_groups(state, lb);
}

void gamelist_format_platform_selection(const GameListState *state, const LaunchboxInfo *lb,
                                         char *out, size_t out_cap) {
    if (!state->platform_selected || lb->platform_count == 0) {
        snprintf(out, out_cap, "All");
        return;
    }

    int selected_count = 0;
    for (int i = 0; i < lb->platform_count; i++) {
        if (state->platform_selected[i]) {
            selected_count++;
        }
    }

    if (selected_count == lb->platform_count) {
        snprintf(out, out_cap, "All");
        return;
    }
    if (selected_count == 0) {
        snprintf(out, out_cap, "None");
        return;
    }

    size_t off = 0;
    out[0] = '\0';
    SDL_bool first = SDL_TRUE;
    for (int i = 0; i < lb->platform_count && off < out_cap; i++) {
        if (!state->platform_selected[i]) {
            continue;
        }
        int written = snprintf(out + off, out_cap - off, "%s%s", first ? "" : ",", lb->platform_names[i]);
        if (written < 0) {
            break;
        }
        off += (size_t)written;
        first = SDL_FALSE;
    }
}

void gamelist_move(GameListState *state, const LaunchboxInfo *lb, int delta) {
    if (delta == 0 || state->system_modal_open || state->exit_confirm_open || state->notice_open) {
        /* Nothing underneath an open modal may move -- moving
           selected_group off a system row corrupts render.c's
           gamelist_system_entry_labels[] lookup (crashed once). */
        return;
    }

    if (state->selected_version >= 0) {
        /* Version picker open -- wrap within its rows only. */
        const LaunchboxGameGroup *grp = gamelist_selected_group(state, lb);
        if (!grp) {
            state->selected_version = -1;
            return;
        }
        int version_count = grp->version_count;
        int next = (state->selected_version + delta) % version_count;
        if (next < 0) {
            next += version_count;
        }
        state->selected_version = next;
        return;
    }

    int total = gamelist_total_rows(lb, state);
    int games_start = game_rows_start(lb);
    int next = state->selected_group + delta;

    if (state->selected_group >= games_start) {
        /* The games loop on themselves: running off the end returns to
           the first game instead of dropping into the settings rows,
           which are reached by going up from the first game instead. */
        if (next >= total) {
            next = games_start;
        } else if (next < games_start) {
            next = games_start - 1;
        }
    } else {
        /* Settings and platform rows sit above the list: walk down into
           the games, stop at the top. */
        if (next >= total) {
            next = total - 1;
        } else if (next < 0) {
            next = 0;
        }
    }

    state->selected_group = next;
}

void gamelist_jump_letter(GameListState *state, const LaunchboxInfo *lb, int direction) {
    if (state->visible_group_count <= 0 || gamelist_selected_is_system(state) ||
        gamelist_selected_is_platform(state, lb) || state->system_modal_open || state->exit_confirm_open ||
        state->notice_open) {
        return;
    }

    int rows_start = game_rows_start(lb);
    int n = state->visible_group_count;
    int idx = state->selected_group - rows_start;
    char current = first_letter(lb->groups[state->visible_group_indices[idx]].title);

    if (direction > 0) {
        int i = idx;
        for (int steps = 0; steps < n; steps++) {
            i = (i + 1) % n;
            if (first_letter(lb->groups[state->visible_group_indices[i]].title) != current) {
                break;
            }
        }
        state->selected_group = i + rows_start;
    } else if (direction < 0) {
        /* Walk to the start of the current run first, so "previous" always
           means a different letter. */
        int group_start = idx;
        while (group_start > 0 &&
               first_letter(lb->groups[state->visible_group_indices[group_start - 1]].title) == current) {
            group_start--;
        }

        int prev_last = (group_start == 0) ? (n - 1) : (group_start - 1);
        char prev_letter = first_letter(lb->groups[state->visible_group_indices[prev_last]].title);

        int prev_start = prev_last;
        while (prev_start > 0 &&
               first_letter(lb->groups[state->visible_group_indices[prev_start - 1]].title) == prev_letter) {
            prev_start--;
        }

        state->selected_group = prev_start + rows_start;
    }

    state->selected_version = -1;
}

void gamelist_toggle_expand(GameListState *state, const LaunchboxInfo *lb) {
    if (gamelist_selected_is_system(state) || gamelist_selected_is_platform(state, lb) ||
        state->system_modal_open || state->exit_confirm_open || state->notice_open) {
        return;
    }

    const LaunchboxGameGroup *grp = gamelist_selected_group(state, lb);
    if (!grp) {
        return;
    }

    if (state->selected_version < 0) {
        if (grp->version_count > 1) {
            state->selected_version = 0;
        }
    } else {
        state->selected_version = -1;
    }
}

void gamelist_scroll_into_view(GameListState *state, const LaunchboxInfo *lb, int visible_rows) {
    int total = gamelist_total_rows(lb, state);

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
