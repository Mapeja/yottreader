#include "battery.h"
#include <Arduino.h>

// LilyGo T5 V2.3.1: GPIO35, 1:1 divider (100K/100K), ADC_11db attenuation.
// Returns 0-100 for LiPo %, BATTERY_USB (-2) when no battery detected (voltage
// outside LiPo range — typical when running from USB only), -1 if unavailable.
#define BAT_PIN     35
#define BAT_SAMPLES 8
#define VBAT_MIN    3.0f
#define VBAT_MAX    4.2f
#define VBAT_LOW    2.7f  // below this → no battery connected (USB power)

void battery_init() {
  pinMode(BAT_PIN, INPUT);
  analogSetPinAttenuation(BAT_PIN, ADC_11db);
}

int battery_percent() {
  uint32_t sum = 0;
  for (int i = 0; i < BAT_SAMPLES; i++) sum += analogRead(BAT_PIN);
  float raw = (float)(sum / BAT_SAMPLES);
  float v   = (raw / 4095.0f) * 3.3f * 2.0f; // ×2 for voltage divider

  if (v < VBAT_LOW) return BATTERY_USB;  // pin floating low → USB only, no battery

  int pct = (int)((v - VBAT_MIN) / (VBAT_MAX - VBAT_MIN) * 100.0f);
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}
