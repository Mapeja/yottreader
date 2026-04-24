#pragma once
#include "button.h"

void        menu_open();
// Returns: nullptr = stay, "" = go to settings, "/path" = open this book
const char* menu_handle(ButtonEvent evt);
