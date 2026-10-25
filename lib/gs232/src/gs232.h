// Yaesu GS-232A/B command interpreter (pure C, unit-tested on the host in test/test_gs232).
// Formats follow Hamlib's gs232a/gs232b backends: commands end with CR, replies end with CR LF,
// GS-232A position "+0aaa+0eee", GS-232B position "AZ=aaa EL=eee", no reply to move/stop.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GS232_VARIANT_A = 0,
    GS232_VARIANT_B = 1,
} gs232_variant_t;

typedef struct {
    bool az_ok;          // azimuth data valid
    int az;
    bool el_installed;   // elevation unit installed (if not, elevation reads as 0)
    bool el_ok;          // elevation data valid
    int el;
} gs232_position_t;

typedef enum {
    GS232_ACT_NONE = 0,
    GS232_ACT_AZ_GOTO,     // az
    GS232_ACT_AZEL_GOTO,   // az, el
    GS232_ACT_EL_GOTO,     // el
    GS232_ACT_STOP_ALL,
    GS232_ACT_STOP_AZ,
    GS232_ACT_STOP_EL,
} gs232_action_type_t;

typedef struct {
    gs232_action_type_t type;
    int az;
    int el;
} gs232_action_t;

// Emulation of continuous rotation (R/L/U/D): the units only know "go to angle".
#define GS232_AZ_RIGHT_LIMIT 359
#define GS232_AZ_LEFT_LIMIT  0
#define GS232_EL_UP_LIMIT    90
#define GS232_EL_DOWN_LIMIT  0

// Interprets one command line (without the CR terminator). Fills action with what the
// rotator should do and reply with the text to send back (including CR LF).
// Returns the reply length; 0 means send nothing (also when position data is not valid,
// so the client reports a communication error instead of a stale position).
size_t gs232_handle(const char *line, gs232_variant_t variant, const gs232_position_t *pos,
                    gs232_action_t *action, char *reply, size_t reply_size);

#ifdef __cplusplus
}
#endif
