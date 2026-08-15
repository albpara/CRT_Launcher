#include "tests.h"

#include <stdlib.h>

#include "config.h"

#define TMP_INI "crt_test_config.ini"

static void write_file(const char *path, const char *contents) {
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(contents, 1, strlen(contents), f);
        fclose(f);
    }
}

/* Caller frees. Returns NULL if the file is missing. */
static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)n + 1);
    size_t got = buf ? fread(buf, 1, (size_t)n, f) : 0;
    fclose(f);
    if (buf) {
        buf[got] = '\0';
    }
    return buf;
}

static int count_occurrences(const char *haystack, const char *needle) {
    int n = 0;
    for (const char *p = strstr(haystack, needle); p; p = strstr(p + 1, needle)) {
        n++;
    }
    return n;
}

void test_config(void) {
    char *out;

    /* No file at all: the key still lands. */
    remove(TMP_INI);
    CHECK(config_save_selected_platforms(TMP_INI, "Arcade,SNES"));
    out = read_file(TMP_INI);
    CHECK(out != NULL);
    if (out) {
        CHECK(strstr(out, "selected_platforms=Arcade,SNES") != NULL);
        free(out);
    }

    /* Existing key is replaced in place, neighbours untouched. */
    write_file(TMP_INI, "[launchbox]\nlaunchbox_dir=E:\\LB\nselected_platforms=All\n");
    CHECK(config_save_selected_platforms(TMP_INI, "None"));
    out = read_file(TMP_INI);
    if (out) {
        CHECK_INT(count_occurrences(out, "selected_platforms="), 1);
        CHECK(strstr(out, "selected_platforms=None") != NULL);
        CHECK(strstr(out, "launchbox_dir=E:\\LB") != NULL);
        free(out);
    }

    /* Section present, key absent. */
    write_file(TMP_INI, "[launchbox]\nlaunchbox_dir=E:\\LB\n");
    CHECK(config_save_selected_platforms(TMP_INI, "Arcade"));
    out = read_file(TMP_INI);
    if (out) {
        CHECK_INT(count_occurrences(out, "selected_platforms="), 1);
        free(out);
    }

    /* Key is the last thing in the file, with no trailing newline. */
    write_file(TMP_INI, "[launchbox]\nselected_platforms=All");
    CHECK(config_save_selected_platforms(TMP_INI, "Arcade"));
    out = read_file(TMP_INI);
    if (out) {
        CHECK_INT(count_occurrences(out, "selected_platforms="), 1);
        CHECK(strstr(out, "selected_platforms=Arcade") != NULL);
        free(out);
    }

    /* CRLF must survive a read-modify-write without doubling -- the reason
       config.c does binary-mode I/O throughout. */
    write_file(TMP_INI, "[launchbox]\r\nselected_platforms=All\r\n");
    CHECK(config_save_selected_platforms(TMP_INI, "Arcade"));
    out = read_file(TMP_INI);
    if (out) {
        CHECK(strstr(out, "\r\r") == NULL);
        free(out);
    }

    /* A value right at the buffer limit. */
    {
        char big[CONFIG_SELECTED_PLATFORMS_MAX];
        memset(big, 'A', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        write_file(TMP_INI, "[launchbox]\nselected_platforms=All\n");
        CHECK(config_save_selected_platforms(TMP_INI, big));
        out = read_file(TMP_INI);
        if (out) {
            CHECK_INT(count_occurrences(out, "selected_platforms="), 1);
            free(out);
        }
    }

    /* Duplicate [bindings] sections collapse to one -- the dedup loop in
       config_save_bindings exists because real files have had two. */
    {
        InputBinding bindings[INPUT_ACTION_COUNT];
        memset(bindings, 0, sizeof(bindings));
        for (int i = 0; i < INPUT_ACTION_COUNT; i++) {
            bindings[i].type = INPUT_BINDING_KEYBOARD;
            bindings[i].key = SDLK_a;
        }

        write_file(TMP_INI,
                   "[display]\nwidth=320\n"
                   "[bindings]\nup=KEYBOARD Up\n"
                   "[bindings]\ndown=KEYBOARD Down\n");
        CHECK(config_save_bindings(TMP_INI, bindings));
        out = read_file(TMP_INI);
        if (out) {
            CHECK_INT(count_occurrences(out, "[bindings]"), 1);
            CHECK(strstr(out, "width=320") != NULL);
            CHECK_INT(count_occurrences(out, "up="), 1);
            free(out);
        }
    }

    /* Bindings into a file that has no [bindings] section yet. */
    write_file(TMP_INI, "[display]\nwidth=320\n");
    {
        InputBinding bindings[INPUT_ACTION_COUNT];
        memset(bindings, 0, sizeof(bindings));
        for (int i = 0; i < INPUT_ACTION_COUNT; i++) {
            bindings[i].type = INPUT_BINDING_JOY_BUTTON;
            bindings[i].joy_button = i;
        }
        CHECK(config_save_bindings(TMP_INI, bindings));
        out = read_file(TMP_INI);
        if (out) {
            CHECK_INT(count_occurrences(out, "[bindings]"), 1);
            CHECK(strstr(out, "JOYBUTTON") != NULL);
            free(out);
        }
    }

    remove(TMP_INI);
}
