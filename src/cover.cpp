#include "cover.h"
#include "theme.h"
#include "fonts.h"
#include <GxEPD2_BW.h>
#include <LittleFS.h>
#include <string.h>
#include <stdio.h>

extern GxEPD2_BW<GxEPD2_213_GDEY0213B74, GxEPD2_213_GDEY0213B74::HEIGHT> display;

static void makeCoverPath(const char* bookPath, char* out, size_t len, bool large) {
  strncpy(out, bookPath, len - 12);
  out[len - 12] = 0;
  char* ext = strrchr(out, '.');
  if (ext && strcmp(ext, ".book") == 0) *ext = 0;
  strncat(out, large ? ".cvr_lg" : ".cvr_sm", len - strlen(out) - 1);
}

bool cover_save(const char* bookPath, const uint8_t* data, size_t len, bool large) {
  char path[80];
  makeCoverPath(bookPath, path, sizeof(path), large);
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  size_t written = f.write(data, len);
  f.close();
  return written == len;
}

bool cover_load(const char* bookPath, uint8_t* buf, size_t len, bool large) {
  char path[80];
  makeCoverPath(bookPath, path, sizeof(path), large);
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  size_t n = f.read(buf, len);
  f.close();
  return n == len;
}

bool cover_exists(const char* bookPath, bool large) {
  char path[80];
  makeCoverPath(bookPath, path, sizeof(path), large);
  return LittleFS.exists(path);
}

void cover_delete(const char* bookPath) {
  char path[80];
  makeCoverPath(bookPath, path, sizeof(path), true);
  LittleFS.remove(path);
  makeCoverPath(bookPath, path, sizeof(path), false);
  LittleFS.remove(path);
}

void cover_draw(int x, int y, int w, int h, const uint8_t* data) {
  display.drawBitmap(x, y, data, w, h, theme_fg(), theme_bg());
}

static uint8_t ph_glyphW(char c) {
  const GFXfont* f = UI_FONT_S;
  uint8_t u = (uint8_t)c;
  if (u < f->first || u > f->last) return 0;
  return f->glyph[u - f->first].xAdvance;
}

void cover_draw_placeholder(int x, int y, int w, int h, const char* title) {
  display.drawRect(x, y, w, h, theme_fg());
  display.drawRect(x + 1, y + 1, w - 2, h - 2, theme_fg());

  display.setFont(UI_FONT_S);
  display.setTextColor(theme_fg());

  int lh   = UI_FONT_S->yAdvance;
  int tx   = x + 4;
  int maxW = w - 8;
  int ty   = y + lh + 2;
  int maxY = y + h - 4;

  const char* p = title;
  while (*p && ty <= maxY) {
    int n = 0, lineW = 0, lastBreak = 0;
    while (p[n]) {
      int cw = ph_glyphW(p[n]);
      if (lineW + cw > maxW && n > 0) break;
      lineW += cw;
      n++;
      if (p[n] == ' ' || p[n] == 0) lastBreak = n;
    }
    if (!p[n]) lastBreak = n;
    if (lastBreak == 0) lastBreak = n;

    char buf[52];
    int take = lastBreak < 51 ? lastBreak : 51;
    strncpy(buf, p, take);
    buf[take] = 0;
    while (take > 0 && buf[take - 1] == ' ') buf[--take] = 0;

    display.setCursor(tx, ty);
    display.print(buf);
    ty += lh;
    p += lastBreak;
    while (*p == ' ') p++;
  }
}
