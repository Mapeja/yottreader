#pragma once
#include <Arduino.h>

#define COVER_LG_W     128
#define COVER_LG_H     192
#define COVER_LG_BYTES (((COVER_LG_W + 7) / 8) * COVER_LG_H)   // 16 * 192 = 3072

#define COVER_SM_W     56
#define COVER_SM_H     84
#define COVER_SM_BYTES (((COVER_SM_W + 7) / 8) * COVER_SM_H)    // 7 * 84 = 588

bool cover_save(const char* bookPath, const uint8_t* data, size_t len, bool large);
bool cover_load(const char* bookPath, uint8_t* buf, size_t len, bool large);
bool cover_exists(const char* bookPath, bool large);
void cover_delete(const char* bookPath);
void cover_draw(int x, int y, int w, int h, const uint8_t* data);
void cover_draw_placeholder(int x, int y, int w, int h, const char* title);
