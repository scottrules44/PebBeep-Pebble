#include "welcome.h"
#include "chat_list.h"
#include "touch_ui.h"

// Forward declarations from main.c
extern void send_command(const char *command);

static Window *s_window;
static TextLayer *s_title_layer;
static TextLayer *s_subtitle_layer;
static TextLayer *s_instructions_layer;
static TextLayer *s_hint_layer;   // "MID: Connect  DOWN: Demo" on button-only watches
static Layer *s_footer_layer;     // tappable [Connect] [Demo] on touch watches
#if defined(PBL_TOUCH)
static Recognizer *s_tap_recognizer;
#endif

static void start_demo_mode(void) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Starting demo mode");

  chat_list_init();
  chat_list_add_item(0, 5, "demo-1", "Alice", "Are you free tonight?");
  chat_list_add_item(1, 5, "demo-2", "Work Group", "Meeting at 3pm");
  chat_list_add_item(2, 5, "demo-3", "Mom", "Call me when you can");
  chat_list_add_item(3, 5, "demo-4", "Bob", "Check this out!");
  chat_list_add_item(4, 5, "demo-5", "Signal Group", "Who's coming Saturday?");

  chat_list_show();
}

static void start_live_mode(void) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Starting live mode");

  chat_list_init();
  chat_list_show();
  send_command("fetch_chats");
}

// --- Buttons ---

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  start_live_mode();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  start_demo_mode();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

// --- Touch (Pebble Time 2 / Pebble Round 2) ---

#if defined(PBL_TOUCH)
#define BTN_CONNECT 0
#define BTN_DEMO    1

static void footer_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  touch_ui_draw_button(ctx, touch_ui_footer_button_rect(b, 2, BTN_CONNECT), "Connect", true);
  touch_ui_draw_button(ctx, touch_ui_footer_button_rect(b, 2, BTN_DEMO), "Demo", false);
}

// Which footer button (if any) contains a screen-space point
static int prv_button_at(GPoint screen_point) {
  GRect frame = layer_get_frame(s_footer_layer);
  GPoint p = GPoint(screen_point.x - frame.origin.x, screen_point.y - frame.origin.y);
  GRect local = GRect(0, 0, frame.size.w, frame.size.h);
  for (int i = 0; i < 2; i++) {
    GRect r = touch_ui_footer_button_rect(local, 2, i);
    if (grect_contains_point(&r, &p)) return i;
  }
  return -1;
}

static void tap_handler(const Recognizer *recognizer, RecognizerEvent event) {
  if (event != RecognizerEvent_Completed || !s_footer_layer) return;
  switch (prv_button_at(tap_recognizer_get_tap_point(recognizer))) {
    case BTN_CONNECT: start_live_mode(); break;
    case BTN_DEMO:    start_demo_mode(); break;
    default: break;
  }
}
#endif

// --- Window ---

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  const bool touch = touch_ui_available();

#ifdef PBL_ROUND
  // The chalk insets were tuned for 180px; scale the top inset for the
  // 260px Pebble Round 2 so the title stays clear of the bezel.
  const int16_t top_inset = bounds.size.h * 26 / 180;
  GRect content = grect_inset(bounds, GEdgeInsets(top_inset, 18, 20, 18));
#else
  GRect content = grect_inset(bounds, GEdgeInsets(6, 8, 6, 8));
#endif

  int y = content.origin.y;

  // Title — "PebBeep"
  s_title_layer = text_layer_create(GRect(content.origin.x, y, content.size.w, 32));
  text_layer_set_text(s_title_layer, "PebBeep");
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_title_layer, GColorClear);
#ifdef PBL_COLOR
  text_layer_set_text_color(s_title_layer, GColorWhite);
#endif
  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));
  y += 30;

  // Subtitle
  s_subtitle_layer = text_layer_create(GRect(content.origin.x, y, content.size.w, 18));
  text_layer_set_text(s_subtitle_layer, "Beeper on your wrist");
  text_layer_set_font(s_subtitle_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_subtitle_layer, GTextAlignmentCenter);
  text_layer_set_background_color(s_subtitle_layer, GColorClear);
#ifdef PBL_COLOR
  text_layer_set_text_color(s_subtitle_layer, GColorLightGray);
#endif
  layer_add_child(window_layer, text_layer_get_layer(s_subtitle_layer));
  y += 22;

  // Instructions
  s_instructions_layer = text_layer_create(GRect(content.origin.x, y, content.size.w, 64));
#ifdef PBL_ROUND
  // Shorter text for round display
  text_layer_set_text(s_instructions_layer,
    "Pair via phone app\n"
    "settings to connect"
  );
#else
  text_layer_set_text(s_instructions_layer,
    "To connect:\n"
    "1. Install PebBeep desktop\n"
    "2. Open app settings\n"
    "3. Scan QR to pair"
  );
#endif
  text_layer_set_font(s_instructions_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_instructions_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_instructions_layer, GTextOverflowModeWordWrap);
  text_layer_set_background_color(s_instructions_layer, GColorClear);
#ifdef PBL_COLOR
  text_layer_set_text_color(s_instructions_layer, GColorLightGray);
#endif
  layer_add_child(window_layer, text_layer_get_layer(s_instructions_layer));

  if (touch) {
#if defined(PBL_TOUCH)
    // Tappable buttons along the bottom edge
    s_footer_layer = layer_create(touch_ui_footer_rect(bounds));
    layer_set_update_proc(s_footer_layer, footer_update_proc);
    layer_add_child(window_layer, s_footer_layer);

    // This window draws its own touch controls, so keep the system
    // button-emulation bridge out of the way. The window is pushed once and
    // lives until the app exits, so a single attach is all it needs.
    window_set_touch_bridge_disabled(window, true);
    if (!s_tap_recognizer) {
      s_tap_recognizer = tap_recognizer_create(tap_handler, NULL);
      window_attach_recognizer(window, s_tap_recognizer);
    }
#endif
  } else {
    // Button hint at bottom
#ifdef PBL_ROUND
    int label_y = bounds.size.h - 52;
#else
    int label_y = bounds.size.h - 38;
#endif
    s_hint_layer = text_layer_create(GRect(content.origin.x, label_y, content.size.w, 34));
    text_layer_set_text(s_hint_layer, "MID: Connect  DOWN: Demo");
    text_layer_set_font(s_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
    text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
    text_layer_set_background_color(s_hint_layer, GColorClear);
#ifdef PBL_COLOR
    text_layer_set_text_color(s_hint_layer, GColorVividCerulean);
#endif
    layer_add_child(window_layer, text_layer_get_layer(s_hint_layer));
  }
}

static void window_unload(Window *window) {
  text_layer_destroy(s_title_layer);
  text_layer_destroy(s_subtitle_layer);
  text_layer_destroy(s_instructions_layer);
  if (s_hint_layer)   { text_layer_destroy(s_hint_layer); s_hint_layer = NULL; }
  if (s_footer_layer) { layer_destroy(s_footer_layer);    s_footer_layer = NULL; }
}

void welcome_show(void) {
  s_window = window_create();
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });

#ifdef PBL_COLOR
  window_set_background_color(s_window, GColorDarkGray);
#endif

  window_stack_push(s_window, true);
  APP_LOG(APP_LOG_LEVEL_INFO, "Welcome screen shown");
}

void welcome_deinit(void) {
  if (s_window) {
#if defined(PBL_TOUCH)
    // The firmware never frees recognizers attached to a window's root layer,
    // so release ours before the window goes away.
    if (s_tap_recognizer) {
      window_detach_recognizer(s_window, s_tap_recognizer);
      recognizer_destroy(s_tap_recognizer);
      s_tap_recognizer = NULL;
    }
#endif
    window_destroy(s_window);
    s_window = NULL;
  }
}
