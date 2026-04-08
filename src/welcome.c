#include "welcome.h"
#include "chat_list.h"

// Forward declarations from main.c
extern void send_command(const char *command);

static Window *s_window;
static TextLayer *s_title_layer;
static TextLayer *s_subtitle_layer;
static TextLayer *s_instructions_layer;
static TextLayer *s_demo_label;

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

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Inset for round displays
#ifdef PBL_ROUND
  GRect content = grect_inset(bounds, GEdgeInsets(26, 18, 20, 18));
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
#ifdef PBL_ROUND
  // Shorter text for round display
  s_instructions_layer = text_layer_create(GRect(content.origin.x, y, content.size.w, 60));
  text_layer_set_text(s_instructions_layer,
    "Pair via phone app\n"
    "settings to connect"
  );
#else
  s_instructions_layer = text_layer_create(GRect(content.origin.x, y, content.size.w, 60));
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

  // Button labels at bottom
#ifdef PBL_ROUND
  int label_y = bounds.size.h - 52;
#else
  int label_y = bounds.size.h - 38;
#endif

  s_demo_label = text_layer_create(GRect(content.origin.x, label_y, content.size.w, 34));
  text_layer_set_text(s_demo_label,
    "MID: Connect  DOWN: Demo"
  );
  text_layer_set_font(s_demo_label, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_demo_label, GTextAlignmentCenter);
  text_layer_set_background_color(s_demo_label, GColorClear);
#ifdef PBL_COLOR
  text_layer_set_text_color(s_demo_label, GColorVividCerulean);
#endif
  layer_add_child(window_layer, text_layer_get_layer(s_demo_label));
}

static void window_unload(Window *window) {
  text_layer_destroy(s_title_layer);
  text_layer_destroy(s_subtitle_layer);
  text_layer_destroy(s_instructions_layer);
  text_layer_destroy(s_demo_label);
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
    window_destroy(s_window);
    s_window = NULL;
  }
}
