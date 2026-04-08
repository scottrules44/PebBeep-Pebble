#pragma once
#include <pebble.h>

// Max chats displayed — reduced on aplite for memory
#ifdef PBL_PLATFORM_APLITE
  #define MAX_CHATS 5
#else
  #define MAX_CHATS 10
#endif

#define CHAT_NAME_LEN 24
#define CHAT_PREVIEW_LEN 40
#define CHAT_ID_LEN 64

void chat_list_init(void);
void chat_list_deinit(void);
void chat_list_show(void);
void chat_list_add_item(int index, int total, const char *id, const char *name, const char *preview);
void chat_list_upsert(const char *id, const char *name, const char *preview);

// Forward declarations for message view integration
void message_view_add_message(int index, const char *sender, const char *text, const char *time_str);
