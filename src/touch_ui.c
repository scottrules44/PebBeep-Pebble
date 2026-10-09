#include "touch_ui.h"

#define FOOTER_H        40
#define BUTTON_H        32
#define BUTTON_GAP      6
#define FOOTER_INSET_X  6
#define BUTTON_RADIUS   8
// Round displays: lift the footer so the chord it sits on is wide enough for
// two buttons side by side.
#define ROUND_FOOTER_BOTTOM_INSET 20

bool touch_ui_available(void) {
#if defined(PBL_TOUCH)
  return touch_service_is_enabled();
#else
  return false;
#endif
}

int16_t touch_ui_footer_height(void) {
  return touch_ui_available() ? FOOTER_H : 0;
}

#if defined(PBL_ROUND)
// Integer square root, good enough for chord maths on a 260px display.
static int32_t prv_isqrt(int32_t v) {
  if (v <= 0) return 0;
  int32_t r = 0;
  while ((r + 1) * (r + 1) <= v) r++;
  return r;
}
#endif

GRect touch_ui_footer_rect(GRect bounds) {
#if defined(PBL_ROUND)
  const int32_t radius = bounds.size.w / 2;
  const int16_t footer_y = bounds.size.h - FOOTER_H - ROUND_FOOTER_BOTTOM_INSET;
  // Bottom edge of the buttons is the narrowest point that has to stay visible.
  const int32_t button_bottom = footer_y + (FOOTER_H + BUTTON_H) / 2;
  const int32_t dy = button_bottom - radius;
  int32_t half = prv_isqrt(radius * radius - dy * dy) - 4;
  if (half < 40) half = 40;
  return GRect((int16_t)(radius - half), footer_y, (int16_t)(2 * half), FOOTER_H);
#else
  return GRect(bounds.origin.x, bounds.origin.y + bounds.size.h - FOOTER_H,
               bounds.size.w, FOOTER_H);
#endif
}

GRect touch_ui_footer_button_rect(GRect footer, int count, int index) {
  if (count < 1) count = 1;
  if (index < 0) index = 0;
  if (index >= count) index = count - 1;
  const int16_t usable = footer.size.w - 2 * FOOTER_INSET_X - (count - 1) * BUTTON_GAP;
  const int16_t w = usable / count;
  const int16_t x = footer.origin.x + FOOTER_INSET_X + index * (w + BUTTON_GAP);
  const int16_t y = footer.origin.y + (footer.size.h - BUTTON_H) / 2;
  return GRect(x, y, w, BUTTON_H);
}

void touch_ui_draw_button(GContext *ctx, GRect rect, const char *label, bool primary) {
  const GColor bg = primary ? PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack)
                            : PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite);
  const GColor fg = primary ? GColorWhite : GColorBlack;

  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, rect, BUTTON_RADIUS, GCornersAll);
#if defined(PBL_BW)
  // Keep the secondary button visible against a white background.
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_round_rect(ctx, rect, BUTTON_RADIUS);
#endif

  graphics_context_set_text_color(ctx, fg);
  // Gothic 18 carries a few pixels of top bearing; nudge the box up a touch so
  // the glyphs sit visually centred in the pill.
  const int16_t text_h = 24;
  GRect text_rect = GRect(rect.origin.x, rect.origin.y + (rect.size.h - text_h) / 2 - 2,
                          rect.size.w, text_h);
  graphics_draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), text_rect,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}
