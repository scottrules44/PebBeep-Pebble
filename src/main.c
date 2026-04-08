#include <pebble.h>
#include "chat_list.h"
#include "notification.h"
#include "welcome.h"

// AppMessage keys — must match package.json messageKeys order (0-indexed)
#define KEY_COMMAND      0
#define KEY_CHAT_ID      1
#define KEY_CHAT_NAME    2
#define KEY_CHAT_PREVIEW 3
#define KEY_MSG_TEXT     4
#define KEY_MSG_SENDER   5
#define KEY_MSG_TIME     6
#define KEY_REPLY_TEXT   7
#define KEY_STATUS       8
#define KEY_INDEX        9
#define KEY_TOTAL        10

// Settings keys (from config page)
#define KEY_CUSTOM_REPLY_1       11
#define KEY_CUSTOM_REPLY_2       12
#define KEY_CUSTOM_REPLY_3       13
#define KEY_CUSTOM_REPLY_4       14
#define KEY_VIBRATE_ON_MESSAGE   15
#define KEY_SHOW_NOTIFICATION    16
#define KEY_POLL_INTERVAL        17
#define KEY_SERVICE              18

// Persistent storage keys for settings
#define PERSIST_CUSTOM_REPLY_COUNT 100
#define PERSIST_CUSTOM_REPLY_BASE  101  // 101-104
#define PERSIST_VIBRATE            110
#define PERSIST_SHOW_NOTIFICATION  111

// Global settings (loaded from persistent storage)
static bool s_vibrate_on_message = true;
static bool s_show_notification = true;

// Forward declarations
void send_command(const char *command);
void send_reply(const char *chat_id, const char *text);
void request_messages(const char *chat_id);

// AppMessage buffer sizes
#ifdef PBL_PLATFORM_APLITE
  #define INBOX_SIZE  1024
  #define OUTBOX_SIZE 256
#else
  #define INBOX_SIZE  2048
  #define OUTBOX_SIZE 512
#endif

// Load settings from persistent storage
static void load_settings(void) {
  if (persist_exists(PERSIST_VIBRATE)) {
    s_vibrate_on_message = persist_read_bool(PERSIST_VIBRATE);
  }
  if (persist_exists(PERSIST_SHOW_NOTIFICATION)) {
    s_show_notification = persist_read_bool(PERSIST_SHOW_NOTIFICATION);
  }
  APP_LOG(APP_LOG_LEVEL_INFO, "Settings loaded: vibrate=%d, notifications=%d",
    s_vibrate_on_message, s_show_notification);
}

// Save a custom reply to persistent storage
static void save_custom_reply(int index, const char *text) {
  if (index < 0 || index > 3) return;
  persist_write_string(PERSIST_CUSTOM_REPLY_BASE + index, text);
  APP_LOG(APP_LOG_LEVEL_INFO, "Saved custom reply %d: %s", index, text);

  // Update count
  int count = 0;
  for (int i = 0; i < 4; i++) {
    if (persist_exists(PERSIST_CUSTOM_REPLY_BASE + i)) {
      char buf[40];
      persist_read_string(PERSIST_CUSTOM_REPLY_BASE + i, buf, sizeof(buf));
      if (buf[0] != '\0') count = i + 1;
    }
  }
  persist_write_int(PERSIST_CUSTOM_REPLY_COUNT, count);
}

// Handle settings from config page (no KEY_COMMAND — settings keys come directly)
static bool handle_settings(DictionaryIterator *iter) {
  bool handled = false;

  Tuple *reply1 = dict_find(iter, KEY_CUSTOM_REPLY_1);
  if (reply1) {
    save_custom_reply(0, reply1->value->cstring);
    handled = true;
  }

  Tuple *reply2 = dict_find(iter, KEY_CUSTOM_REPLY_2);
  if (reply2) {
    save_custom_reply(1, reply2->value->cstring);
    handled = true;
  }

  Tuple *reply3 = dict_find(iter, KEY_CUSTOM_REPLY_3);
  if (reply3) {
    save_custom_reply(2, reply3->value->cstring);
    handled = true;
  }

  Tuple *reply4 = dict_find(iter, KEY_CUSTOM_REPLY_4);
  if (reply4) {
    save_custom_reply(3, reply4->value->cstring);
    handled = true;
  }

  Tuple *vibrate = dict_find(iter, KEY_VIBRATE_ON_MESSAGE);
  if (vibrate) {
    s_vibrate_on_message = (vibrate->value->int32 != 0);
    persist_write_bool(PERSIST_VIBRATE, s_vibrate_on_message);
    APP_LOG(APP_LOG_LEVEL_INFO, "Vibrate set to: %d", s_vibrate_on_message);
    handled = true;
  }

  Tuple *show_notif = dict_find(iter, KEY_SHOW_NOTIFICATION);
  if (show_notif) {
    s_show_notification = (show_notif->value->int32 != 0);
    persist_write_bool(PERSIST_SHOW_NOTIFICATION, s_show_notification);
    APP_LOG(APP_LOG_LEVEL_INFO, "Show notification set to: %d", s_show_notification);
    handled = true;
  }

  Tuple *poll = dict_find(iter, KEY_POLL_INTERVAL);
  if (poll) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Poll interval set to: %d seconds", (int)poll->value->int32);
    handled = true;
  }

  if (handled) {
    vibes_short_pulse();
    APP_LOG(APP_LOG_LEVEL_INFO, "Settings saved to watch — auto-connecting");

    // Auto-launch chat list and fetch chats after settings are saved
    chat_list_init();
    chat_list_show();
    send_command("fetch_chats");
  }

  return handled;
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  // First check if this is a settings update (no KEY_COMMAND)
  Tuple *command_tuple = dict_find(iter, KEY_COMMAND);
  if (!command_tuple) {
    // No command — might be settings from config page
    if (handle_settings(iter)) {
      return;
    }
    APP_LOG(APP_LOG_LEVEL_WARNING, "Received message with no command and no settings");
    return;
  }

  const char *command = command_tuple->value->cstring;
  APP_LOG(APP_LOG_LEVEL_INFO, "Received command: %s", command);

  if (strcmp(command, "chat_item") == 0) {
    Tuple *index_tuple = dict_find(iter, KEY_INDEX);
    Tuple *id_tuple = dict_find(iter, KEY_CHAT_ID);
    Tuple *name_tuple = dict_find(iter, KEY_CHAT_NAME);
    Tuple *preview_tuple = dict_find(iter, KEY_CHAT_PREVIEW);
    Tuple *total_tuple = dict_find(iter, KEY_TOTAL);

    if (index_tuple && id_tuple && name_tuple) {
      int index = index_tuple->value->int32;
      int total = total_tuple ? total_tuple->value->int32 : 0;
      APP_LOG(APP_LOG_LEVEL_INFO, "Chat %d/%d: %s", index, total, name_tuple->value->cstring);
      chat_list_add_item(
        index, total,
        id_tuple->value->cstring,
        name_tuple->value->cstring,
        preview_tuple ? preview_tuple->value->cstring : ""
      );
    }
  } else if (strcmp(command, "new_message") == 0) {
    Tuple *sender_tuple = dict_find(iter, KEY_MSG_SENDER);
    Tuple *text_tuple = dict_find(iter, KEY_MSG_TEXT);
    Tuple *time_tuple = dict_find(iter, KEY_MSG_TIME);
    Tuple *chat_name_tuple = dict_find(iter, KEY_CHAT_NAME);
    Tuple *index_tuple = dict_find(iter, KEY_INDEX);
    Tuple *service_tuple = dict_find(iter, KEY_SERVICE);

    const char *sender = sender_tuple ? sender_tuple->value->cstring : "Unknown";
    const char *text = text_tuple ? text_tuple->value->cstring : "";
    const char *time_str = time_tuple ? time_tuple->value->cstring : "";
    const char *chat_name = chat_name_tuple ? chat_name_tuple->value->cstring : "";
    const char *service = service_tuple ? service_tuple->value->cstring : "";
    int index = index_tuple ? index_tuple->value->int32 : -1;

    APP_LOG(APP_LOG_LEVEL_INFO, "New message from %s [%s]: %s", sender, service, text);

    // Skip encrypted/undecryptable messages entirely
    if (strcmp(text, "Encrypted") == 0 || strcmp(sender, "Unknown") == 0) {
      APP_LOG(APP_LOG_LEVEL_INFO, "Skipping encrypted/unknown message");
      return;
    }

    // Get short service label for display
    const char *svc_label = "";
    if (strlen(service) > 0) {
      if (strcmp(service, "imessage") == 0) svc_label = "iMsg";
      else if (strcmp(service, "slack") == 0) svc_label = "Slack";
      else if (strcmp(service, "signal") == 0) svc_label = "Signal";
      else if (strcmp(service, "whatsapp") == 0) svc_label = "WA";
      else if (strcmp(service, "telegram") == 0) svc_label = "TG";
      else if (strcmp(service, "instagram") == 0) svc_label = "IG";
      else if (strcmp(service, "messenger") == 0) svc_label = "FB";
      else if (strcmp(service, "discord") == 0) svc_label = "Discord";
      else if (strcmp(service, "linkedin") == 0) svc_label = "LinkedIn";
      else if (strcmp(service, "twitter") == 0) svc_label = "X";
      else if (strcmp(service, "googlechat") == 0) svc_label = "GChat";
      else if (strcmp(service, "gmessages") == 0) svc_label = "GMsg";
      else if (strcmp(service, "matrix") == 0) svc_label = "Matrix";
      else svc_label = service;
    }

    // Build chat name with service label
    char display_name[CHAT_NAME_LEN];
    if (strlen(svc_label) > 0) {
      snprintf(display_name, sizeof(display_name), "[%s] %s", svc_label, chat_name);
    } else {
      snprintf(display_name, sizeof(display_name), "%s", chat_name);
    }

    // Build preview for chat list
    char preview[CHAT_PREVIEW_LEN];
    snprintf(preview, sizeof(preview), "%s: %s", sender, text);

    // Always add/update chat list with service label
    chat_list_upsert(chat_name, display_name, preview);

    // Only show notification for OTHER people's messages
    bool is_own_message = (strcmp(sender, "Me") == 0);
    if (!is_own_message && index == -1) {
      // index == -1 means it's a new incoming message (not a history fetch)
      if (s_show_notification) {
        show_notification(sender, text, chat_name);
      } else if (s_vibrate_on_message) {
        vibes_short_pulse();
      }
    }

    message_view_add_message(index, sender, text, time_str);

  } else if (strcmp(command, "status") == 0) {
    Tuple *status_tuple = dict_find(iter, KEY_STATUS);
    if (status_tuple) {
      const char *status = status_tuple->value->cstring;
      APP_LOG(APP_LOG_LEVEL_INFO, "Status: %s", status);
      if (strcmp(status, "sent") == 0) {
        vibes_short_pulse();
      } else if (strcmp(status, "no_chats") == 0) {
        APP_LOG(APP_LOG_LEVEL_INFO, "No chats available (preserving any persisted/in-memory chats)");
      }
    }
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Unknown command: %s", command);
  }
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped: %d", (int)reason);
}

static void outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed: %d", (int)reason);
}

static void outbox_sent_handler(DictionaryIterator *iter, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send succeeded");
}

// Send a command to JS companion
void send_command(const char *command) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Sending command: %s", command);
  DictionaryIterator *out;
  AppMessageResult result = app_message_outbox_begin(&out);
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to begin outbox: %d", (int)result);
    return;
  }
  dict_write_cstring(out, KEY_COMMAND, command);
  app_message_outbox_send();
}

// Send a reply to a specific chat
void send_reply(const char *chat_id, const char *text) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Sending reply to %s: %s", chat_id, text);

  // Add sent message to message view immediately (optimistic)
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char time_str[12];
  snprintf(time_str, sizeof(time_str), "%d:%02d", t->tm_hour, t->tm_min);
  message_view_add_message(-1, "Me", text, time_str);

  // Update chat list preview (preserve existing display name if known)
  char preview[CHAT_PREVIEW_LEN];
  snprintf(preview, sizeof(preview), "Me: %s", text);
  APP_LOG(APP_LOG_LEVEL_INFO, "send_reply: upserting chat_id='%s' preview='%s'", chat_id, preview);
  chat_list_upsert(chat_id, chat_id, preview);

  DictionaryIterator *out;
  AppMessageResult result = app_message_outbox_begin(&out);
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to begin outbox for reply: %d", (int)result);
    return;
  }
  dict_write_cstring(out, KEY_COMMAND, "send_reply");
  dict_write_cstring(out, KEY_CHAT_ID, chat_id);
  dict_write_cstring(out, KEY_REPLY_TEXT, text);
  app_message_outbox_send();
}

// Request messages for a specific chat
void request_messages(const char *chat_id) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Requesting messages for chat: %s", chat_id);
  DictionaryIterator *out;
  AppMessageResult result = app_message_outbox_begin(&out);
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to begin outbox for messages: %d", (int)result);
    return;
  }
  dict_write_cstring(out, KEY_COMMAND, "fetch_messages");
  dict_write_cstring(out, KEY_CHAT_ID, chat_id);
  app_message_outbox_send();
}

static void init(void) {
  APP_LOG(APP_LOG_LEVEL_INFO, "PebBeep starting...");

  // Load saved settings
  load_settings();

  // Check if launched from timeline pin action
  bool launched_by_timeline = (launch_reason() == APP_LAUNCH_TIMELINE_ACTION);
  if (launched_by_timeline) {
    uint32_t launch_arg = launch_get_args();
    APP_LOG(APP_LOG_LEVEL_INFO, "Launched by timeline pin (arg=%d)", (int)launch_arg);
  }

  // Open AppMessage
  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_register_outbox_failed(outbox_failed_handler);
  app_message_register_outbox_sent(outbox_sent_handler);
  app_message_open(INBOX_SIZE, OUTBOX_SIZE);

  APP_LOG(APP_LOG_LEVEL_INFO, "AppMessage opened (inbox=%d, outbox=%d)", INBOX_SIZE, OUTBOX_SIZE);

  // Route: timeline pin or configured → chat list, otherwise welcome
  if (launched_by_timeline || persist_exists(PERSIST_VIBRATE)) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Going to chat list");
    chat_list_init();
    chat_list_show();
    send_command("fetch_chats");
  } else {
    APP_LOG(APP_LOG_LEVEL_INFO, "First run — showing welcome screen");
    welcome_show();
  }
}

static void deinit(void) {
  APP_LOG(APP_LOG_LEVEL_INFO, "PebBeep shutting down");
  welcome_deinit();
  chat_list_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
