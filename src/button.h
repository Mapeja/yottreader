#pragma once
#include <Arduino.h>

enum ButtonEvent {
  BTN_NONE,
  BTN_SINGLE,
  BTN_DOUBLE,
  BTN_TRIPLE,
  BTN_LONG,
  BTN_CLICK_HOLD,
};

void        button_init();
ButtonEvent button_update();
bool        button_can_sleep();
