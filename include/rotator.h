// Rotator controller core: owns the RS485 bus, polls the AZ and EL units and keeps the
// shared state. Every user interface (web page, touch display) reads rotator_get_state()
// and drives the rotator only through the rotator_cmd_* functions.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <driver/gpio.h>
#include <driver/uart.h>

#define RS485_UART_PORT       UART_NUM_1
#define RS485_TX_PIN          GPIO_NUM_4
#define RS485_RX_PIN          GPIO_NUM_36
#define RS485_BAUD_RATE       38400   // must match the AZ/EL units

#define ROTATOR_POLL_PERIOD_MS   250   // one AZ + EL poll round
#define ROTATOR_OFFLINE_AFTER_MS 2000  // unit counts as not communicating after this silence

typedef enum {
    ROTATOR_AXIS_AZ = 0,
    ROTATOR_AXIS_EL = 1,
    ROTATOR_AXIS_COUNT
} rotator_axis_t;

typedef struct {
    bool present;      // unit has answered at least once since boot
    bool ok;           // valid answer within ROTATOR_OFFLINE_AFTER_MS
    int deg;           // last valid angle
    bool has_target;   // a goto command is in progress
    int target;        // requested angle when has_target
    uint32_t age_ms;   // time since the last valid answer (0 when never answered)
    char ver[24];      // unit firmware version from its status answer, "" when unknown
} rotator_axis_state_t;

typedef struct {
    rotator_axis_state_t axis[ROTATOR_AXIS_COUNT];
} rotator_state_t;

// Installs the UART driver and starts the bus task.
void rotator_start(void);

// Copies a consistent snapshot of the current state.
void rotator_get_state(rotator_state_t *out);

// Requests the azimuth to turn to deg (0-359). Returns false when the command queue is full
// or the value is out of range.
bool rotator_cmd_az_goto(int deg);

// Stops the azimuth by setting its target to the current position.
bool rotator_cmd_az_stop(void);
