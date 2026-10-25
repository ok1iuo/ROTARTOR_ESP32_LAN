// 4.0" 480x320 SPI display (ST7796S) on the LaskaKit ESPlan v1.1 header JP1. Shows the
// rotator state through display_ui.c; touch (FT6336U on I2C) is not wired yet. Wiring: doc/zapojeni-displeje.md
#pragma once

#include <driver/gpio.h>
#include <driver/spi_master.h>

#define DISPLAY_ENABLED     1

#define DISPLAY_SPI_HOST    SPI2_HOST       // IO13/IO14 are its native (IOMUX) pins
#define DISPLAY_PIN_MOSI    GPIO_NUM_13     // JP1 pin 11
#define DISPLAY_PIN_SCLK    GPIO_NUM_14     // JP1 pin 13
#define DISPLAY_PIN_CS      GPIO_NUM_0      // JP1 pin 8, strapping pin: high at boot = CS idle
#define DISPLAY_PIN_DC      GPIO_NUM_16     // JP1 pin 10
#define DISPLAY_PIN_RST     GPIO_NUM_NC     // display RST tied to EN, reset together with the ESP32
#define DISPLAY_SPI_CLOCK_HZ (26 * 1000 * 1000)   // lower if long wires give garbled pixels

// Landscape orientation; flip mirror_x/mirror_y together to turn the picture by 180 deg.
#define DISPLAY_SWAP_XY      true
#define DISPLAY_MIRROR_X     true
#define DISPLAY_MIRROR_Y     true
#define DISPLAY_INVERT_COLORS false   // set true if black shows as white
#define DISPLAY_BGR          true     // set false if red and blue are swapped

#define DISPLAY_DRAW_LINES   20       // LVGL draws in two DMA buffers of this many lines
#define DISPLAY_UPDATE_PERIOD_MS 250

// Initialises the panel and LVGL and starts the task that redraws the rotator state.
// rotator_start() and esp_netif_init() must have been called before.
void display_start(void);
