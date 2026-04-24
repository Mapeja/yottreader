#pragma once
#include "button.h"

enum AppState { APP_READING, APP_MENU, APP_SETTINGS, APP_WIFI };

void     sm_init(AppState initial);
AppState sm_state();
void     sm_handle(ButtonEvent evt);
