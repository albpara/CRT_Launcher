#include "tests.h"

/* Included, not linked: gamelist_parse_platform_selection is static. */
#include "../src/gamelist.c"

void test_gamelist(void) {
    static char names[3][LAUNCHBOX_PLATFORM_MAX] = {"Arcade", "SNES", "Mega Drive"};
    LaunchboxInfo lb;
    SDL_zero(lb);
    lb.platform_count = 3;
    lb.platform_names = names;

    SDL_bool selected[3];
    GameListState state;
    SDL_zero(state);
    state.platform_selected = selected;

    char out[256];

    gamelist_parse_platform_selection("All", &lb, selected);
    CHECK(selected[0] && selected[1] && selected[2]);
    gamelist_format_platform_selection(&state, &lb, out, sizeof(out));
    CHECK_STR(out, "All");

    gamelist_parse_platform_selection("None", &lb, selected);
    CHECK(!selected[0] && !selected[1] && !selected[2]);
    gamelist_format_platform_selection(&state, &lb, out, sizeof(out));
    CHECK_STR(out, "None");

    /* Missing/blank means everything, not nothing -- a fresh config.ini
       has no value and must still show games. */
    gamelist_parse_platform_selection("", &lb, selected);
    CHECK(selected[0] && selected[1] && selected[2]);
    gamelist_parse_platform_selection(NULL, &lb, selected);
    CHECK(selected[0] && selected[1] && selected[2]);

    /* Case-insensitive, and tolerant of spaces around tokens. */
    gamelist_parse_platform_selection("  arcade , MEGA DRIVE ", &lb, selected);
    CHECK(selected[0]);
    CHECK(!selected[1]);
    CHECK(selected[2]);

    /* Round trip: format then re-parse yields the same set. */
    gamelist_format_platform_selection(&state, &lb, out, sizeof(out));
    {
        SDL_bool again[3];
        gamelist_parse_platform_selection(out, &lb, again);
        CHECK(again[0] == selected[0]);
        CHECK(again[1] == selected[1]);
        CHECK(again[2] == selected[2]);
    }

    /* Names that match nothing are ignored rather than fatal. */
    gamelist_parse_platform_selection("Nonexistent", &lb, selected);
    CHECK(!selected[0] && !selected[1] && !selected[2]);

    /* A single platform formats as its bare name, not "All". */
    gamelist_parse_platform_selection("SNES", &lb, selected);
    gamelist_format_platform_selection(&state, &lb, out, sizeof(out));
    CHECK_STR(out, "SNES");
}
