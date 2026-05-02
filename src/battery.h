#pragma once
#include <Arduino.h>

#define BATTERY_USB (-2)  // returned when no battery detected (USB-only power)

void battery_init();
int  battery_percent();     // 0-100, BATTERY_USB, or -1 if unavailable
bool battery_is_charging(); // true when USB connected and battery charging
