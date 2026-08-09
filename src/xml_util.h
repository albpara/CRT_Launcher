#ifndef CRT_XML_UTIL_H
#define CRT_XML_UTIL_H

#include <stddef.h>

/* Substring-based XML field extraction shared by launchbox.c and
   launcher.c. Not a real XML parser -- just enough for LaunchBox's flat
   export format. */

/* Reads the whole file into a heap buffer (caller frees), null-terminated.
   Returns NULL on failure. */
char *xml_read_entire_file(const char *path, long *out_len);

/* Finds `open_tag ... close_tag` within [block_start, block_end) and
   decodes the text between them into `out` (empty string if absent). */
void xml_extract_field(const char *block_start, const char *block_end,
                        const char *open_tag, const char *close_tag,
                        char *out, size_t out_cap);

#endif /* CRT_XML_UTIL_H */
