#include "message_view.h"
#include "reply_menu.h"

typedef struct {
  char sender[MSG_SENDER_LEN];
  char text[MSG_TEXT_LEN];
  char time[MSG_TIME_LEN];
} MessageItem;

// Compact message for persistent storage (fits in 256 bytes per persist key)
typedef struct {
  char sender[16];
  char text[60];
  char time[8];
} CompactMessage;

// Persistent storage: keys 200-214 (3 msgs x 5 chats = 15 slots)
// Key 200 = chat 0 msg 0, 201 = chat 0 msg 1, 202 = chat 0 msg 2
// Key 203 = chat 1 msg 0, etc.
// Key 215-219 = chat IDs (hashes) for slots 0-4
// Key 220-224 = message counts per chat slot
#define PERSIST_MSG_BASE 200
#define PERSIST_CHAT_ID_BASE 215
#define PERSIST_MSG_COUNT_BASE 220
#define MAX_STORED_CHATS 5
#define MSGS_PER_CHAT 3

static Window *s_window;
static ScrollLayer *s_scroll_layer;
static Layer *s_draw_layer;
static MessageItem s_messages[MAX_MESSAGES];
static int s_message_count = 0;
static char s_chat_id[64];
static char s_chat_name[24];
static int s_content_height = 0;

// Simple hash for chat ID (to fit in persist key)
static uint32_t hash_chat_id(const char *id) {
  uint32_t h = 5381;
  while (*id) { h = ((h << 5) + h) + (uint8_t)*id++; }
  return h;
}

// Find or allocate a persistent storage slot for this chat
static int find_chat_slot(const char *chat_id) {
  uint32_t hash = hash_chat_id(chat_id);

  // Check existing slots
  for (int i = 0; i < MAX_STORED_CHATS; i++) {
    if (persist_exists(PERSIST_CHAT_ID_BASE + i)) {
      uint32_t stored = (uint32_t)persist_read_int(PERSIST_CHAT_ID_BASE + i);
      if (stored == hash) return i;
    }
  }

  // Find empty slot
  for (int i = 0; i < MAX_STORED_CHATS; i++) {
    if (!persist_exists(PERSIST_CHAT_ID_BASE + i)) {
      persist_write_int(PERSIST_CHAT_ID_BASE + i, (int32_t)hash);
      persist_write_int(PERSIST_MSG_COUNT_BASE + i, 0);
      return i;
    }
  }

  // All full — overwrite oldest (slot 0)
  persist_write_int(PERSIST_CHAT_ID_BASE, (int32_t)hash);
  persist_write_int(PERSIST_MSG_COUNT_BASE, 0);
  return 0;
}

// Save current messages to persistent storage
static void save_messages_to_storage(void) {
  int slot = find_chat_slot(s_chat_id);
  int base = PERSIST_MSG_BASE + (slot * MSGS_PER_CHAT);

  // Save last MSGS_PER_CHAT messages
  int start = s_message_count > MSGS_PER_CHAT ? s_message_count - MSGS_PER_CHAT : 0;
  int count = 0;

  for (int i = start; i < s_message_count && count < MSGS_PER_CHAT; i++) {
    CompactMessage cm;
    memset(&cm, 0, sizeof(cm));
    strncpy(cm.sender, s_messages[i].sender, sizeof(cm.sender) - 1);
    strncpy(cm.text, s_messages[i].text, sizeof(cm.text) - 1);
    strncpy(cm.time, s_messages[i].time, sizeof(cm.time) - 1);
    persist_write_data(base + count, &cm, sizeof(cm));
    count++;
  }
  persist_write_int(PERSIST_MSG_COUNT_BASE + slot, count);
  APP_LOG(APP_LOG_LEVEL_INFO, "Saved %d messages for chat slot %d", count, slot);
}

// Load messages from persistent storage
static void load_messages_from_storage(void) {
  int slot = find_chat_slot(s_chat_id);
  int base = PERSIST_MSG_BASE + (slot * MSGS_PER_CHAT);
  int count = persist_exists(PERSIST_MSG_COUNT_BASE + slot)
    ? persist_read_int(PERSIST_MSG_COUNT_BASE + slot) : 0;

  if (count <= 0) return;

  APP_LOG(APP_LOG_LEVEL_INFO, "Loading %d stored messages for chat slot %d", count, slot);

  for (int i = 0; i < count && i < MSGS_PER_CHAT; i++) {
    CompactMessage cm;
    if (persist_read_data(base + i, &cm, sizeof(cm)) == sizeof(cm)) {
      strncpy(s_messages[i].sender, cm.sender, MSG_SENDER_LEN - 1);
      s_messages[i].sender[MSG_SENDER_LEN - 1] = '\0';
      strncpy(s_messages[i].text, cm.text, MSG_TEXT_LEN - 1);
      s_messages[i].text[MSG_TEXT_LEN - 1] = '\0';
      strncpy(s_messages[i].time, cm.time, MSG_TIME_LEN - 1);
      s_messages[i].time[MSG_TIME_LEN - 1] = '\0';
    }
  }
  s_message_count = count;
}

// Calculate height needed for a message bubble
static int calc_message_height(GRect bounds, int idx) {
  int text_w = bounds.size.w - 20;
#ifdef PBL_ROUND
  text_w = bounds.size.w - 50; // more inset on round
#endif

  GSize sender_size = graphics_text_layout_get_content_size(
    s_messages[idx].sender,
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(0, 0, text_w, 100),
    GTextOverflowModeWordWrap, GTextAlignmentLeft
  );

  GSize text_size = graphics_text_layout_get_content_size(
    s_messages[idx].text,
    fonts_get_system_font(FONT_KEY_GOTHIC_18),
    GRect(0, 0, text_w, 500),
    GTextOverflowModeWordWrap, GTextAlignmentLeft
  );

  // sender + text + time + padding
  return sender_size.h + text_size.h + 16 + 12;
}

static void draw_messages(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

#ifdef PBL_ROUND
  int inset_x = 30;
  int y = 28; // extra top padding for circular cutoff
#else
  int inset_x = 10;
  int y = 8;
#endif
  int text_w = bounds.size.w - (inset_x * 2);

  for (int i = 0; i < s_message_count; i++) {
    bool is_me = (strcmp(s_messages[i].sender, "Me") == 0);

    // Sender name
#ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, is_me ? GColorVividCerulean : GColorChromeYellow);
#else
    graphics_context_set_text_color(ctx, GColorBlack);
#endif
    GSize sender_size = graphics_text_layout_get_content_size(
      s_messages[i].sender,
      fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
      GRect(0, 0, text_w, 100),
      GTextOverflowModeWordWrap,
#ifdef PBL_ROUND
      GTextAlignmentCenter
#else
      is_me ? GTextAlignmentRight : GTextAlignmentLeft
#endif
    );
    graphics_draw_text(ctx, s_messages[i].sender,
      fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
      GRect(inset_x, y, text_w, sender_size.h + 2),
      GTextOverflowModeWordWrap,
#ifdef PBL_ROUND
      GTextAlignmentCenter,
#else
      is_me ? GTextAlignmentRight : GTextAlignmentLeft,
#endif
      NULL);
    y += sender_size.h + 2;

    // Message text
#ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorWhite);
#else
    graphics_context_set_text_color(ctx, GColorBlack);
#endif
    GSize text_size = graphics_text_layout_get_content_size(
      s_messages[i].text,
      fonts_get_system_font(FONT_KEY_GOTHIC_18),
      GRect(0, 0, text_w, 500),
      GTextOverflowModeWordWrap,
#ifdef PBL_ROUND
      GTextAlignmentCenter
#else
      is_me ? GTextAlignmentRight : GTextAlignmentLeft
#endif
    );
    graphics_draw_text(ctx, s_messages[i].text,
      fonts_get_system_font(FONT_KEY_GOTHIC_18),
      GRect(inset_x, y, text_w, text_size.h + 2),
      GTextOverflowModeWordWrap,
#ifdef PBL_ROUND
      GTextAlignmentCenter,
#else
      is_me ? GTextAlignmentRight : GTextAlignmentLeft,
#endif
      NULL);
    y += text_size.h + 2;

    // Time
    if (s_messages[i].time[0] != '\0') {
#ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, GColorDarkGray);
#endif
      graphics_draw_text(ctx, s_messages[i].time,
        fonts_get_system_font(FONT_KEY_GOTHIC_14),
        GRect(inset_x, y, text_w, 16),
        GTextOverflowModeTrailingEllipsis,
#ifdef PBL_ROUND
        GTextAlignmentCenter,
#else
        is_me ? GTextAlignmentRight : GTextAlignmentLeft,
#endif
        NULL);
    }
    y += 16;

    // Separator line
    y += 4;
#ifdef PBL_COLOR
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
#endif
    graphics_draw_line(ctx, GPoint(inset_x + 10, y), GPoint(bounds.size.w - inset_x - 10, y));
    y += 8;
  }

  if (s_message_count == 0) {
#ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorLightGray);
#endif
    graphics_draw_text(ctx, "Loading messages...",
      fonts_get_system_font(FONT_KEY_GOTHIC_18),
      GRect(0, bounds.size.h / 3, bounds.size.w, 30),
      GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }

#ifdef PBL_ROUND
  s_content_height = y + 28; // extra bottom padding for circular cutoff
#else
  s_content_height = y + 8;
#endif
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  reply_menu_show(s_chat_id, s_chat_name);
}

static void scroll_click_config_provider(void *context) {
  // Let scroll layer handle UP/DOWN, we only add SELECT
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

static ScrollLayerCallbacks s_scroll_callbacks;

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

#ifdef PBL_COLOR
  window_set_background_color(window, GColorBlack);
#endif

  s_scroll_layer = scroll_layer_create(bounds);
  scroll_layer_set_click_config_onto_window(s_scroll_layer, window);

  // Add SELECT button for reply without breaking scroll
  scroll_layer_set_callbacks(s_scroll_layer, (ScrollLayerCallbacks) {
    .click_config_provider = scroll_click_config_provider,
  });

  // Custom draw layer for messages
  s_draw_layer = layer_create(GRect(0, 0, bounds.size.w, 2000));
  layer_set_update_proc(s_draw_layer, draw_messages);
  scroll_layer_add_child(s_scroll_layer, s_draw_layer);

  // Initial content size — will be updated after draw
  scroll_layer_set_content_size(s_scroll_layer, GSize(bounds.size.w, 400));

#ifdef PBL_ROUND
  scroll_layer_set_paging(s_scroll_layer, true);
#endif

  layer_add_child(window_layer, scroll_layer_get_layer(s_scroll_layer));
}

static void window_unload(Window *window) {
  // Save messages before closing
  if (s_message_count > 0) {
    save_messages_to_storage();
  }
  if (s_draw_layer) {
    layer_destroy(s_draw_layer);
    s_draw_layer = NULL;
  }
  if (s_scroll_layer) {
    scroll_layer_destroy(s_scroll_layer);
    s_scroll_layer = NULL;
  }
}

static char s_last_chat_id[64] = "";

void message_view_show(const char *chat_id, const char *chat_name) {
  // Only reset messages if switching to a different chat
  bool same_chat = (strcmp(s_chat_id, chat_id) == 0);

  strncpy(s_chat_id, chat_id, sizeof(s_chat_id) - 1);
  s_chat_id[sizeof(s_chat_id) - 1] = '\0';
  strncpy(s_chat_name, chat_name, sizeof(s_chat_name) - 1);
  s_chat_name[sizeof(s_chat_name) - 1] = '\0';

  if (!same_chat) {
    // Save current chat's messages before switching
    if (s_message_count > 0 && strlen(s_last_chat_id) > 0) {
      save_messages_to_storage();
    }
    s_message_count = 0;
    strncpy(s_last_chat_id, chat_id, sizeof(s_last_chat_id) - 1);

    // Load stored messages for the new chat
    load_messages_from_storage();
  }

  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers) {
      .load = window_load,
      .unload = window_unload,
    });
  }

  window_stack_push(s_window, true);

  // Load demo messages if this is a demo chat
  if (strncmp(chat_id, "demo-", 5) == 0) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Loading demo messages for %s", chat_name);
    if (strcmp(chat_id, "demo-1") == 0) {
      message_view_add_message(0, "Alice", "Hey! Are you free tonight?", "6:30 PM");
      message_view_add_message(1, "Me", "Maybe, what's up?", "6:32 PM");
      message_view_add_message(2, "Alice", "Dinner at 8? That new place downtown", "6:33 PM");
    } else if (strcmp(chat_id, "demo-2") == 0) {
      message_view_add_message(0, "Boss", "Team meeting moved to 3pm", "2:15 PM");
      message_view_add_message(1, "Sarah", "Thanks for the heads up", "2:16 PM");
      message_view_add_message(2, "Boss", "Please review the doc before", "2:18 PM");
    } else if (strcmp(chat_id, "demo-3") == 0) {
      message_view_add_message(0, "Mom", "Hi sweetie, call me when you can", "11:00 AM");
      message_view_add_message(1, "Mom", "It's about the weekend plans", "11:02 AM");
    } else {
      message_view_add_message(0, chat_name, "Check out this cool link!", "5:00 PM");
      message_view_add_message(1, "Me", "Nice, thanks!", "5:05 PM");
    }
  }
}

void message_view_add_message(int index, const char *sender, const char *text, const char *time_str) {
  // Safety: ignore empty/null inputs that would corrupt state
  if (!sender || !text) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "add_message: null sender/text, ignoring");
    return;
  }

  // If the message_view window isn't currently visible, don't touch the
  // in-memory buffer at all — it belongs to whichever chat the user last
  // opened, and appending foreign messages would corrupt it AND cause the
  // layer-touching code paths below to run against stale pointers.
  bool window_visible = (s_window != NULL)
    && (window_stack_get_top_window() == s_window)
    && (s_draw_layer != NULL)
    && (s_scroll_layer != NULL);

  APP_LOG(APP_LOG_LEVEL_INFO, "add_message: visible=%d count=%d '%s': '%s'",
    window_visible, s_message_count, sender, text);

  if (!window_visible) {
    APP_LOG(APP_LOG_LEVEL_INFO, "  window not visible, skipping in-memory add");
    return;
  }

  // Check for duplicate (same sender + same text already exists)
  for (int i = 0; i < s_message_count; i++) {
    if (strcmp(s_messages[i].sender, sender) == 0 &&
        strcmp(s_messages[i].text, text) == 0) {
      APP_LOG(APP_LOG_LEVEL_INFO, "Skipping duplicate message: %s: %s", sender, text);
      return; // Skip duplicate
    }
  }

  // index == -1 means append to end
  if (index < 0 || index >= s_message_count) {
    index = s_message_count;
    if (index >= MAX_MESSAGES) {
      // Shift messages up to make room
      for (int i = 0; i < MAX_MESSAGES - 1; i++) {
        s_messages[i] = s_messages[i + 1];
      }
      index = MAX_MESSAGES - 1;
      s_message_count = MAX_MESSAGES;
    }
  }
  if (index >= MAX_MESSAGES) return;

  strncpy(s_messages[index].sender, sender, MSG_SENDER_LEN - 1);
  s_messages[index].sender[MSG_SENDER_LEN - 1] = '\0';

  strncpy(s_messages[index].text, text, MSG_TEXT_LEN - 1);
  s_messages[index].text[MSG_TEXT_LEN - 1] = '\0';

  strncpy(s_messages[index].time, time_str, MSG_TIME_LEN - 1);
  s_messages[index].time[MSG_TIME_LEN - 1] = '\0';

  if (index + 1 > s_message_count) {
    s_message_count = index + 1;
  }

  // Redraw and update scroll size (only if window is loaded)
  if (s_draw_layer && s_scroll_layer && s_window) {
    layer_mark_dirty(s_draw_layer);

    // Update content size after a short delay to let draw calculate height
    GRect bounds = layer_get_bounds(window_get_root_layer(s_window));
    int estimated_h = 0;
    for (int i = 0; i < s_message_count; i++) {
      estimated_h += calc_message_height(bounds, i);
    }
#ifdef PBL_ROUND
    estimated_h += 56; // top + bottom circular padding
#else
    estimated_h += 16;
#endif
    scroll_layer_set_content_size(s_scroll_layer, GSize(bounds.size.w, estimated_h));

    // Scroll to bottom
    GPoint offset = GPoint(0, -(estimated_h - bounds.size.h));
    if (offset.y < 0) {
      scroll_layer_set_content_offset(s_scroll_layer, offset, true);
    }
  }

  // Auto-save to persistent storage
  save_messages_to_storage();
}
