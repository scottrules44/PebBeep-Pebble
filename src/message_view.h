#pragma once
#include <pebble.h>

#ifdef PBL_PLATFORM_APLITE
  #define MAX_MESSAGES 3
#else
  #define MAX_MESSAGES 5
#endif

#define MSG_SENDER_LEN 20
#define MSG_TEXT_LEN 200
#define MSG_TIME_LEN 12

void message_view_show(const char *chat_id, const char *chat_name);
void message_view_add_message(int index, const char *sender, const char *text, const char *time_str);
