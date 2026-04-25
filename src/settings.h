#pragma once
#include "button.h"

enum SettingsResult { SETTINGS_STAY, SETTINGS_GO_LIBRARY, SETTINGS_GO_WIFI };
enum SleepMode { SLEEP_LIGHT_ONLY = 0, SLEEP_LIGHT_AND_DEEP = 1 };

struct WebSettings {
  int fontSize;
  int fontFamily;
  int hyphenation;
  int displayMode;
  int orientation;
  int refresh;
  int stats;
  int sleep;
};

void           settings_load();
void           settings_open();
SettingsResult settings_handle(ButtonEvent evt);
int            settings_get_refresh_interval();
int            settings_get_sleep_mode();
uint32_t       settings_get_deep_sleep_timeout_ms();
WebSettings    settings_get_web();
void           settings_apply_web(const WebSettings& s);
