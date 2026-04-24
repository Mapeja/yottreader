#include "menu.h"
#include "library.h"
#include "fonts.h"
#include "storage.h"
#include "theme.h"
#include "battery.h"
#include <GxEPD2_BW.h>
#include <string.h>
#include <stdio.h>

extern GxEPD2_BW<GxEPD2_213_GDEY0213B74, GxEPD2_213_GDEY0213B74::HEIGHT> display;

static int menuCursor = 0;
static int menuScroll = 0;

static const int ITEM_H = 30;
static const int HEADER_H = 16;
static const int HEADER_GAP = 2;

static uint8_t glyphW(const GFXfont* f, char c) {
  uint8_t u = (uint8_t)c;
  if (u < f->first || u > f->last) return 0;
  return f->glyph[u - f->first].xAdvance;
}

static int textW(const GFXfont* f, const char* s) {
  int w = 0;
  while (*s) w += glyphW(f, *s++);
  return w;
}

static void clipEllipsis(const GFXfont* f, const char* src, char* out, size_t outLen, int maxW) {
  if (outLen == 0) return;
  out[0] = 0;
  int fullW = textW(f, src);
  if (fullW <= maxW) {
    strncpy(out, src, outLen - 1);
    out[outLen - 1] = 0;
    return;
  }

  const char* ell = "...";
  int ellW = textW(f, ell);
  int w = 0;
  size_t n = 0;
  while (src[n] && n + 4 < outLen) {
    int cw = glyphW(f, src[n]);
    if (w + cw + ellW > maxW) break;
    out[n] = src[n];
    w += cw;
    n++;
  }
  out[n++] = '.';
  out[n++] = '.';
  out[n++] = '.';
  out[n] = 0;
}

static void drawHeader() {
  const char* title = "LIBRARY";
  const char* hint  = "tap:next hold:open T+H:settings";

  display.setFont(UI_FONT_S);
  display.setTextColor(theme_fg());

  int titleX = 3;
  int baseY = 11;
  display.setCursor(titleX, baseY);
  display.print(title);

  int titleW = textW(UI_FONT_S, title);
  int hintMaxW = display.width() - 6 - titleW - 8;
  if (hintMaxW > 0) {
    char hbuf[48];
    clipEllipsis(UI_FONT_S, hint, hbuf, sizeof(hbuf), hintMaxW);
    int hw = textW(UI_FONT_S, hbuf);
    int hx = display.width() - 3 - hw;
    if (hx < titleX + titleW + 8) hx = titleX + titleW + 8;
    display.setCursor(hx, baseY);
    display.print(hbuf);
  }

}

static void menuDraw(bool full) {
  int n   = library_count();
  int y0  = HEADER_H + HEADER_GAP;
  int vis = (display.height() - y0) / ITEM_H;

  if (full) display.setFullWindow();
  else      display.setPartialWindow(0, 0, display.width(), display.height());

  display.firstPage();
  do {
    display.fillScreen(theme_bg());
    drawHeader();

    if (n == 0) {
      display.setFont(UI_FONT);
      display.setTextColor(theme_fg());
      display.setCursor(4, y0);
      display.print("No books found");
      continue;
    }

    for (int i = 0; i < vis; i++) {
      int idx = menuScroll + i;
      if (idx >= n) break;
      const BookEntry* b = library_get(idx);
      if (!b) continue;

      int rowY = y0 + i * ITEM_H;
      bool sel = (idx == menuCursor);
      if (sel) display.fillRect(0, rowY, display.width(), ITEM_H, theme_fg());

      int progress = storage_get_book_progress(b->path);
      char pbuf[8] = {0};
      bool showProgress = progress >= 0;
      if (showProgress) {
        if (progress == 0) snprintf(pbuf, sizeof(pbuf), "<1%%");
        else               snprintf(pbuf, sizeof(pbuf), "%d%%", progress);
      }

      int xTitle = 4;
      int xRight = display.width() - 4;
      int xProg = xRight;
      if (showProgress) {
        int pw = textW(UI_FONT, pbuf);
        xProg = xRight - pw;
      }

      int titleMaxW = showProgress ? (xProg - 8 - xTitle) : (xRight - xTitle);
      if (titleMaxW < 20) titleMaxW = 20;

      char title[LIBRARY_STR_LEN + 4];
      clipEllipsis(UI_FONT, b->title, title, sizeof(title), titleMaxW);

      int authorMaxW = xRight - xTitle;
      char author[LIBRARY_STR_LEN + 4];
      clipEllipsis(UI_FONT_S, b->author, author, sizeof(author), authorMaxW);

      display.setFont(UI_FONT);
      display.setTextColor(sel ? theme_bg() : theme_fg());
      display.setCursor(xTitle, rowY + 12);
      display.print(title);

      if (showProgress) {
        display.setCursor(xProg, rowY + 12);
        display.print(pbuf);
      }

      display.setFont(UI_FONT_S);
      display.setTextColor(sel ? theme_bg() : theme_fg());
      display.setCursor(xTitle, rowY + 24);
      display.print(author[0] ? author : "-");
    }

    // footer: counter left, battery right
    display.setFont(UI_FONT_S);
    display.setTextColor(theme_fg());
    int footerY = display.height() - 3;

    char cbuf[12];
    snprintf(cbuf, sizeof(cbuf), "%d / %d", menuCursor + 1, n);
    display.setCursor(4, footerY);
    display.print(cbuf);

    int batt = battery_percent();
    char btxt[16];
    if (batt == BATTERY_USB)   snprintf(btxt, sizeof(btxt), "USB");
    else if (batt >= 0)        snprintf(btxt, sizeof(btxt), "Bat: %d%%", batt);
    else                       snprintf(btxt, sizeof(btxt), "Bat: --");
    int bw = textW(UI_FONT_S, btxt);
    display.setCursor(display.width() - 4 - bw, footerY);
    display.print(btxt);
  } while (display.nextPage());
}

void menu_open() {
  menuCursor = 0;
  menuScroll = 0;
  library_resort();
  menuDraw(true);
}

const char* menu_handle(ButtonEvent evt) {
  int n   = library_count();
  int vis = (display.height() - (HEADER_H + HEADER_GAP)) / ITEM_H;

  if (evt == BTN_CLICK_HOLD) return "";  // caller enters settings

  if (n == 0) return nullptr;

  if (evt == BTN_SINGLE) {
    menuCursor = (menuCursor + 1) % n;
    if (menuCursor < menuScroll)        menuScroll = menuCursor;
    if (menuCursor >= menuScroll + vis) menuScroll = menuCursor - vis + 1;
    if (menuScroll < 0)                 menuScroll = 0;
    menuDraw(false);
    return nullptr;
  }

  if (evt == BTN_DOUBLE) {
    menuCursor = (menuCursor - 1 + n) % n;
    if (menuCursor < menuScroll)        menuScroll = menuCursor;
    if (menuCursor >= menuScroll + vis) menuScroll = menuCursor - vis + 1;
    menuDraw(false);
    return nullptr;
  }

  if (evt == BTN_LONG) {
    const BookEntry* b = library_get(menuCursor);
    return b ? b->path : nullptr;
  }

  return nullptr;
}
