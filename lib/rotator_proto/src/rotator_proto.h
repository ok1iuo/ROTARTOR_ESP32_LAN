// Pure protocol helpers for the AZ/EL rotator units on RS485 (no ESP-IDF dependencies,
// unit-tested on the host in test/test_rotator_proto).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Tolerance used by the units' motor control is 0.9 deg and they report whole degrees.
#define ROTATOR_TARGET_TOLERANCE_DEG 1

// Parses one angle answer line, e.g. "+0123" or "-0005" (surrounding CR/LF/spaces allowed).
// Rejects anything else (echoed commands, "new azimut =...", garbage) and values outside
// <min_deg, max_deg>. Returns true and stores the angle on success.
bool rotator_parse_angle(const char *line, int min_deg, int max_deg, int *deg_out);

// Extracts the firmware version from a status (ST/ST2) answer containing
// "ver:<version> build:<date>" or, for older units, "ver:<date>".
// Returns true when "ver:" was found and a non-empty version copied to out.
bool rotator_parse_version(const char *text, char *out, size_t out_size);

// True when the reported angle is within the units' stop tolerance of the target.
bool rotator_target_reached(int deg, int target);

// Splits buffered serial data into lines terminated by CR or LF.
// Feeds chars one by one; returns true when a non-empty line is complete in line_buf
// (NUL-terminated). Overlong lines are truncated.
typedef struct {
    char buf[96];
    size_t len;
} rotator_line_reader_t;

void rotator_line_reader_reset(rotator_line_reader_t *r);
bool rotator_line_reader_feed(rotator_line_reader_t *r, char ch, char *line_buf, size_t line_size);

#ifdef __cplusplus
}
#endif
