#include "tests.h"

int tests_run = 0;
int tests_failed = 0;

int main(void) {
    static const struct {
        const char *name;
        void (*run)(void);
    } SUITES[] = {
        {"xml_util", test_xml_util},
        {"launcher", test_launcher},
        {"config", test_config},
        {"gamelist", test_gamelist},
        {"launchbox", test_launchbox},
    };

    for (size_t i = 0; i < sizeof(SUITES) / sizeof(SUITES[0]); i++) {
        printf("  %s\n", SUITES[i].name);
        SUITES[i].run();
    }

    printf("\n%d checks, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
