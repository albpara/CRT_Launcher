#ifndef CRT_XML_UTIL_H
#define CRT_XML_UTIL_H

#include <SDL.h>
#include <stddef.h>

/*
 * Tiny, dependency-free substring-based XML helpers shared by launchbox.c
 * and launcher.c. Not a real XML parser (see the "known placeholder"
 * comments in those files for what that means in practice) -- just enough
 * to pull specific fields out of LaunchBox's flat, predictable export
 * format.
 */

/* Reads the whole file at `path` into a heap buffer (caller frees),
   null-terminated. `*out_len` receives the byte count actually read
   (excluding the added null terminator). Returns NULL on any failure. */
char *xml_read_entire_file(const char *path, long *out_len);

/* Bounded substring search -- like strstr, but never reads past `end`.
   Needed when searching for fields within one already-delimited block:
   without a bound, a missing field could match a tag belonging to the
   next block instead of correctly reporting "not found". */
const char *xml_find_in_range(const char *start, const char *end, const char *needle);

/* Finds `open_tag ... close_tag` within [block_start, block_end) and
   decodes the text between them (via xml_unescape) into `out`. Leaves
   `out` as an empty string if the field isn't present in this block. */
void xml_extract_field(const char *block_start, const char *block_end,
                        const char *open_tag, const char *close_tag,
                        char *out, size_t out_cap);

/* Copies `in_len` bytes from `in` into `out`, decoding the handful of XML
   entities LaunchBox actually emits (&amp; &lt; &gt; &quot; &apos;).
   Truncates (rather than overflows) if `out` is smaller than the decoded
   text. */
void xml_unescape(const char *in, size_t in_len, char *out, size_t out_cap);

#endif /* CRT_XML_UTIL_H */
