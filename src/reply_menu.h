#pragma once
#include <pebble.h>

#define MAX_REPLIES 10
#define REPLY_TEXT_LEN 40

void reply_menu_show(const char *chat_id, const char *chat_name);
