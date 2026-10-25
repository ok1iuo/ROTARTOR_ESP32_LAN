// Screen layout of the 480x320 display, drawn with LVGL. Hardware independent so the same
// code can be rendered on a PC; display.c owns the panel and calls these under the LVGL lock.
#pragma once

#include <lvgl.h>

#include "rotator.h"

#define DISPLAY_UI_WIDTH  480
#define DISPLAY_UI_HEIGHT 320

// Builds all widgets on the given screen.
void display_ui_create(lv_obj_t *screen);

// Shows the rotator state; ip is the board address ("" while the link is down).
void display_ui_update(const rotator_state_t *st, const char *ip, const char *fw_version);
