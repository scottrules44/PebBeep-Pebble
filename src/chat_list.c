#include "chat_list.h"
#include "message_view.h"
#include "reply_menu.h"

typedef struct {
  char id[CHAT_ID_LEN];
  char name[CHAT_NAME_LEN];
  char preview[CHAT_PREVIEW_LEN];
} ChatItem;

static Window *s_window;
static MenuLayer *s_menu_layer;
static ChatItem s_chats[MAX_CHATS];
static int s_chat_count = 0;

// Persistent storage for chat list
#define PERSIST_CHAT_LIST_COUNT 250
#define PERSIST_CHAT_LIST_BASE  251  // 251..251+MAX_CHATS-1

static void chat_list_save(void) {
  persist_write_int(PERSIST_CHAT_LIST_COUNT, s_chat_count);
  for (int i = 0; i < s_chat_count; i++) {
    persist_write_data(PERSIST_CHAT_LIST_BASE + i, &s_chats[i], sizeof(ChatItem));
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "chat_list_save: persisted %d chats", s_chat_count);
}

static void chat_list_load(void) {
  if (!persist_exists(PERSIST_CHAT_LIST_COUNT)) {
    APP_LOG(APP_LOG_LEVEL_INFO, "chat_list_load: no persisted chats");
    return;
  }
  int count = persist_read_int(PERSIST_CHAT_LIST_COUNT);
  if (count > MAX_CHATS) count = MAX_CHATS;
  if (count < 0) count = 0;
  int loaded = 0;
  for (int i = 0; i < count; i++) {
    if (persist_exists(PERSIST_CHAT_LIST_BASE + i)) {
      if (persist_read_data(PERSIST_CHAT_LIST_BASE + i, &s_chats[i], sizeof(ChatItem)) == sizeof(ChatItem)) {
        loaded++;
      }
    }
  }
  s_chat_count = loaded;
  APP_LOG(APP_LOG_LEVEL_INFO, "chat_list_load: restored %d chats", loaded);
}

// Service icons (loaded once) — available on all platforms including BW
static GBitmap *s_icons[14] = {NULL};
static bool s_icons_loaded = false;

static const uint32_t s_icon_resources[14] = {
  RESOURCE_ID_ICON_IMESSAGE, RESOURCE_ID_ICON_SLACK, RESOURCE_ID_ICON_SIGNAL,
  RESOURCE_ID_ICON_WHATSAPP, RESOURCE_ID_ICON_TELEGRAM, RESOURCE_ID_ICON_INSTAGRAM,
  RESOURCE_ID_ICON_MESSENGER, RESOURCE_ID_ICON_DISCORD, RESOURCE_ID_ICON_TWITTER,
  RESOURCE_ID_ICON_LINKEDIN, RESOURCE_ID_ICON_GOOGLECHAT, RESOURCE_ID_ICON_GMESSAGES,
  RESOURCE_ID_ICON_MATRIX, RESOURCE_ID_ICON_CHAT,
};

static void load_icons(void) {
  if (s_icons_loaded) return;
  APP_LOG(APP_LOG_LEVEL_INFO, "Loading service icons... heap free=%d", (int)heap_bytes_free());
  int loaded = 0;
  for (int i = 0; i < 14; i++) {
    s_icons[i] = gbitmap_create_with_resource(s_icon_resources[i]);
    if (s_icons[i]) {
      GRect b = gbitmap_get_bounds(s_icons[i]);
      APP_LOG(APP_LOG_LEVEL_INFO, "  icon[%d] OK res=%d bounds=%dx%d",
        i, (int)s_icon_resources[i], b.size.w, b.size.h);
      loaded++;
    } else {
      APP_LOG(APP_LOG_LEVEL_ERROR, "  icon[%d] FAILED to load res=%d",
        i, (int)s_icon_resources[i]);
    }
  }
  s_icons_loaded = true;
  APP_LOG(APP_LOG_LEVEL_INFO, "Loaded %d/14 service icons, heap free=%d",
    loaded, (int)heap_bytes_free());
}

static void destroy_icons(void) {
  for (int i = 0; i < 14; i++) {
    if (s_icons[i]) { gbitmap_destroy(s_icons[i]); s_icons[i] = NULL; }
  }
  s_icons_loaded = false;
}

// Get icon index from service label in chat name (e.g. "[iMsg] Name")
static int get_service_icon_index(const char *name) {
  if (!name || name[0] != '[') return 13; // generic chat
  if (strncmp(name, "[iMsg]", 6) == 0) return 0;
  if (strncmp(name, "[Slack]", 7) == 0) return 1;
  if (strncmp(name, "[Signal]", 8) == 0) return 2;
  if (strncmp(name, "[WA]", 4) == 0) return 3;
  if (strncmp(name, "[TG]", 4) == 0) return 4;
  if (strncmp(name, "[IG]", 4) == 0) return 5;
  if (strncmp(name, "[FB]", 4) == 0) return 6;
  if (strncmp(name, "[Discord]", 9) == 0) return 7;
  if (strncmp(name, "[X]", 3) == 0) return 8;
  if (strncmp(name, "[LinkedIn]", 10) == 0) return 9;
  if (strncmp(name, "[GChat]", 7) == 0) return 10;
  if (strncmp(name, "[GMsg]", 6) == 0) return 11;
  if (strncmp(name, "[Matrix]", 8) == 0) return 12;
  return 13;
}

// Get display name without service prefix
static const char *get_display_name(const char *name) {
  if (!name || name[0] != '[') return name;
  const char *p = strchr(name, ']');
  if (p && *(p+1) == ' ') return p + 2;
  if (p) return p + 1;
  return name;
}

// Forward declarations
extern void send_command(const char *command);
extern void request_messages(const char *chat_id);

static uint16_t menu_get_num_sections(MenuLayer *menu_layer, void *data) {
  return 1;
}

static uint16_t menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return s_chat_count > 0 ? s_chat_count : 1;
}

static int16_t menu_get_header_height(MenuLayer *menu_layer, uint16_t section_index, void *data) {
#ifdef PBL_ROUND
  return 0; // No header on round — wastes precious circular space
#else
  return 22;
#endif
}

static void menu_draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
#ifndef PBL_ROUND
  GRect bounds = layer_get_bounds(cell_layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);

  char header[32];
  snprintf(header, sizeof(header), "PebBeep  %d chats", s_chat_count);
  graphics_draw_text(ctx, header,
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(8, 1, bounds.size.w - 16, 18),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
#endif
}

static int16_t menu_get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
#ifdef PBL_ROUND
  // Taller cells on round for better readability in the curved area
  if (menu_layer_is_index_selected(menu_layer, cell_index)) {
    return 68; // Selected (center) cell is taller
  }
  return 48;
#else
  return 52;
#endif
}

static void menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  GRect bounds = layer_get_bounds(cell_layer);
  bool is_selected = menu_layer_is_index_selected(s_menu_layer, cell_index);
  APP_LOG(APP_LOG_LEVEL_INFO, "menu_draw_row: row=%d count=%d bounds=%dx%d",
    cell_index->row, s_chat_count, bounds.size.w, bounds.size.h);

  if (s_chat_count == 0) {
    // Loading state
#ifdef PBL_COLOR
    if (is_selected) {
      graphics_context_set_fill_color(ctx, GColorDarkGray);
      graphics_fill_rect(ctx, bounds, 0, GCornerNone);
      graphics_context_set_text_color(ctx, GColorWhite);
    } else {
      graphics_context_set_text_color(ctx, GColorLightGray);
    }
#endif
    graphics_draw_text(ctx, "Loading chats...",
      fonts_get_system_font(FONT_KEY_GOTHIC_18),
      GRect(0, 12, bounds.size.w, 24),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    return;
  }

  int idx = cell_index->row;
  if (idx >= s_chat_count) return;

#ifdef PBL_ROUND
  // Round display layout — centered text, more padding
  int inset = 16;
  int text_w = bounds.size.w - (inset * 2);

  // Highlight selected row
#ifdef PBL_COLOR
  if (is_selected) {
    graphics_context_set_fill_color(ctx, GColorDukeBlue);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);
  } else {
    graphics_context_set_text_color(ctx, GColorLightGray);
  }
#endif

  // Service icon, centered horizontally above the text (all platforms)
  int icon_idx_r = get_service_icon_index(s_chats[idx].name);
  const char *display_name_r = get_display_name(s_chats[idx].name);
  if (s_icons[icon_idx_r]) {
    GRect ib = gbitmap_get_bounds(s_icons[icon_idx_r]);
    GRect dest = GRect((bounds.size.w - ib.size.w) / 2, is_selected ? 2 : 4,
                       ib.size.w, ib.size.h);
#ifdef PBL_BW
    graphics_context_set_compositing_mode(ctx, is_selected ? GCompOpAssignInverted : GCompOpAssign);
#else
    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
#endif
    graphics_draw_bitmap_in_rect(ctx, s_icons[icon_idx_r], dest);
  }

  if (is_selected) {
    // Selected: show name + preview, centered (shifted down for icon)
    graphics_draw_text(ctx, display_name_r,
      fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
      GRect(inset, 24, text_w, 28),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    // Preview text below name
#ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, is_selected ? GColorCeleste : GColorDarkGray);
#endif
    graphics_draw_text(ctx, s_chats[idx].preview,
      fonts_get_system_font(FONT_KEY_GOTHIC_14),
      GRect(inset, 50, text_w, 20),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  } else {
    // Non-selected: just name, centered, below icon
    graphics_draw_text(ctx, display_name_r,
      fonts_get_system_font(FONT_KEY_GOTHIC_18),
      GRect(inset, 24, text_w, 24),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

#else
  // Rectangular display layout — left-aligned, compact

  // Draw selection highlight
#ifdef PBL_COLOR
  if (is_selected) {
    graphics_context_set_fill_color(ctx, GColorDukeBlue);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);
  }
#endif

  // Service icon + text layout (both color and BW)
  int icon_idx = get_service_icon_index(s_chats[idx].name);
  const char *display_name = get_display_name(s_chats[idx].name);

  // Draw service icon
  if (s_icons[icon_idx]) {
    GRect ib = gbitmap_get_bounds(s_icons[icon_idx]);
    GRect dest = GRect(6, (bounds.size.h - ib.size.h) / 2, ib.size.w, ib.size.h);
#ifdef PBL_BW
    // On BW the auto-converted bitmap is 1-bit. Use Assign for the normal
    // cell and AssignInverted when the row is selected (inverted colors).
    graphics_context_set_compositing_mode(ctx, is_selected ? GCompOpAssignInverted : GCompOpAssign);
#else
    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
#endif
    graphics_draw_bitmap_in_rect(ctx, s_icons[icon_idx], dest);
  }

#ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, is_selected ? GColorWhite : GColorBlack);
#else
  graphics_context_set_text_color(ctx, is_selected ? GColorWhite : GColorBlack);
#endif

  int text_x = 30;
  int text_w_rect = bounds.size.w - text_x - 4;

  // Chat name (without service prefix)
  graphics_draw_text(ctx, display_name,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
    GRect(text_x, 2, text_w_rect, 22),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Preview
#ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, is_selected ? GColorCeleste : GColorDarkGray);
#else
  graphics_context_set_text_color(ctx, is_selected ? GColorWhite : GColorBlack);
#endif
  graphics_draw_text(ctx, s_chats[idx].preview,
    fonts_get_system_font(FONT_KEY_GOTHIC_14),
    GRect(text_x, 24, text_w_rect, 20),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
#endif
}

static void menu_select_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (s_chat_count == 0) return;
  int idx = cell_index->row;
  if (idx >= s_chat_count) return;

  request_messages(s_chats[idx].id);
  message_view_show(s_chats[idx].id, s_chats[idx].name);
}

static void menu_long_select_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (s_chat_count == 0) return;
  int idx = cell_index->row;
  if (idx >= s_chat_count) return;

  reply_menu_show(s_chats[idx].id, s_chats[idx].name);
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  load_icons();

  s_menu_layer = menu_layer_create(bounds);

#ifdef PBL_ROUND
  menu_layer_set_center_focused(s_menu_layer, true);
#endif

#ifdef PBL_COLOR
  // White background, dark text for non-selected; blue/white for selected
  menu_layer_set_normal_colors(s_menu_layer, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_menu_layer, GColorDukeBlue, GColorWhite);
#endif

  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_sections = menu_get_num_sections,
    .get_num_rows = menu_get_num_rows,
    .get_cell_height = menu_get_cell_height,
    .get_header_height = menu_get_header_height,
    .draw_header = menu_draw_header,
    .draw_row = menu_draw_row,
    .select_click = menu_select_click,
    .select_long_click = menu_long_select_click,
  });

  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
}

void chat_list_init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  s_chat_count = 0;
  chat_list_load();
}

void chat_list_deinit(void) {
  destroy_icons();
  window_destroy(s_window);
}

void chat_list_show(void) {
  window_stack_push(s_window, true);
}

void chat_list_add_item(int index, int total, const char *id, const char *name, const char *preview) {
  if (index >= MAX_CHATS) return;

  strncpy(s_chats[index].id, id, CHAT_ID_LEN - 1);
  s_chats[index].id[CHAT_ID_LEN - 1] = '\0';

  strncpy(s_chats[index].name, name, CHAT_NAME_LEN - 1);
  s_chats[index].name[CHAT_NAME_LEN - 1] = '\0';

  strncpy(s_chats[index].preview, preview, CHAT_PREVIEW_LEN - 1);
  s_chats[index].preview[CHAT_PREVIEW_LEN - 1] = '\0';

  if (index + 1 > s_chat_count) {
    s_chat_count = index + 1;
  }

  if (total > 0 && total < s_chat_count) {
    s_chat_count = total;
  }

  if (s_menu_layer) {
    menu_layer_reload_data(s_menu_layer);
  }
}

void chat_list_upsert(const char *id, const char *name, const char *preview) {
  APP_LOG(APP_LOG_LEVEL_INFO, "chat_list_upsert: id='%s' name='%s' preview='%s' (count=%d)",
    id, name, preview, s_chat_count);

  // Find existing chat by id
  int found = -1;
  for (int i = 0; i < s_chat_count; i++) {
    if (strcmp(s_chats[i].id, id) == 0) {
      found = i;
      break;
    }
  }

  int slot;
  if (found >= 0) {
    slot = found;
    APP_LOG(APP_LOG_LEVEL_INFO, "  -> updating existing slot %d", slot);
  } else if (s_chat_count < MAX_CHATS) {
    slot = s_chat_count;
    s_chat_count++;
    APP_LOG(APP_LOG_LEVEL_INFO, "  -> adding new slot %d (count now %d)", slot, s_chat_count);
  } else {
    slot = MAX_CHATS - 1;
    APP_LOG(APP_LOG_LEVEL_WARNING, "  -> chat list full, overwriting slot %d", slot);
  }

  strncpy(s_chats[slot].id, id, CHAT_ID_LEN - 1);
  s_chats[slot].id[CHAT_ID_LEN - 1] = '\0';
  // Only overwrite name on insert, or if caller provided a "richer" name
  // (i.e. don't clobber "[iMsg] Alice" with raw chat_id from send_reply)
  bool name_is_just_id = (strcmp(name, id) == 0);
  if (found < 0 || !name_is_just_id) {
    strncpy(s_chats[slot].name, name, CHAT_NAME_LEN - 1);
    s_chats[slot].name[CHAT_NAME_LEN - 1] = '\0';
  }
  strncpy(s_chats[slot].preview, preview, CHAT_PREVIEW_LEN - 1);
  s_chats[slot].preview[CHAT_PREVIEW_LEN - 1] = '\0';

  if (s_menu_layer) {
    menu_layer_reload_data(s_menu_layer);
    APP_LOG(APP_LOG_LEVEL_INFO, "  -> menu reloaded");
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "  -> menu_layer is NULL, cannot reload!");
  }

  chat_list_save();
}
