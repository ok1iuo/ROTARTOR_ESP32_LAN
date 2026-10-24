#include "rotator_proto.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool rotator_parse_angle(const char *line, int min_deg, int max_deg, int *deg_out)
{
    if (line == NULL || deg_out == NULL) {
        return false;
    }
    const char *p = line;
    while (is_blank(*p)) {
        p++;
    }
    const char *start = p;
    if (*p == '+' || *p == '-') {
        p++;
    }
    if (!isdigit((unsigned char)*p)) {
        return false;
    }
    while (isdigit((unsigned char)*p)) {
        p++;
    }
    if (p - start > 6) {
        return false;
    }
    const char *end = p;
    while (is_blank(*p)) {
        p++;
    }
    if (*p != '\0') {
        return false;
    }
    char num[8];
    memcpy(num, start, (size_t)(end - start));
    num[end - start] = '\0';
    int value = atoi(num);
    if (value < min_deg || value > max_deg) {
        return false;
    }
    *deg_out = value;
    return true;
}

bool rotator_parse_version(const char *text, char *out, size_t out_size)
{
    if (text == NULL || out == NULL || out_size == 0) {
        return false;
    }
    const char *p = strstr(text, "ver:");
    if (p == NULL) {
        return false;
    }
    p += 4;
    const char *end = p;
    while (*end != '\0' && *end != '\r' && *end != '\n') {
        if (strncmp(end, " build:", 7) == 0) {
            break;
        }
        end++;
    }
    while (end > p && is_blank(end[-1])) {
        end--;
    }
    size_t len = (size_t)(end - p);
    if (len == 0) {
        return false;
    }
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

bool rotator_target_reached(int deg, int target)
{
    return abs(deg - target) <= ROTATOR_TARGET_TOLERANCE_DEG;
}

void rotator_line_reader_reset(rotator_line_reader_t *r)
{
    r->len = 0;
}

bool rotator_line_reader_feed(rotator_line_reader_t *r, char ch, char *line_buf, size_t line_size)
{
    if (ch == '\r' || ch == '\n') {
        if (r->len == 0) {
            return false;
        }
        size_t n = r->len < line_size - 1 ? r->len : line_size - 1;
        memcpy(line_buf, r->buf, n);
        line_buf[n] = '\0';
        r->len = 0;
        return true;
    }
    if (r->len < sizeof(r->buf) - 1) {
        r->buf[r->len++] = ch;
    }
    return false;
}
