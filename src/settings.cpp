#include "settings.h"
#include "reader.h"
#include "fonts.h"
#include "theme.h"
#include <GxEPD2_BW.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

extern GxEPD2_BW<GxEPD2_213_GDEY0213B74, GxEPD2_213_GDEY0213B74::HEIGHT> display;

static const int REFRESH_OPTIONS[] = { 5, 10, 20, 50 };
static const int REFRESH_COUNT = sizeof(REFRESH_OPTIONS) / sizeof(REFRESH_OPTIONS[0]);
static const int HEADER_H = 16;
static const int HEADER_GAP = 2;

enum SettingId {
  SET_FONT_SIZE = 0,
  SET_FONT_FAMILY,
  SET_HYPHENATION,
  SET_DISPLAY_MODE,
  SET_ORIENTATION,
  SET_REFRESH,
  SET_STATS,
  SET_SLEEP_MODE,
  SET_WIFI_UPLOAD,
  SET_SHORTCUTS,
  SET_COUNT
};

enum SettingKind { SK_CHOICE = 0, SK_ACTION = 1 };
enum UiMode { UI_SETTINGS = 0, UI_SHORTCUTS = 1 };

struct Setting {
  const char* label;
  SettingKind kind;
  int         value;
  int         minVal;
  int         maxVal;
};

static Setting cfg[SET_COUNT] = {
  { "Font size",   SK_CHOICE, FONT_SIZE_MEDIUM, 0, 2 },
  { "Font",        SK_CHOICE, FONT_FAMILY_SANS, 0, 3 },
  { "Hyphenation", SK_CHOICE, 0,                0, 1 },
  { "Display",     SK_CHOICE, 0,                0, 1 }, // 0 light, 1 dark (placeholder)
  { "Orientation", SK_CHOICE, 0,                0, 1 }, // 0 normal, 1 flipped
  { "Refresh",     SK_CHOICE, 10,               0, 0 }, // fixed options
  { "Stats bar",   SK_CHOICE, 0,                0, 2 }, // 0 off, 1 chapter, 2 book
  { "Deep sleep",  SK_CHOICE, 0,                0, 5 }, // 0 never, 1-5 = 2/5/10/15/30 min
  { "WiFi upload", SK_ACTION, 0,                0, 0 },
  { "Shortcuts",   SK_ACTION, 0,                0, 0 },
};

struct ShortcutLine {
  const char* left;
  const char* right;
  bool        heading;
};

static const ShortcutLine SHORTCUT_LINES[] = {
  { "READING",   "",                  true  },
  { "single press",    "next page",         false },
  { "double press",    "previous page",     false },
  { "triple press",    "refresh screen",    false },
  { "long press",      "go to library",           false },
  { "press+hold",  "go to settings",          false },
  { "LIBRARY",   "",                  true  },
  { "single press",    "next book",         false },
  { "double press",    "previous book",     false },
  { "triple press",    "refresh screen",    false },
  { "long press",      "open book",              false },
  { "press+hold",  "go to settings",          false },
  { "SETTINGS",  "",                  true  },
  { "single press",    "next item",              false },
  { "double press",    "previous item",          false },
  { "triple press",    "refresh screen",    false },
  { "long press",      "edit/apply",        false },
  { "press+hold",  "go to library",           false },
};
static const int SHORTCUT_COUNT = sizeof(SHORTCUT_LINES) / sizeof(SHORTCUT_LINES[0]);

static int     cursor = 0;
static int     listScroll = 0;
static bool    editing = false;
static UiMode  uiMode = UI_SETTINGS;
static int     shortcutsScroll = 0; // pixels
static uint32_t infoUntilMs = 0;
static const char* infoText = nullptr;

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
  if (textW(f, src) <= maxW) {
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

static void drawHeader(const char* title, const char* legend) {
  display.setFont(UI_FONT_S);
  display.setTextColor(theme_fg());

  int titleX = 3;
  int baseY = 11;
  display.setCursor(titleX, baseY);
  display.print(title);

  int titleW = textW(UI_FONT_S, title);
  int hintMaxW = display.width() - 6 - titleW - 8;
  if (hintMaxW > 0) {
    char hbuf[52];
    clipEllipsis(UI_FONT_S, legend, hbuf, sizeof(hbuf), hintMaxW);
    int hw = textW(UI_FONT_S, hbuf);
    int hx = display.width() - 3 - hw;
    if (hx < titleX + titleW + 8) hx = titleX + titleW + 8;
    display.setCursor(hx, baseY);
    display.print(hbuf);
  }
}

static int normalizeRefresh(int v) {
  for (int i = 0; i < REFRESH_COUNT; i++) if (REFRESH_OPTIONS[i] == v) return v;
  return 10;
}

static int refreshIdx(int v) {
  for (int i = 0; i < REFRESH_COUNT; i++) if (REFRESH_OPTIONS[i] == v) return i;
  return 1;
}

static int refreshNext(int v) {
  int idx = (refreshIdx(v) + 1) % REFRESH_COUNT;
  return REFRESH_OPTIONS[idx];
}

static int refreshPrev(int v) {
  int idx = (refreshIdx(v) - 1 + REFRESH_COUNT) % REFRESH_COUNT;
  return REFRESH_OPTIONS[idx];
}

static int settingsY0() {
  int lh = UI_FONT->yAdvance;
  return HEADER_H + HEADER_GAP + lh - 2;
}

static int settingsVisibleRows() {
  int lh = UI_FONT->yAdvance;
  return (display.height() - settingsY0()) / lh + 1;
}

static void ensureCursorVisible() {
  int vis = settingsVisibleRows();
  if (cursor < listScroll) listScroll = cursor;
  if (cursor >= listScroll + vis) listScroll = cursor - vis + 1;
  if (listScroll < 0) listScroll = 0;
}

static const char* valueStr(int id) {
  static char buf[18];
  switch (id) {
    case SET_FONT_SIZE:
      if (cfg[id].value == FONT_SIZE_SMALL) return "small";
      if (cfg[id].value == FONT_SIZE_LARGE) return "large";
      return "medium";
    case SET_FONT_FAMILY:
      switch (cfg[id].value) {
        case FONT_FAMILY_SANS_BOLD:  return "sans bold";
        case FONT_FAMILY_SERIF:      return "serif";
        case FONT_FAMILY_SERIF_BOLD: return "serif bold";
        default:                     return "sans";
      }
    case SET_HYPHENATION:
      return cfg[id].value ? "on" : "off";
    case SET_DISPLAY_MODE:
      return cfg[id].value ? "dark" : "light";
    case SET_ORIENTATION:
      return cfg[id].value ? "flipped" : "normal";
    case SET_REFRESH:
      snprintf(buf, sizeof(buf), "%d pg", cfg[id].value);
      return buf;
    case SET_STATS:
      if (cfg[id].value == 1) return "chapter";
      if (cfg[id].value == 2) return "book";
      return "off";
    case SET_SLEEP_MODE:
      switch (cfg[id].value) {
        case 1: return "2 min";
        case 2: return "5 min";
        case 3: return "10 min";
        case 4: return "15 min";
        case 5: return "30 min";
        default: return "never";
      }
    case SET_WIFI_UPLOAD:
      return "activate";
    case SET_SHORTCUTS:
      return "view";
  }
  return "";
}

static void applyAll() {
  theme_set_dark(cfg[SET_DISPLAY_MODE].value != 0);
  reader_set_font(cfg[SET_FONT_FAMILY].value, cfg[SET_FONT_SIZE].value);
  reader_set_hyphenation(cfg[SET_HYPHENATION].value != 0);
  reader_set_refresh_interval(cfg[SET_REFRESH].value);
  reader_set_stats_mode(cfg[SET_STATS].value);
  display.setRotation(cfg[SET_ORIENTATION].value ? 3 : 1);
}

static void saveAll() {
  Preferences prefs;
  prefs.begin("yott-cfg", false);
  prefs.putInt("font_size", cfg[SET_FONT_SIZE].value);
  prefs.putInt("font_fam",  cfg[SET_FONT_FAMILY].value);
  prefs.putInt("hyphen",    cfg[SET_HYPHENATION].value);
  prefs.putInt("display",   cfg[SET_DISPLAY_MODE].value);
  prefs.putInt("orient",    cfg[SET_ORIENTATION].value);
  prefs.putInt("refresh_n", cfg[SET_REFRESH].value);
  prefs.putInt("stats_mode",cfg[SET_STATS].value);
  prefs.putInt("sleep",     cfg[SET_SLEEP_MODE].value);
  prefs.end();
}

static void clampAll() {
  cfg[SET_REFRESH].value = normalizeRefresh(cfg[SET_REFRESH].value);
  for (int i = 0; i < SET_COUNT; i++) {
    if (cfg[i].kind != SK_CHOICE || i == SET_REFRESH) continue;
    if (cfg[i].value < cfg[i].minVal) cfg[i].value = cfg[i].minVal;
    if (cfg[i].value > cfg[i].maxVal) cfg[i].value = cfg[i].maxVal;
  }
}

void settings_load() {
  Preferences prefs;
  prefs.begin("yott-cfg", true);
  cfg[SET_FONT_SIZE].value   = prefs.getInt("font_size", FONT_SIZE_MEDIUM);
  cfg[SET_FONT_FAMILY].value = prefs.getInt("font_fam",  FONT_FAMILY_SANS);
  cfg[SET_HYPHENATION].value = prefs.getInt("hyphen",    0);
  cfg[SET_DISPLAY_MODE].value = prefs.getInt("display",  0);
  cfg[SET_ORIENTATION].value = prefs.getInt("orient",    0);
  cfg[SET_REFRESH].value     = prefs.getInt("refresh_n",  10);
  cfg[SET_STATS].value       = prefs.getInt("stats_mode", 0);
  cfg[SET_SLEEP_MODE].value  = prefs.getInt("sleep",      0);
  prefs.end();
  clampAll();
  applyAll();
}

int settings_get_refresh_interval() { return cfg[SET_REFRESH].value; }
int settings_get_sleep_mode() {
  return cfg[SET_SLEEP_MODE].value > 0 ? SLEEP_LIGHT_AND_DEEP : SLEEP_LIGHT_ONLY;
}
uint32_t settings_get_deep_sleep_timeout_ms() {
  static const uint32_t TABLE[] = { 0, 2*60000UL, 5*60000UL, 10*60000UL, 15*60000UL, 30*60000UL };
  int v = cfg[SET_SLEEP_MODE].value;
  if (v < 1 || v > 5) v = 3; // fallback to 10 min
  return TABLE[v];
}

WebSettings settings_get_web() {
  WebSettings ws;
  ws.fontSize    = cfg[SET_FONT_SIZE].value;
  ws.fontFamily  = cfg[SET_FONT_FAMILY].value;
  ws.hyphenation = cfg[SET_HYPHENATION].value;
  ws.displayMode = cfg[SET_DISPLAY_MODE].value;
  ws.orientation = cfg[SET_ORIENTATION].value;
  ws.refresh     = cfg[SET_REFRESH].value;
  ws.stats       = cfg[SET_STATS].value;
  ws.sleep       = cfg[SET_SLEEP_MODE].value;
  return ws;
}

void settings_apply_web(const WebSettings& s) {
  cfg[SET_FONT_SIZE].value    = s.fontSize;
  cfg[SET_FONT_FAMILY].value  = s.fontFamily;
  cfg[SET_HYPHENATION].value  = s.hyphenation;
  cfg[SET_DISPLAY_MODE].value = s.displayMode;
  cfg[SET_ORIENTATION].value  = s.orientation;
  cfg[SET_REFRESH].value      = s.refresh;
  cfg[SET_STATS].value        = s.stats;
  cfg[SET_SLEEP_MODE].value   = s.sleep;
  clampAll();
  applyAll();
  saveAll();
}

static int shortcutsTotalHeight() {
  int h = 0;
  for (int i = 0; i < SHORTCUT_COUNT; i++) h += SHORTCUT_LINES[i].heading ? 11 : 9;
  return h;
}

static void drawShortcuts() {
  int contentY = HEADER_H + HEADER_GAP;
  int contentH = display.height() - contentY;
  int totalH = shortcutsTotalHeight();
  int maxScroll = totalH - contentH;
  if (maxScroll < 0) maxScroll = 0;
  if (shortcutsScroll < 0) shortcutsScroll = 0;
  if (shortcutsScroll > maxScroll) shortcutsScroll = maxScroll;

  drawHeader("SHORTCUTS", "tap:down dbl:up hold:exit");

  int y = contentY - shortcutsScroll;
  for (int i = 0; i < SHORTCUT_COUNT; i++) {
    if (SHORTCUT_LINES[i].heading) {
      if (y + 11 >= contentY && y <= display.height()) {
        display.setFont(UI_FONT);
        display.setTextColor(theme_fg());
        display.setCursor(4, y + 9);
        display.print(SHORTCUT_LINES[i].left);
      }
      y += 11;
    } else {
      if (y + 9 >= contentY && y <= display.height()) {
        display.setFont(UI_FONT_S);
        display.setTextColor(theme_fg());
        display.setCursor(4, y + 7);
        display.print(SHORTCUT_LINES[i].left);

        int rw = textW(UI_FONT_S, SHORTCUT_LINES[i].right);
        int rx = display.width() - 5 - rw;
        if (rx < 85) rx = 85;
        display.setCursor(rx, y + 7);
        display.print(SHORTCUT_LINES[i].right);
      }
      y += 9;
    }
  }

  if (maxScroll > 0) {
    int barW = 2;
    int barH = (contentH * contentH) / totalH;
    if (barH < 8) barH = 8;
    int barY = contentY + (shortcutsScroll * (contentH - barH)) / maxScroll;
    display.fillRect(display.width() - barW, barY, barW, barH, theme_fg());
  }
}

static void drawSettings() {
  int lh  = UI_FONT->yAdvance;
  int y0  = settingsY0();
  int vis = settingsVisibleRows();

  const char* legend = nullptr;
  if (infoText && millis() < infoUntilMs) {
    legend = infoText;
  } else {
    infoText = nullptr;
    legend = editing ? "tap:+ dbl:- hold:apply" : "tap:next hold:edit T+H:library";
  }

  drawHeader("SETTINGS", legend);
  display.setFont(UI_FONT);
  display.setTextColor(theme_fg());

  for (int i = 0; i < vis; i++) {
    int idx = listScroll + i;
    if (idx >= SET_COUNT) break;
    display.setCursor(4, y0 + i * lh);
    bool sel = (idx == cursor);
    display.print(sel && editing ? "* " : (sel ? "> " : "  "));
    display.print(cfg[idx].label);
    display.print(": ");
    if (editing && sel && cfg[idx].kind == SK_CHOICE) {
      display.print("<");
      display.print(valueStr(idx));
      display.print(">");
    } else {
      display.print(valueStr(idx));
    }
  }
}

static void draw() {
  display.firstPage();
  do {
    display.fillScreen(theme_bg());
    if (uiMode == UI_SHORTCUTS) drawShortcuts();
    else drawSettings();
  } while (display.nextPage());
}

static void showInfo(const char* text) {
  infoText = text;
  infoUntilMs = millis() + 1300;
}

void settings_open() {
  cursor = 0;
  listScroll = 0;
  editing = false;
  uiMode = UI_SETTINGS;
  shortcutsScroll = 0;
  infoText = nullptr;
  display.setFullWindow();
  draw();
}

SettingsResult settings_handle(ButtonEvent evt) {
  if (evt == BTN_NONE) return SETTINGS_STAY;

  if (uiMode == UI_SHORTCUTS) {
    int maxScroll = shortcutsTotalHeight() - (display.height() - (HEADER_H + HEADER_GAP));
    if (maxScroll < 0) maxScroll = 0;

    bool redraw = false;
    if (evt == BTN_SINGLE) {
      shortcutsScroll += 18;
      if (shortcutsScroll > maxScroll) shortcutsScroll = maxScroll;
      redraw = true;
    } else if (evt == BTN_DOUBLE) {
      shortcutsScroll -= 18;
      if (shortcutsScroll < 0) shortcutsScroll = 0;
      redraw = true;
    } else if (evt == BTN_LONG) {
      uiMode = UI_SETTINGS;
      redraw = true;
    }
    // BTN_CLICK_HOLD intentionally no-op on shortcuts screen.

    if (redraw) {
      display.setPartialWindow(0, 0, display.width(), display.height());
      draw();
    }
    return SETTINGS_STAY;
  }

  if (evt == BTN_CLICK_HOLD) {
    editing = false;
    return SETTINGS_GO_LIBRARY;
  }

  bool redraw = false;

  if (editing) {
    if (evt == BTN_SINGLE) {
      if (cfg[cursor].kind == SK_CHOICE) {
        if (cursor == SET_REFRESH) {
          cfg[SET_REFRESH].value = refreshNext(cfg[SET_REFRESH].value);
        } else {
          cfg[cursor].value++;
          if (cfg[cursor].value > cfg[cursor].maxVal) cfg[cursor].value = cfg[cursor].minVal;
        }
        redraw = true;
      }
    } else if (evt == BTN_DOUBLE) {
      if (cfg[cursor].kind == SK_CHOICE) {
        if (cursor == SET_REFRESH) {
          cfg[SET_REFRESH].value = refreshPrev(cfg[SET_REFRESH].value);
        } else {
          cfg[cursor].value--;
          if (cfg[cursor].value < cfg[cursor].minVal) cfg[cursor].value = cfg[cursor].maxVal;
        }
        redraw = true;
      }
    } else if (evt == BTN_LONG) {
      applyAll();
      saveAll();
      editing = false;
      redraw = true;
    }
  } else {
    if (evt == BTN_SINGLE) {
      cursor = (cursor + 1) % SET_COUNT;
      ensureCursorVisible();
      redraw = true;
    } else if (evt == BTN_DOUBLE) {
      cursor = (cursor - 1 + SET_COUNT) % SET_COUNT;
      ensureCursorVisible();
      redraw = true;
    } else if (evt == BTN_LONG) {
      if (cfg[cursor].kind == SK_ACTION) {
        if (cursor == SET_SHORTCUTS) {
          uiMode = UI_SHORTCUTS;
          shortcutsScroll = 0;
        } else if (cursor == SET_WIFI_UPLOAD) {
          return SETTINGS_GO_WIFI;
        }
      } else {
        editing = true;
      }
      redraw = true;
    }
  }

  if (redraw) {
    display.setPartialWindow(0, 0, display.width(), display.height());
    draw();
  }

  return SETTINGS_STAY;
}
