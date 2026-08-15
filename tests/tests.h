#ifndef CRT_TESTS_H
#define CRT_TESTS_H

#include <stdio.h>
#include <string.h>

/* Plain assert-style runner, no framework -- nothing here is worth a
   dependency. Checks report and keep going so one bad case doesn't hide
   the rest of the suite. */

extern int tests_run;
extern int tests_failed;

#define CHECK(cond)                                                        \
    do {                                                                   \
        tests_run++;                                                       \
        if (!(cond)) {                                                     \
            tests_failed++;                                                \
            printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                  \
    } while (0)

#define CHECK_STR(actual, expected)                                        \
    do {                                                                   \
        const char *check_a = (actual);                                    \
        const char *check_e = (expected);                                  \
        tests_run++;                                                       \
        if (strcmp(check_a, check_e) != 0) {                               \
            tests_failed++;                                                \
            printf("    FAIL %s:%d  got \"%s\", want \"%s\"\n",            \
                   __FILE__, __LINE__, check_a, check_e);                  \
        }                                                                  \
    } while (0)

#define CHECK_INT(actual, expected)                                        \
    do {                                                                   \
        long check_a = (long)(actual);                                     \
        long check_e = (long)(expected);                                   \
        tests_run++;                                                       \
        if (check_a != check_e) {                                          \
            tests_failed++;                                                \
            printf("    FAIL %s:%d  got %ld, want %ld\n",                  \
                   __FILE__, __LINE__, check_a, check_e);                  \
        }                                                                  \
    } while (0)

void test_xml_util(void);
void test_launcher(void);
void test_config(void);
void test_gamelist(void);
void test_launchbox(void);

#endif /* CRT_TESTS_H */
