#include "xml_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *xml_read_entire_file(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[read] = '\0';
    *out_len = (long)read;
    return buf;
}

const char *xml_find_in_range(const char *start, const char *end, const char *needle) {
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || end <= start) {
        return NULL;
    }
    size_t range_len = (size_t)(end - start);
    if (needle_len > range_len) {
        return NULL;
    }
    for (const char *p = start; p <= end - needle_len; p++) {
        if (memcmp(p, needle, needle_len) == 0) {
            return p;
        }
    }
    return NULL;
}

void xml_unescape(const char *in, size_t in_len, char *out, size_t out_cap) {
    size_t oi = 0;
    size_t i = 0;

    while (i < in_len && oi + 1 < out_cap) {
        if (in[i] == '&') {
            if (i + 5 <= in_len && strncmp(in + i, "&amp;", 5) == 0) { out[oi++] = '&'; i += 5; continue; }
            if (i + 4 <= in_len && strncmp(in + i, "&lt;", 4) == 0) { out[oi++] = '<'; i += 4; continue; }
            if (i + 4 <= in_len && strncmp(in + i, "&gt;", 4) == 0) { out[oi++] = '>'; i += 4; continue; }
            if (i + 6 <= in_len && strncmp(in + i, "&quot;", 6) == 0) { out[oi++] = '"'; i += 6; continue; }
            if (i + 6 <= in_len && strncmp(in + i, "&apos;", 6) == 0) { out[oi++] = '\''; i += 6; continue; }
        }
        out[oi++] = in[i++];
    }
    out[oi] = '\0';
}

void xml_extract_field(const char *block_start, const char *block_end,
                        const char *open_tag, const char *close_tag,
                        char *out, size_t out_cap) {
    out[0] = '\0';

    const char *tag_pos = xml_find_in_range(block_start, block_end, open_tag);
    if (!tag_pos) {
        return;
    }
    const char *val_start = tag_pos + strlen(open_tag);
    const char *val_end = xml_find_in_range(val_start, block_end, close_tag);
    if (!val_end || val_end < val_start) {
        return;
    }

    xml_unescape(val_start, (size_t)(val_end - val_start), out, out_cap);
}
