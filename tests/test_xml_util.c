#include "tests.h"

/* Included, not linked: xml_unescape is static and worth covering directly. */
#include "../src/xml_util.c"

/* Two blocks, where the second carries a field the first lacks. */
static const char DOC[] =
    "<Game><Title>Pac-Man</Title></Game>"
    "<Game><Title>Galaga</Title><Notes>later</Notes></Game>";

void test_xml_util(void) {
    const char *first = DOC;
    const char *second = strstr(DOC + 1, "<Game>");
    const char *doc_end = DOC + sizeof(DOC) - 1;
    char out[64];

    xml_extract_field(first, second, "<Title>", "</Title>", out, sizeof(out));
    CHECK_STR(out, "Pac-Man");

    /* The bounded-search invariant: a field missing from this block must
       not be picked up from the next one, or data silently mis-associates. */
    xml_extract_field(first, second, "<Notes>", "</Notes>", out, sizeof(out));
    CHECK_STR(out, "");

    /* ...and it really is present later, so the check above means something. */
    xml_extract_field(second, doc_end, "<Notes>", "</Notes>", out, sizeof(out));
    CHECK_STR(out, "later");

    xml_extract_field(first, second, "<Nope>", "</Nope>", out, sizeof(out));
    CHECK_STR(out, "");

    static const char UNCLOSED[] = "<Game><Title>Dangling</Game>";
    xml_extract_field(UNCLOSED, UNCLOSED + sizeof(UNCLOSED) - 1,
                       "<Title>", "</Title>", out, sizeof(out));
    CHECK_STR(out, "");

    static const char EMPTY[] = "<Game><Title></Title></Game>";
    xml_extract_field(EMPTY, EMPTY + sizeof(EMPTY) - 1,
                       "<Title>", "</Title>", out, sizeof(out));
    CHECK_STR(out, "");

    /* Every entity LaunchBox emits. */
    static const char ENTS[] = "<T>&amp;&lt;&gt;&quot;&apos;</T>";
    xml_extract_field(ENTS, ENTS + sizeof(ENTS) - 1, "<T>", "</T>", out, sizeof(out));
    CHECK_STR(out, "&<>\"'");

    /* A bare '&' and an unknown entity pass through untouched. */
    static const char BARE[] = "<T>Tom &amp Jerry &x;</T>";
    xml_extract_field(BARE, BARE + sizeof(BARE) - 1, "<T>", "</T>", out, sizeof(out));
    CHECK_STR(out, "Tom &amp Jerry &x;");

    /* out_cap includes the terminator. */
    char small[5];
    xml_extract_field(first, second, "<Title>", "</Title>", small, sizeof(small));
    CHECK_STR(small, "Pac-");

    static const char RAW[] = "a&amp;b";
    xml_unescape(RAW, strlen(RAW), out, sizeof(out));
    CHECK_STR(out, "a&b");

    /* Truncation must not emit half an entity. */
    char tiny[3];
    xml_unescape(RAW, strlen(RAW), tiny, sizeof(tiny));
    CHECK_STR(tiny, "a&");

    char one[1];
    xml_unescape(RAW, strlen(RAW), one, sizeof(one));
    CHECK_STR(one, "");
}
