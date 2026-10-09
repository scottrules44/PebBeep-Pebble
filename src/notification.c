#include "notification.h"
#include "chat_list.h"
#include "reply_menu.h"
#include "touch_ui.h"

static Window *s_window;
static TextLayer *s_sender_layer;
static TextLayer *s_text_layer;
static TextLayer *s_hint_layer;   // button hints on button-only watches
static Layer *s_footer_layer;     // [Reply] [Next/Dismiss] on touch watches

static char s_sender_buf[32];
static char s_text_buf[128];
static char s_chat_id_buf[64];
static char s_chat_name_buf[24];

// Notification queue for back-to-back messages
#define MAX_NOTIF_QUEUE 5
typedef struct {
  char sender[32];
  char text[128];
  char chat_id[64];
  char chat_name[24];
} NotifItem;

static NotifItem s_notif_queue[MAX_NOTIF_QUEUE];
static int s_notif_count = 0;
static int s_notif_current = 0;
#if defined(PBL_TOUCH)
static bool s_recognizers_attached = false;
#endif

static bool prv_has_next(void) { return s_notif_current < s_notif_count - 1; }
static bool prv_has_prev(void) { return s_notif_current > 0; }

static void show_current_notif(void) {
  if (s_notif_current >= s_notif_count) return;
  NotifItem *item = &s_notif_queue[s_notif_current];

  if (s_notif_count > 1) {
    // Show "Sender (1/3)" for multiple notifications
    snprintf(s_sender_buf, sizeof(s_sender_buf), "%s (%d/%d)",
             item->sender, s_notif_current + 1, s_notif_count);
  } else {
    snprintf(s_sender_buf, sizeof(s_sender_buf), "%s", item->sender);
  }
  snprintf(s_text_buf, sizeof(s_text_buf), "%s", item->text);
  snprintf(s_chat_id_buf, sizeof(s_chat_id_buf), "%s", item->chat_id);
  snprintf(s_chat_name_buf, sizeof(s_chat_name_buf), "%s", item->chat_name);

  // Update layers if the window is loaded
  if (s_sender_layer) text_layer_set_text(s_sender_layer, s_sender_buf);
  if (s_text_layer)   text_layer_set_text(s_text_layer, s_text_buf);
  if (s_footer_layer) layer_mark_dirty(s_footer_layer);
}

// --- Actions (shared by buttons and touch) ---

// BACK: step to the next queued notification, or dismiss when on the last one
static void prv_next_or_dismiss(void) {
  if (prv_has_next()) {
    s_notif_current++;
    show_current_notif();
  } else {
    s_notif_count = 0;
    s_notif_current = 0;
    window_stack_pop(true);
  }
}

// UP: step back to the previous queued notification
static void prv_previous(void) {
  if (prv_has_prev()) {
    s_notif_current--;
    show_current_notif();
  }
}

// SELECT: open the reply menu for the current notification's chat
static void prv_reply(void) {
  window_stack_pop(false);
  reply_menu_show(s_chat_id_buf, s_chat_name_buf);
  s_notif_count = 0;
  s_notif_current = 0;
}

// --- Buttons ---

static void back_click_handler(ClickRecognizerRef recognizer, void *context) {
  prv_next_or_dismiss();
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  prv_previous();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  prv_reply();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_BACK, back_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

// --- Touch (Pebble Time 2 / Pebble Round 2) ---

#if defined(PBL_TOUCH)
#define BTN_REPLY   0
#define BTN_DISMISS 1

static void footer_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  touch_ui_draw_button(ctx, touch_ui_footer_button_rect(b, 2, BTN_REPLY), "Reply", true);
  touch_ui_draw_button(ctx, touch_ui_footer_button_rect(b, 2, BTN_DISMISS),
                       prv_has_next() ? "Next" : "Dismiss", false);
}

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
    case BTN_REPLY:   prv_reply(); break;
    case BTN_DISMISS: prv_next_or_dismiss(); break;
    default: break;
  }
}

// Swipe left/right pages through queued notifications; swiping right on the
// first one acts like BACK, matching the system-wide "swipe right = back".
static void swipe_handler(const Recognizer *recognizer, RecognizerEvent event) {
  if (event != RecognizerEvent_Completed) return;
  switch (swipe_recognizer_get_direction(recognizer)) {
    case SwipeDirection_Left:
      if (prv_has_next()) prv_next_or_dismiss();
      break;
    case SwipeDirection_Right:
      if (prv_has_prev()) prv_previous();
      else prv_next_or_dismiss();
      break;
    default:
      break;
  }
}
#endif

// --- Window ---

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  const bool touch = touch_ui_available();

#ifdef PBL_COLOR
  window_set_background_color(window, GColorDarkGray);
#endif

#ifdef PBL_ROUND
  int inset_x = bounds.size.w * 30 / 180;
  int inset_y = bounds.size.h * 30 / 180;
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

  // Bottom strip: either touch buttons or a text hint
  int bottom_reserved;
  GRect footer = touch_ui_footer_rect(bounds);
  if (touch) {
    bottom_reserved = bounds.size.h - footer.origin.y + 2;
  } else {
#ifdef PBL_ROUND
    bottom_reserved = 56;
#else
    bottom_reserved = 26;
#endif
  }

  // Message text
  int text_y = inset_y + 32;
  int text_h = bounds.size.h - text_y - bottom_reserved;
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

  if (touch) {
#if defined(PBL_TOUCH)
    s_footer_layer = layer_create(footer);
    layer_set_update_proc(s_footer_layer, footer_update_proc);
    layer_add_child(window_layer, s_footer_layer);

    // Own the touches: tap the buttons, swipe between queued notifications.
    // Recognizers live on the window's root layer for the life of the window
    // (the firmware does not free them on unload), and this window is reused
    // for every notification, so attach them exactly once.
    window_set_touch_bridge_disabled(window, true);
    if (!s_recognizers_attached) {
      window_attach_recognizer(window, tap_recognizer_create(tap_handler, NULL));
      window_attach_recognizer(window, swipe_recognizer_create(swipe_handler, NULL,
                               SwipeDirection_Left | SwipeDirection_Right));
      s_recognizers_attached = true;
    }
#endif
  } else {
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
}

static void window_unload(Window *window) {
  // NOTE: do NOT call window_destroy here — the framework is currently
  // unloading this window. Destroying it from inside its own unload is a
  // double-free that crashes on the next window operation.
  // We keep s_window alive and reuse it on the next notification.
  if (s_sender_layer) { text_layer_destroy(s_sender_layer); s_sender_layer = NULL; }
  if (s_text_layer)   { text_layer_destroy(s_text_layer);   s_text_layer = NULL; }
  if (s_hint_layer)   { text_layer_destroy(s_hint_layer);   s_hint_layer = NULL; }
  if (s_footer_layer) { layer_destroy(s_footer_layer);      s_footer_layer = NULL; }
}

void show_notification(const char *sender, const char *text, const char *chat_name) {
  // Vibrate
  static const uint32_t segments[] = { 100, 100, 100 };
  VibePattern pattern = { .durations = segments, .num_segments = 3 };
  vibes_enqueue_custom_pattern(pattern);

  // Add to queue; when full, drop the oldest so the newest is never lost
  if (s_notif_count >= MAX_NOTIF_QUEUE) {
    memmove(&s_notif_queue[0], &s_notif_queue[1], sizeof(NotifItem) * (MAX_NOTIF_QUEUE - 1));
    s_notif_count = MAX_NOTIF_QUEUE - 1;
  }
  NotifItem *item = &s_notif_queue[s_notif_count];
  snprintf(item->sender, sizeof(item->sender), "%s", sender);
  snprintf(item->text, sizeof(item->text), "%s", text);
  snprintf(item->chat_name, sizeof(item->chat_name), "%s", chat_name);
  snprintf(item->chat_id, sizeof(item->chat_id), "%s", chat_name);
  s_notif_count++;

  // Add/update chat list (use upsert so we don't clobber other chats)
  chat_list_upsert(chat_name, chat_name, text);

  // Show the latest notification
  s_notif_current = s_notif_count - 1;
  show_current_notif();

  // Create window once (lazily). We never destroy it — the framework
  // calls window_unload when popped, which frees the layers; the
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
  // rebuilds the layers fresh. If it IS already on top, just mark
  // the root layer dirty so the new text repaints.
  if (window_stack_get_top_window() != s_window) {
    APP_LOG(APP_LOG_LEVEL_INFO, "show_notification: pushing notification window");
    window_stack_push(s_window, true);
  } else {
    APP_LOG(APP_LOG_LEVEL_INFO, "show_notification: already on top, redrawing");
    layer_mark_dirty(window_get_root_layer(s_window));
  }
}
