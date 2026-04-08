#include "notification.h"
#include "chat_list.h"
#include "reply_menu.h"

// Forward declarations
extern void request_messages(const char *chat_id);

static Window *s_window;
static TextLayer *s_sender_layer;
static TextLayer *s_text_layer;
static TextLayer *s_hint_layer;
static TextLayer *s_service_layer;

static char s_sender_buf[32];
static char s_text_buf[128];
static char s_chat_id_buf[64];
static char s_chat_name_buf[24];
static char s_service_buf[16];

// Notification queue for back-to-back messages
#define MAX_NOTIF_QUEUE 5
typedef struct {
  char sender[32];
  char text[128];
  char chat_id[64];
  char chat_name[24];
  char service[16];
} NotifItem;

static NotifItem s_notif_queue[MAX_NOTIF_QUEUE];
static int s_notif_count = 0;
static int s_notif_current = 0;

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

#ifdef PBL_COLOR
  window_set_background_color(window, GColorDarkGray);
#endif

#ifdef PBL_ROUND
  int inset_x = 30;
  int inset_y = 30;
#else
  int inset_x = 8;
  int inset_y = 8;
#endif

  int text_w = bounds.size.w - (inset_x * 2);

  // Sender name
  s_sender_layer = text_layer_create(GRect(inset_x, inset_y, text_w, 30));
  text_layer_set_font(s_sender_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text(s_sender_layer, s_sender_buf);
  text_layer_set_overflow_mode(s_sender_layer, GTextOverflowModeTrailingEllipsis);
  text_layer_set_background_color(s_sender_layer, GColorClear);
#ifdef PBL_COLOR
  text_layer_set_text_color(s_sender_layer, GColorChromeYellow);
#endif
#ifdef PBL_ROUND
  text_layer_set_text_alignment(s_sender_layer, GTextAlignmentCenter);
#endif
  layer_add_child(window_layer, text_layer_get_layer(s_sender_layer));

  // Message text
  int text_y = inset_y + 32;
#ifdef PBL_ROUND
  int text_h = bounds.size.h - text_y - 56;
#else
  int text_h = bounds.size.h - text_y - 26;
#endif
  s_text_layer = text_layer_create(GRect(inset_x, text_y, text_w, text_h));
  text_layer_set_font(s_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text(s_text_layer, s_text_buf);
  text_layer_set_overflow_mode(s_text_layer, GTextOverflowModeWordWrap);
  text_layer_set_background_color(s_text_layer, GColorClear);
#ifdef PBL_COLOR
  text_layer_set_text_color(s_text_layer, GColorWhite);
#endif
#ifdef PBL_ROUND
  text_layer_set_text_alignment(s_text_layer, GTextAlignmentCenter);
#endif
  layer_add_child(window_layer, text_layer_get_layer(s_text_layer));

  // Hint at bottom
#ifdef PBL_ROUND
  // Stacked hint for round: two lines centered
  s_hint_layer = text_layer_create(GRect(inset_x + 5, bounds.size.h - 50, text_w - 10, 34));
  text_layer_set_text(s_hint_layer, "SEL: Reply\nBACK: Dismiss");
  text_layer_set_overflow_mode(s_hint_layer, GTextOverflowModeWordWrap);
#else
  s_hint_layer = text_layer_create(GRect(inset_x, bounds.size.h - 22, text_w, 18));
  text_layer_set_text(s_hint_layer, "SEL: Reply  BACK: Dismiss");
#endif
  text_layer_set_font(s_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_background_color(s_hint_layer, GColorClear);
#ifdef PBL_COLOR
  text_layer_set_text_color(s_hint_layer, GColorLightGray);
#endif
  text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_hint_layer));
}

static void window_unload(Window *window) {
  // NOTE: do NOT call window_destroy here — the framework is currently
  // unloading this window. Destroying it from inside its own unload is a
  // double-free that crashes on the next window operation.
  // We keep s_window alive and reuse it on the next notification.
  if (s_sender_layer) { text_layer_destroy(s_sender_layer); s_sender_layer = NULL; }
  if (s_text_layer)   { text_layer_destroy(s_text_layer);   s_text_layer = NULL; }
  if (s_hint_layer)   { text_layer_destroy(s_hint_layer);   s_hint_layer = NULL; }
}

static void show_current_notif(void) {
  if (s_notif_current >= s_notif_count) return;
  NotifItem *item = &s_notif_queue[s_notif_current];

  snprintf(s_sender_buf, sizeof(s_sender_buf), "%s", item->sender);
  if (s_notif_count > 1) {
    // Show "Sender (1/3)" for multiple notifications
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%s (%d/%d)", item->sender, s_notif_current + 1, s_notif_count);
    snprintf(s_sender_buf, sizeof(s_sender_buf), "%s", tmp);
  }
  snprintf(s_text_buf, sizeof(s_text_buf), "%s", item->text);
  snprintf(s_chat_id_buf, sizeof(s_chat_id_buf), "%s", item->chat_id);
  snprintf(s_chat_name_buf, sizeof(s_chat_name_buf), "%s", item->chat_name);

  // Update text layers if window is loaded
  if (s_sender_layer) text_layer_set_text(s_sender_layer, s_sender_buf);
  if (s_text_layer) text_layer_set_text(s_text_layer, s_text_buf);
}

static void back_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_notif_count > 1 && s_notif_current < s_notif_count - 1) {
    // Show next notification in queue
    s_notif_current++;
    show_current_notif();
  } else {
    // Dismiss all — clear queue
    s_notif_count = 0;
    s_notif_current = 0;
    window_stack_pop(true);
  }
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  // Go to previous notification
  if (s_notif_current > 0) {
    s_notif_current--;
    show_current_notif();
  }
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  // Open reply menu for current notification's chat
  window_stack_pop(false);
  reply_menu_show(s_chat_id_buf, s_chat_name_buf);
  s_notif_count = 0;
  s_notif_current = 0;
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_BACK, back_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

void show_notification(const char *sender, const char *text, const char *chat_name) {
  // Vibrate
  static const uint32_t segments[] = { 100, 100, 100 };
  VibePattern pattern = { .durations = segments, .num_segments = 3 };
  vibes_enqueue_custom_pattern(pattern);

  // Add to queue
  if (s_notif_count < MAX_NOTIF_QUEUE) {
    NotifItem *item = &s_notif_queue[s_notif_count];
    snprintf(item->sender, sizeof(item->sender), "%s", sender);
    snprintf(item->text, sizeof(item->text), "%s", text);
    snprintf(item->chat_name, sizeof(item->chat_name), "%s", chat_name);
    snprintf(item->chat_id, sizeof(item->chat_id), "%s", chat_name);
    s_notif_count++;
  }

  // Add/update chat list (use upsert so we don't clobber other chats)
  chat_list_upsert(chat_name, chat_name, text);

  // Show the latest notification
  s_notif_current = s_notif_count - 1;
  show_current_notif();

  // Create window once (lazily). We never destroy it — the framework
  // calls window_unload when popped, which frees the text layers; the
  // window object itself is reused on the next push.
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers) {
      .load = window_load,
      .unload = window_unload,
    });
    window_set_click_config_provider(s_window, click_config_provider);
  }

  // Push it if it isn't currently visible. This fires window_load and
  // rebuilds the text layers fresh. If it IS already on top, just mark
  // the root layer dirty so the new text repaints.
  if (window_stack_get_top_window() != s_window) {
    APP_LOG(APP_LOG_LEVEL_INFO, "show_notification: pushing notification window");
    window_stack_push(s_window, true);
  } else {
    APP_LOG(APP_LOG_LEVEL_INFO, "show_notification: already on top, redrawing");
    layer_mark_dirty(window_get_root_layer(s_window));
  }
}
