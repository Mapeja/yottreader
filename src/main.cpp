#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <LittleFS.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <string.h>
#include "fonts.h"
#include "button.h"
#include "reader.h"
#include "library.h"
#include "storage.h"
#include "battery.h"
#include "menu.h"
#include "settings.h"
#include "cover.h"
#include "state_machine.h"
#include "theme.h"

// Set to 1 temporarily for on-device debugging.
#define SLEEP_DEBUG 0

#define EPD_CS   5
#define EPD_DC   17
#define EPD_RST  16
#define EPD_BUSY 4

GxEPD2_BW<GxEPD2_213_GDEY0213B74, GxEPD2_213_GDEY0213B74::HEIGHT>
  display(GxEPD2_213_GDEY0213B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

static uint32_t gLastActivityMs = 0;

static uint8_t glyphW(const GFXfont* f, char c) {
  uint8_t u = (uint8_t)c;
  if (u < f->first || u > f->last) return 0;
  return f->glyph[u - f->first].xAdvance;
}

static int textW(const GFXfont* f, const char* s, int n) {
  int w = 0;
  for (int i = 0; i < n; i++) w += glyphW(f, s[i]);
  return w;
}

static void printSpan(const char* s, int n) {
  char buf[48];
  int i = 0;
  while (i < n) {
    int c = n - i;
    if (c > 47) c = 47;
    memcpy(buf, s + i, c);
    buf[c] = 0;
    display.print(buf);
    i += c;
  }
}

static int drawWrappedText(const String& text, const GFXfont* f, int x, int y, int maxW, int yMax) {
  display.setFont(f);
  const char* s = text.c_str();
  int len = text.length();
  int i = 0;
  int cx = x;
  int lh = f->yAdvance;

  while (i < len && y <= yMax) {
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i >= len) break;

    if (s[i] == '\n') {
      cx = x;
      y += lh;
      i++;
      continue;
    }

    int ws = i;
    while (i < len && s[i] != ' ' && s[i] != '\t' && s[i] != '\n') i++;
    int wl = i - ws;
    int ww = textW(f, s + ws, wl);

    if (cx > x && cx + ww > x + maxW) {
      cx = x;
      y += lh;
      if (y > yMax) break;
    }

    if (ww <= (x + maxW - cx)) {
      display.setCursor(cx, y);
      printSpan(s + ws, wl);
      cx += ww + glyphW(f, ' ');
    } else {
      int consumed = 0;
      while (consumed < wl && y <= yMax) {
        int avail = x + maxW - cx;
        int take = 0;
        int w = 0;
        while (consumed + take < wl) {
          int cw = glyphW(f, s[ws + consumed + take]);
          if (take > 0 && w + cw > avail) break;
          w += cw;
          take++;
          if (w >= avail) break;
        }
        if (take <= 0) {
          cx = x;
          y += lh;
          continue;
        }
        display.setCursor(cx, y);
        printSpan(s + ws + consumed, take);
        consumed += take;
        if (consumed < wl) {
          cx = x;
          y += lh;
        } else {
          cx += w + glyphW(f, ' ');
        }
      }
    }
  }

  return y + lh;
}

static void loadBookMeta(const char* path, String& title, String& author) {
  title = "";
  author = "";

  File f = LittleFS.open(path, "r");
  if (f) {
    bool inTitle = false, inAuthor = false;
    while (f.available() && (title.length() == 0 || author.length() == 0)) {
      String line = f.readStringUntil('\n');
      if (line.endsWith("\r")) line.remove(line.length() - 1);

      if (line == "::TITLE::")  { inTitle = true;  inAuthor = false; continue; }
      if (line == "::AUTHOR::") { inAuthor = true; inTitle = false;  continue; }
      if (line.startsWith("::")) { inTitle = false; inAuthor = false; continue; }

      if (inTitle && title.length() == 0 && line.length() > 0) title = line;
      if (inAuthor && author.length() == 0 && line.length() > 0) author = line;
    }
    f.close();
  }

  if (title.length() == 0) {
    String p = path;
    int slash = p.lastIndexOf('/');
    if (slash >= 0) p = p.substring(slash + 1);
    int dot = p.lastIndexOf('.');
    if (dot > 0) p = p.substring(0, dot);
    title = p;
  }
  if (author.length() == 0) author = "Unknown author";
}

static void drawDeepSleepWallpaper(const char* path, int progressPct) {
  bool hasBook = (path && path[0]);

  if (settings_get_wallpaper_mode() == 1 && hasBook) {
    int rot = settings_get_orientation() ? 2 : 0;
    display.setRotation(rot);
    display.setFullWindow();

    static uint8_t coverBuf[COVER_LG_BYTES];
    bool hasCover = cover_load(path, coverBuf, COVER_LG_BYTES, true);

    String title = "", author = "";
    loadBookMeta(path, title, author);

    int lh      = UI_FONT_S->yAdvance;
    int sw      = display.width();
    int maxTW   = sw - 4;
    int footerH = display.height() - COVER_LG_H - 6;

    // Word-wrap title fully; clip only if title exhausts all available lines
    int maxTitleLines = (footerH - lh - 2) / lh;
    if (maxTitleLines < 1) maxTitleLines = 1;
    if (maxTitleLines > 6) maxTitleLines = 6;

    String tlines[6];
    int numTlines = 0;
    {
      String rem = title;
      while (rem.length() && numTlines < maxTitleLines) {
        while (rem.length() && rem[0] == ' ') rem = rem.substring(1);
        if (!rem.length()) break;
        bool fits = textW(UI_FONT_S, rem.c_str(), rem.length()) <= maxTW;
        bool isLast = (numTlines == maxTitleLines - 1);
        if (fits) {
          tlines[numTlines++] = rem;
          rem = "";
          break;
        }
        if (isLast) {
          while (rem.length() > 0 &&
                 textW(UI_FONT_S, (rem + "...").c_str(), rem.length() + 3) > maxTW)
            rem.remove(rem.length() - 1);
          tlines[numTlines++] = rem + "...";
          break;
        }
        // find last word boundary that fits
        const char* s = rem.c_str();
        int len = rem.length(), lastFit = 0, i = 0;
        while (i < len) {
          while (i < len && s[i] == ' ') i++;
          while (i < len && s[i] != ' ') i++;
          if (textW(UI_FONT_S, s, i) <= maxTW) lastFit = i;
          else break;
        }
        if (lastFit == 0) {
          while (rem.length() > 0 &&
                 textW(UI_FONT_S, (rem + "...").c_str(), rem.length() + 3) > maxTW)
            rem.remove(rem.length() - 1);
          tlines[numTlines++] = rem + "...";
          break;
        }
        tlines[numTlines++] = rem.substring(0, lastFit);
        rem = rem.substring(lastFit);
      }
    }

    // Show author only if space remains after prompt + title
    bool showAuthor = author.length() > 0 &&
                      (lh + 2 + numTlines * lh + lh) <= footerH;
    if (showAuthor && textW(UI_FONT_S, author.c_str(), author.length()) > maxTW) {
      while (author.length() > 0 &&
             textW(UI_FONT_S, (author + "...").c_str(), author.length() + 3) > maxTW)
        author.remove(author.length() - 1);
      author += "...";
    }

    display.firstPage();
    do {
      display.fillScreen(theme_bg());
      display.setTextColor(theme_fg());

      if (hasCover) cover_draw(0, 0, COVER_LG_W, COVER_LG_H, coverBuf);
      else          cover_draw_placeholder(0, 0, COVER_LG_W, COVER_LG_H, title.c_str());

      display.setFont(UI_FONT_S);
      auto cx = [&](const char* s, int n) { return (sw - textW(UI_FONT_S, s, n)) / 2; };
      int by = COVER_LG_H + 10;

      const char* prompt = "Press to Resume";
      display.setCursor(cx(prompt, strlen(prompt)), by);
      display.print(prompt);
      by += lh + 2;

      for (int i = 0; i < numTlines; i++) {
        const char* tl = tlines[i].c_str();
        display.setCursor(cx(tl, strlen(tl)), by);
        display.print(tl);
        by += lh;
      }

      if (showAuthor) {
        const char* au = author.c_str();
        display.setCursor(cx(au, strlen(au)), by);
        display.print(au);
      }
    } while (display.nextPage());
    return;
  }

  // Text wallpaper (landscape)
  String title, author;
  if (hasBook) loadBookMeta(path, title, author);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(theme_bg());
    display.setTextColor(theme_fg());

    int x = 4;
    int w = display.width() - 8;
    int y = 11;

    display.setFont(UI_FONT_S);
    display.setCursor(x, y);
    display.print("sleep mode");

    y += UI_FONT_S->yAdvance + 2;

    if (hasBook) {
      y = drawWrappedText(title, UI_FONT, x, y, w, display.height() - 28);
      y += 1;
      display.setFont(UI_FONT_S);
      display.setCursor(x, y);
      display.print("by ");
      y = drawWrappedText(author, UI_FONT_S, x + textW(UI_FONT_S, "by ", 3), y, w - textW(UI_FONT_S, "by ", 3), display.height() - 28);
    } else {
      display.setFont(UI_FONT);
      display.setCursor(x, y);
      display.print("No active book");
      y += UI_FONT->yAdvance;
    }

    char pbuf[28];
    if (progressPct >= 0) snprintf(pbuf, sizeof(pbuf), "progress %d%%", progressPct);
    else                  snprintf(pbuf, sizeof(pbuf), "progress --%%");

    int lh = UI_FONT_S->yAdvance;
    int yBottom = display.height() - lh - 1;
    int yAbove  = yBottom - lh - 1;
    display.setFont(UI_FONT_S);
    display.setCursor(x, yAbove);
    display.print(pbuf);
    display.setCursor(x, yBottom);
    display.print("press button to resume");
  } while (display.nextPage());
}

static bool sleepBlockedByWifiMode() {
  return sm_state() == APP_WIFI;
}

static void saveReadingIfActive() {
  if (sm_state() != APP_READING) return;
  const char* path = reader_path();
  if (!path || !path[0]) return;
  int pageCount = reader_page_count();
  if (pageCount <= 0) return;
  storage_save_position(path, reader_current_page(), pageCount);
}

[[noreturn]] static void enterDeepSleep() {
  saveReadingIfActive();

  char savedPath[64] = {0};
  int  ignoredPage = 0;
  bool hasPos = storage_load_position(savedPath, sizeof(savedPath), &ignoredPage);
  int progressPct = hasPos ? storage_get_book_progress(savedPath) : -1;

  drawDeepSleepWallpaper(hasPos ? savedPath : "", progressPct);
  delay(80);

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL); // clear light-sleep timer
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_39, 0); // wake on button press (active low)
  esp_deep_sleep_start();
  for (;;) {}
}

static void enterLightSleepIfIdle(ButtonEvent evt) {
  if (evt != BTN_NONE) return;
  if (!button_can_sleep()) {
#if SLEEP_DEBUG
    static uint32_t nextLog = 0;
    uint32_t now = millis();
    if (now >= nextLog) {
      Serial.println("[SLEEP] skip light: button state active");
      nextLog = now + 500;
    }
#endif
    return;
  }
  if (digitalRead(39) == LOW) return;

#if SLEEP_DEBUG
  Serial.printf("[SLEEP] enter light @%lu\n", (unsigned long)millis());
#endif
  // Do NOT use gpio_wakeup_enable here: it overwrites GPIO.pin[n].int_type,
  // which clobbers the CHANGE interrupt set by attachInterrupt, causing the
  // button release edge to be missed and a continuous ISR flood while held.
  // The existing CHANGE interrupt already wakes the CPU from light sleep on
  // ESP32; the 10ms timer is a fallback in case no edge arrives.
  esp_sleep_enable_timer_wakeup(10000ULL);
  esp_light_sleep_start();
#if SLEEP_DEBUG
  Serial.printf("[SLEEP] wake light @%lu pin=%d\n", (unsigned long)millis(), digitalRead(39));
#endif
}

static void maybeHandleSleep(ButtonEvent evt) {
  if (sleepBlockedByWifiMode()) {
    if (evt != BTN_NONE) gLastActivityMs = millis();
    return; // no sleep while WiFi is active
  }

  uint32_t now = millis();
  if (evt != BTN_NONE) gLastActivityMs = now;

  bool deepAllowed = (settings_get_sleep_mode() == SLEEP_LIGHT_AND_DEEP);
  if (deepAllowed && (uint32_t)(now - gLastActivityMs) >= settings_get_deep_sleep_timeout_ms()) {
#if SLEEP_DEBUG
    Serial.printf("[SLEEP] enter deep @%lu idle=%lu\n",
      (unsigned long)now, (unsigned long)(now - gLastActivityMs));
#endif
    enterDeepSleep();
  }

  enterLightSleepIfIdle(evt);
}

static void showError(const char* msg) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(theme_bg());
    display.setFont(UI_FONT);
    display.setTextColor(theme_fg());
    display.setCursor(4, 20);
    display.print(msg);
  } while (display.nextPage());
}

static void showWakeupSplash() {
  const char* title = "Yottreader";
  const char* sub   = "resuming...";

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(theme_bg());
    display.setTextColor(theme_fg());

    int mid = display.height() / 2;

    display.setFont(UI_FONT);
    int tw = textW(UI_FONT, title, (int)strlen(title));
    display.setCursor((display.width() - tw) / 2, mid);
    display.print(title);

    display.setFont(UI_FONT_S);
    int sw = textW(UI_FONT_S, sub, (int)strlen(sub));
    display.setCursor((display.width() - sw) / 2, mid + UI_FONT_S->yAdvance + 2);
    display.print(sub);
  } while (display.nextPage());
}

void setup() {
  Serial.begin(115200);

  bool fromDeepSleep = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0);

  button_init();
  battery_init();
  display.init(115200);
  display.setRotation(1);

  if (!LittleFS.begin(true)) {
    showError("LittleFS failed");
    return;
  }

  settings_load();
  library_scan();
  Serial.printf("%d book(s) found\n", library_count());

  if (fromDeepSleep) {
    showWakeupSplash();
    delay(600);
  }

  char savedPath[64];
  int  savedPage = 0;
  bool hasPos = storage_load_position(savedPath, sizeof(savedPath), &savedPage);

  if (hasPos && reader_open(savedPath)) {
    reader_goto(savedPage);
    reader_draw(true);
    sm_init(APP_READING);
    Serial.printf("Resumed: %s page %d\n", savedPath, savedPage);
  } else {
    sm_init(APP_MENU);
    menu_open();
  }

  gLastActivityMs = millis();
}

void loop() {
  ButtonEvent evt = button_update();
#if SLEEP_DEBUG
  if (evt != BTN_NONE) {
    const char* n = "NONE";
    if (evt == BTN_SINGLE) n = "SINGLE";
    else if (evt == BTN_DOUBLE) n = "DOUBLE";
    else if (evt == BTN_TRIPLE) n = "TRIPLE";
    else if (evt == BTN_LONG) n = "LONG";
    else if (evt == BTN_CLICK_HOLD) n = "CLICK_HOLD";
    Serial.printf("[MAIN] evt=%s @%lu\n", n, (unsigned long)millis());
  }
#endif
  sm_handle(evt);
  maybeHandleSleep(evt);
}
