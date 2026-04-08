#include "reply_menu.h"

// Forward declaration
extern void send_reply(const char *chat_id, const char *text);

static Window *s_window;
static MenuLayer *s_menu_layer;
static char s_chat_id[64];
static char s_chat_name[24];

// Voice dictation
#ifdef PBL_MICROPHONE
static DictationSession *s_dictation_session = NULL;
static char s_voice_buffer[512];
#endif

// Replies — custom from config, defaults if not set
static char s_replies[MAX_REPLIES][REPLY_TEXT_LEN];
static int s_reply_count = 0;

#define STORAGE_KEY_REPLY_COUNT 100
#define STORAGE_KEY_REPLY_BASE  101

#ifdef PBL_MICROPHONE
  #define HAS_VOICE_ROW 1
#else
  #define HAS_VOICE_ROW 0
#endif

static void load_replies(void) {
  int stored = persist_exists(STORAGE_KEY_REPLY_COUNT)
    ? persist_read_int(STORAGE_KEY_REPLY_COUNT) : 0;

  if (stored > 0 && stored <= MAX_REPLIES) {
    // Load user-configured replies
    s_reply_count = 0;
    for (int i = 0; i < stored; i++) {
      int bytes = persist_read_string(STORAGE_KEY_REPLY_BASE + i,
                                       s_replies[s_reply_count], REPLY_TEXT_LEN);
      if (bytes > 0 && s_replies[s_reply_count][0] != '\0') {
        s_reply_count++;
      }
    }
  }

  // If no custom replies loaded, use defaults
  if (s_reply_count == 0) {
    strncpy(s_replies[0], "Call Me", REPLY_TEXT_LEN);
    strncpy(s_replies[1], "Yes", REPLY_TEXT_LEN);
    strncpy(s_replies[2], "No", REPLY_TEXT_LEN);
    strncpy(s_replies[3], "Okay", REPLY_TEXT_LEN);
    s_reply_count = 4;
  }
}

static int total_rows(void) {
  return HAS_VOICE_ROW + s_reply_count;
}

static const char *get_reply_text(int row) {
  int index = row - HAS_VOICE_ROW;
  if (index < 0) return "";
  if (index < s_reply_count) return s_replies[index];
  return "";
}

// --- Voice dictation ---
#ifdef PBL_MICROPHONE
static void dictation_callback(DictationSession *session, DictationSessionStatus status,
                                char *transcription, void *context) {
  if (status == DictationSessionStatusSuccess && transcription) {
    send_reply(s_chat_id, transcription);
    vibes_short_pulse();
    window_stack_pop(true);
  }
}

static void start_voice_reply(void) {
  if (s_dictation_session) dictation_session_destroy(s_dictation_session);
  s_dictation_session = dictation_session_create(sizeof(s_voice_buffer), dictation_callback, NULL);
  if (s_dictation_session) {
    dictation_session_enable_confirmation(s_dictation_session, true);
    dictation_session_enable_error_dialogs(s_dictation_session, true);
    dictation_session_start(s_dictation_session);
  }
}
#endif

// --- Menu callbacks ---
static uint16_t menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return total_rows();
}

static int16_t menu_get_header_height(MenuLayer *menu_layer, uint16_t section_index, void *data) {
#ifdef PBL_ROUND
  return 0; // no header on round — use space for replies
#else
  return 24;
#endif
}

static void menu_draw_header(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
#ifndef PBL_ROUND
  GRect bounds = layer_get_bounds(cell_layer);
#ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);
#endif
  char header[48];
  snprintf(header, sizeof(header), "Reply to %s", s_chat_name);
  graphics_draw_text(ctx, header,
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
    GRect(8, 1, bounds.size.w - 16, 20),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
#endif
}

static int16_t menu_get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
#ifdef PBL_ROUND
  if (menu_layer_is_index_selected(menu_layer, cell_index)) {
#ifdef PBL_MICROPHONE
    if (cell_index->row == 0) return 52;
#endif
    return 48;
  }
  return 36;
#else
#ifdef PBL_MICROPHONE
  if (cell_index->row == 0) return 44;
#endif
  return 36;
#endif
}

static void menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  GRect bounds = layer_get_bounds(cell_layer);
  bool is_selected = menu_layer_is_index_selected(s_menu_layer, cell_index);

#ifdef PBL_MICROPHONE
  if (cell_index->row == 0) {
    // Voice reply row
#ifdef PBL_COLOR
    if (is_selected) {
      graphics_context_set_fill_color(ctx, GColorVividCerulean);
      graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    }
    graphics_context_set_text_color(ctx, GColorWhite);
#endif
    graphics_draw_text(ctx, "Voice Reply",
      fonts_get_system_font(is_selected ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD),
      GRect(0, is_selected ? 8 : 6, bounds.size.w, 30),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    return;
  }
#endif

  const char *text = get_reply_text(cell_index->row);

#ifdef PBL_ROUND
  // Round: centered text, larger when selected
#ifdef PBL_COLOR
  if (is_selected) {
    graphics_context_set_fill_color(ctx, GColorDukeBlue);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);
  } else {
    graphics_context_set_text_color(ctx, GColorLightGray);
  }
#endif
  graphics_draw_text(ctx, text,
    fonts_get_system_font(is_selected ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18),
    GRect(20, is_selected ? 8 : 6, bounds.size.w - 40, 30),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

#else
  // Rectangular: left-aligned
#ifdef PBL_COLOR
  if (is_selected) {
    graphics_context_set_fill_color(ctx, GColorDukeBlue);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);
  }
#endif
  graphics_draw_text(ctx, text,
    fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
    GRect(10, 2, bounds.size.w - 20, 30),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
#endif
}

static void menu_select_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
#ifdef PBL_MICROPHONE
  if (cell_index->row == 0) { start_voice_reply(); return; }
#endif
  const char *text = get_reply_text(cell_index->row);
  send_reply(s_chat_id, text);
  vibes_short_pulse();
  window_stack_pop(true);
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

#ifdef PBL_COLOR
  window_set_background_color(window, GColorBlack);
#endif

  s_menu_layer = menu_layer_create(bounds);

#ifdef PBL_ROUND
  menu_layer_set_center_focused(s_menu_layer, true);
#endif

#ifdef PBL_COLOR
  menu_layer_set_normal_colors(s_menu_layer, GColorBlack, GColorWhite);
  menu_layer_set_highlight_colors(s_menu_layer, GColorDukeBlue, GColorWhite);
#endif

  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_rows = menu_get_num_rows,
    .get_cell_height = menu_get_cell_height,
    .get_header_height = menu_get_header_height,
    .draw_header = menu_draw_header,
    .draw_row = menu_draw_row,
    .select_click = menu_select_click,
  });

  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
#ifdef PBL_MICROPHONE
  if (s_dictation_session) {
    dictation_session_destroy(s_dictation_session);
    s_dictation_session = NULL;
  }
#endif
}

void reply_menu_show(const char *chat_id, const char *chat_name) {
  strncpy(s_chat_id, chat_id, sizeof(s_chat_id) - 1);
  s_chat_id[sizeof(s_chat_id) - 1] = '\0';
  strncpy(s_chat_name, chat_name, sizeof(s_chat_name) - 1);
  s_chat_name[sizeof(s_chat_name) - 1] = '\0';

  load_replies();

  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers) {
      .load = window_load,
      .unload = window_unload,
    });
  }

  window_stack_push(s_window, true);
}
