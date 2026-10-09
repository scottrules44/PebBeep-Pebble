#pragma once
#include <pebble.h>

// Touch helpers shared by PebBeep's custom (non-MenuLayer) windows.
//
// PBL_TOUCH is only defined when building for a watch with a touchscreen
// (Pebble Time 2 / emery and Pebble Round 2 / gabbro). On every other
// platform these helpers compile down to "no touch, no footer".

// True when this watch has a touchscreen AND the user has touch input enabled
// in Settings. Cheap enough to call from every window_load.
bool touch_ui_available(void);

// Height reserved at the bottom of a window for a row of tappable buttons.
// 0 when touch is unavailable, so button-only layouts keep the full screen.
int16_t touch_ui_footer_height(void);

// Footer strip along the bottom of `bounds`. On round displays it is raised
// and narrowed to the visible chord so buttons never fall off the circle.
GRect touch_ui_footer_rect(GRect bounds);

// Rect for button `index` of `count` laid out left-to-right inside `footer`.
GRect touch_ui_footer_button_rect(GRect footer, int count, int index);

// Draw a rounded pill button with a centred label. `primary` picks the accent
// colour scheme (blue/white); the secondary scheme is light grey/black.
void touch_ui_draw_button(GContext *ctx, GRect rect, const char *label, bool primary);
