#include "gs232.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// Parses an unsigned decimal number at *p, advancing *p. Returns false when no digit.
static bool parse_uint(const char **p, int *out)
{
    const char *s = *p;
    if (!isdigit((unsigned char)*s)) {
        return false;
    }
    int v = 0;
    while (isdigit((unsigned char)*s)) {
        if (v > 10000) {
            return false;
        }
        v = v * 10 + (*s - '0');
        s++;
    }
    *p = s;
    *out = v;
    return true;
}

static bool at_end(const char *p)
{
    while (*p == ' ') {
        p++;
    }
    return *p == '\0';
}

// GS-232 allows 0-450 for overlap rotators; the units know 0-359 only.
static bool normalize_az(int az, int *out)
{
    if (az < 0 || az > 450) {
        return false;
    }
    *out = az % 360;
    return true;
}

static int clamp_reported(int deg)
{
    return deg < 0 ? 0 : deg;   // "+0aaa" / "EL=eee" cannot express negative angles
}

static int el_value(const gs232_position_t *pos)
{
    return pos->el_installed ? clamp_reported(pos->el) : 0;
}

static bool el_valid(const gs232_position_t *pos)
{
    return !pos->el_installed || pos->el_ok;
}

size_t gs232_handle(const char *line, gs232_variant_t variant, const gs232_position_t *pos,
                    gs232_action_t *action, char *reply, size_t reply_size)
{
    action->type = GS232_ACT_NONE;
    reply[0] = '\0';

    char cmd[32];
    size_t n = 0;
    const char *p = line;
    while (*p == ' ' || *p == '\n') {
        p++;
    }
    for (; *p != '\0' && *p != '\r' && *p != '\n' && n < sizeof(cmd) - 1; p++) {
        cmd[n++] = (char)toupper((unsigned char)*p);
    }
    cmd[n] = '\0';
    if (n == 0) {
        return 0;   // empty line (GS-232B clients send an extra CR)
    }

    int len = 0;
    const char *args = cmd + 1;

    if (strcmp(cmd, "C") == 0) {
        if (!pos->az_ok) {
            return 0;
        }
        len = variant == GS232_VARIANT_A
            ? snprintf(reply, reply_size, "+0%03d\r\n", clamp_reported(pos->az))
            : snprintf(reply, reply_size, "AZ=%03d\r\n", clamp_reported(pos->az));
    } else if (strcmp(cmd, "C2") == 0) {
        if (!pos->az_ok || !el_valid(pos)) {
            return 0;
        }
        len = variant == GS232_VARIANT_A
            ? snprintf(reply, reply_size, "+0%03d+0%03d\r\n", clamp_reported(pos->az), el_value(pos))
            : snprintf(reply, reply_size, "AZ=%03d EL=%03d\r\n", clamp_reported(pos->az), el_value(pos));
    } else if (strcmp(cmd, "B") == 0) {
        if (!el_valid(pos)) {
            return 0;
        }
        len = variant == GS232_VARIANT_A
            ? snprintf(reply, reply_size, "+0%03d\r\n", el_value(pos))
            : snprintf(reply, reply_size, "EL=%03d\r\n", el_value(pos));
    } else if (cmd[0] == 'M' && n > 1) {
        int az;
        const char *q = args;
        if (!parse_uint(&q, &az) || !at_end(q) || !normalize_az(az, &az)) {
            len = snprintf(reply, reply_size, "?>\r\n");
        } else {
            action->type = GS232_ACT_AZ_GOTO;
            action->az = az;
        }
    } else if (cmd[0] == 'W' && n > 1) {
        int az, el;
        const char *q = args;
        bool ok = parse_uint(&q, &az);
        while (ok && *q == ' ') {
            q++;
        }
        ok = ok && parse_uint(&q, &el) && at_end(q) && normalize_az(az, &az) && el <= 180;
        if (!ok) {
            len = snprintf(reply, reply_size, "?>\r\n");
        } else if (pos->el_installed) {
            action->type = GS232_ACT_AZEL_GOTO;
            action->az = az;
            action->el = el;
        } else {
            action->type = GS232_ACT_AZ_GOTO;   // azimuth-only station ignores the elevation part
            action->az = az;
        }
    } else if (strcmp(cmd, "S") == 0) {
        action->type = GS232_ACT_STOP_ALL;
    } else if (strcmp(cmd, "A") == 0) {
        action->type = GS232_ACT_STOP_AZ;
    } else if (strcmp(cmd, "E") == 0) {
        action->type = GS232_ACT_STOP_EL;
    } else if (strcmp(cmd, "R") == 0) {
        action->type = GS232_ACT_AZ_GOTO;
        action->az = GS232_AZ_RIGHT_LIMIT;
    } else if (strcmp(cmd, "L") == 0) {
        action->type = GS232_ACT_AZ_GOTO;
        action->az = GS232_AZ_LEFT_LIMIT;
    } else if (strcmp(cmd, "U") == 0) {
        action->type = GS232_ACT_EL_GOTO;
        action->el = GS232_EL_UP_LIMIT;
    } else if (strcmp(cmd, "D") == 0) {
        action->type = GS232_ACT_EL_GOTO;
        action->el = GS232_EL_DOWN_LIMIT;
    } else if (cmd[0] == 'X' && n == 2 && cmd[1] >= '1' && cmd[1] <= '4') {
        // rotation speed: the units have a fixed speed, accept silently
    } else {
        len = snprintf(reply, reply_size, "?>\r\n");
    }

    if (len < 0 || (size_t)len >= reply_size) {
        reply[0] = '\0';
        return 0;
    }
    return (size_t)len;
}
