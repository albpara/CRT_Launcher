#include "tests.h"

/* Included, not linked: all three helpers are static. */
#include "../src/launcher.c"

void test_launcher(void) {
    char out[64];

    /* substitute_romlocation: first placeholder only, copied through when
       absent. */
    substitute_romlocation("-rompath X", "R", out, sizeof(out));
    CHECK_STR(out, "-rompath X");

    substitute_romlocation("%romlocation%", "R", out, sizeof(out));
    CHECK_STR(out, "R");

    substitute_romlocation("%romlocation% -tail", "R", out, sizeof(out));
    CHECK_STR(out, "R -tail");

    substitute_romlocation("head %romlocation%", "R", out, sizeof(out));
    CHECK_STR(out, "head R");

    substitute_romlocation("a %romlocation% b %romlocation%", "R", out, sizeof(out));
    CHECK_STR(out, "a R b %romlocation%");

    /* Template longer than out_cap: truncated, still terminated. */
    char small[8];
    substitute_romlocation("head %romlocation%", "REPLACEMENT", small, sizeof(small));
    CHECK_STR(small, "head RE");

    /* base_name_without_ext */
    base_name_without_ext("Roms\\0.262\\springer.zip", out, sizeof(out));
    CHECK_STR(out, "springer");

    base_name_without_ext("Roms/0.262/springer.zip", out, sizeof(out));
    CHECK_STR(out, "springer");

    base_name_without_ext("springer", out, sizeof(out));
    CHECK_STR(out, "springer");

    /* Last dot wins, so versioned names keep their dots. */
    base_name_without_ext("a.b.c.zip", out, sizeof(out));
    CHECK_STR(out, "a.b.c");

    base_name_without_ext("dir\\name", out, sizeof(out));
    CHECK_STR(out, "name");

    base_name_without_ext("dir\\", out, sizeof(out));
    CHECK_STR(out, "");

    char tiny[4];
    base_name_without_ext("springer.zip", tiny, sizeof(tiny));
    CHECK_STR(tiny, "spr");

    /* directory_part */
    directory_part("D:\\Games\\mame.exe", out, sizeof(out));
    CHECK_STR(out, "D:\\Games");

    directory_part("D:/Games/mame.exe", out, sizeof(out));
    CHECK_STR(out, "D:/Games");

    directory_part("mame.exe", out, sizeof(out));
    CHECK_STR(out, "");

    directory_part("D:\\Games\\", out, sizeof(out));
    CHECK_STR(out, "D:\\Games");

    char tiny2[4];
    directory_part("D:\\Games\\mame.exe", tiny2, sizeof(tiny2));
    CHECK_STR(tiny2, "D:\\");
}
