#include "tests.h"

#include "launchbox.h"

/* CRT_TESTS_FIXTURES is an absolute path baked in by CMake, so the test
   runs the same from any working directory. */
#ifndef CRT_TESTS_FIXTURES
#define CRT_TESTS_FIXTURES "tests/fixtures"
#endif

static const LaunchboxGameGroup *find_group(const LaunchboxInfo *lb, const char *title) {
    for (int i = 0; i < lb->group_count; i++) {
        if (strcmp(lb->groups[i].title, title) == 0) {
            return &lb->groups[i];
        }
    }
    return NULL;
}

void test_launchbox(void) {
    LaunchboxInfo lb;
    launchbox_load(CRT_TESTS_FIXTURES, &lb);

    CHECK_INT(lb.status, LAUNCHBOX_STATUS_LOADED);
    if (lb.status != LAUNCHBOX_STATUS_LOADED) {
        printf("    (fixture not found at %s -- skipping the rest)\n", CRT_TESTS_FIXTURES);
        launchbox_free(&lb);
        return;
    }

    /* Platform name comes from the XML filename. */
    CHECK_INT(lb.platform_count, 1);
    if (lb.platform_count == 1) {
        CHECK_STR(lb.platform_names[0], "Arcade");
    }

    /* Three titles, not five entries: both clone mechanisms collapsed. */
    CHECK_INT(lb.group_count, 3);

    const LaunchboxGameGroup *galaga = find_group(&lb, "Galaga");
    const LaunchboxGameGroup *pacman = find_group(&lb, "Pac-Man");
    const LaunchboxGameGroup *digdug = find_group(&lb, "Dig Dug");
    CHECK(galaga != NULL);
    CHECK(pacman != NULL);
    CHECK(digdug != NULL);

    /* Grouped via <DatabaseID>. */
    if (galaga) {
        CHECK_INT(galaga->version_count, 2);
        CHECK(galaga->is_favorite);
    }
    /* Grouped via <AdditionalApplication>/<GameID>. */
    if (pacman) {
        CHECK_INT(pacman->version_count, 2);
        CHECK(!pacman->is_favorite);
    }
    /* No <DatabaseID>: keyed by <ID>, stands alone. */
    if (digdug) {
        CHECK_INT(digdug->version_count, 1);
    }

    /* The orphan <AdditionalApplication> was dropped: 2 + 2 + 1. */
    CHECK_INT(lb.version_count, 5);

    /* Favorites first, each block alphabetical. */
    CHECK_INT(lb.favorite_count, 1);
    if (lb.group_count == 3) {
        CHECK_STR(lb.groups[0].title, "Galaga");
        CHECK_STR(lb.groups[1].title, "Dig Dug");
        CHECK_STR(lb.groups[2].title, "Pac-Man");
    }

    /* version_start points at the game's own <Game> entry, not a clone --
       this is what SELECT launches on a collapsed row. */
    if (pacman) {
        const LaunchboxVersion *primary = &lb.versions[pacman->version_start];
        CHECK(strstr(primary->rom_path, "pacman.zip") != NULL);
        CHECK_STR(primary->platform, "Arcade");
        /* The additional app inherits the parent's emulator. */
        const LaunchboxVersion *clone = &lb.versions[pacman->version_start + 1];
        CHECK_STR(clone->emulator_id, "emu-mame");
        CHECK_STR(clone->label, "Japan");
    }

    launchbox_free(&lb);
}
