#include "battery.h"
#include <Arduino.h>
#include <Wire.h>

// LilyGo T5 V2.3.1: GPIO35, 1:1 divider (100K/100K), ADC_11db attenuation.
// Returns 0-100 for LiPo %, BATTERY_USB (-2) when no battery detected (voltage
// outside LiPo range — typical when running from USB only), -1 if unavailable.
#define BAT_PIN     35
#define BAT_SAMPLES 8
#define VBAT_MIN    3.0f
#define VBAT_MAX    4.2f
#define VBAT_LOW    2.7f  // below this → no battery connected (USB power)

// IP5306 power management IC (I2C 0x75, SDA=21, SCL=22)
// Register 0x70 bit 3: 1 = charging
#define IP5306_ADDR 0x75
#define IP5306_REG  0x70

static bool ip5306_read(uint8_t reg, uint8_t* out) {
  Wire.beginTransmission(IP5306_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((uint8_t)IP5306_ADDR, (uint8_t)1);
  if (!Wire.available()) return false;
  *out = Wire.read();
  return true;
}

void battery_init() {
  pinMode(BAT_PIN, INPUT);
  analogSetPinAttenuation(BAT_PIN, ADC_11db);
  Wire.begin(21, 22);

  // I2C scan — log all devices found on the bus
  Serial.println("[BAT] I2C scan:");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0)
      Serial.printf("[BAT]   device at 0x%02X\n", addr);
  }

  // Dump registers 0x70-0x7A so we can identify the right one
  Serial.println("[BAT] IP5306 regs 0x70-0x7A:");
  for (uint8_t r = 0x70; r <= 0x7A; r++) {
    uint8_t val = 0xFF;
    bool ok = ip5306_read(r, &val);
    Serial.printf("[BAT]   reg 0x%02X = 0x%02X (%s)\n", r, val, ok ? "ok" : "fail");
  }
}

bool battery_is_charging() {
  uint8_t val;
  if (!ip5306_read(IP5306_REG, &val)) {
    Serial.println("[BAT] charging read failed");
    return false;
  }
  Serial.printf("[BAT] reg 0x%02X = 0x%02X  charging=%d\n", IP5306_REG, val, (val & 0x08) != 0);
  return (val & 0x08) != 0;
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
