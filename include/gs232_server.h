// Yaesu GS-232A/B control of the rotator over Ethernet (TCP) and the USB serial port.
#pragma once

#include <stdbool.h>

#include "gs232.h"

#define GS232_TCP_PORT      10001
#define GS232_TCP_MAX_CLIENTS 3

// USB serial (UART0 through the board's USB bridge). While enabled, ESP-IDF logging on
// UART0 is switched off GS232_USB_TAKEOVER_DELAY_MS after start so it cannot corrupt replies.
// Set to 0 to keep the debug log on USB instead.
#define GS232_USB_ENABLED   1
#define GS232_USB_BAUD_RATE 9600   // Hamlib gs232a/gs232b serial_rate_max
#define GS232_USB_TAKEOVER_DELAY_MS 5000   // boot log stays readable on USB until then

// Loads the protocol variant from NVS and starts the TCP server and the USB serial task.
// nvs_flash_init() and esp_netif_init() must have been called before.
void gs232_server_start(void);

gs232_variant_t gs232_server_get_variant(void);

// Persists the protocol variant in NVS.
bool gs232_server_set_variant(gs232_variant_t variant);
