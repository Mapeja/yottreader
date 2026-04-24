#include "reader.h"
#include "fonts.h"
#include "battery.h"
#include "hyphenation.h"
#include "theme.h"
#include <LittleFS.h>
#include <GxEPD2_BW.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

extern GxEPD2_BW<GxEPD2_213_GDEY0213B74, GxEPD2_213_GDEY0213B74::HEIGHT> display;

// -----------------------------------------------------------------------
// Layout and font
// -----------------------------------------------------------------------

static int fontFamily  = FONT_FAMILY_SANS;
static int fontSizeIdx = FONT_SIZE_MEDIUM;
static int statsMode   = READER_STATS_OFF;
static bool hyphenationEnabled = false;
static bool prefsLoaded = false;

static const GFXfont* rf() { return getReadingFont(fontFamily, fontSizeIdx); }

static const int16_t X0            = 4;
static const int16_t MARGIN_RIGHT  = 4;
static const int16_t MARGIN_BOTTOM = 4;

static int16_t stats_zone_h() {
  if (statsMode == READER_STATS_OFF) return 0;
  return 9;
}

static int16_t XMAX() { return display.width() - MARGIN_RIGHT; }
static int16_t YMAX() {
  if (statsMode == READER_STATS_OFF) return display.height() - MARGIN_BOTTOM;
  return display.height() - stats_zone_h();
}
static int16_t LH() { return rf()->yAdvance; }
static int16_t FY() { return LH(); }

static uint8_t gwf(const GFXfont* f, char c) {
  uint8_t u = (uint8_t)c;
  if (u < f->first || u > f->last) return 0;
  return f->glyph[u - f->first].xAdvance;
}

static uint8_t gw(char c) { return gwf(rf(), c); }

static uint16_t swf(const GFXfont* f, const char* s, int n) {
  uint16_t w = 0;
  for (int i = 0; i < n; i++) w += gwf(f, s[i]);
  return w;
}

static uint16_t sw(const char* s, int n) { return swf(rf(), s, n); }

static uint16_t uiw(const char* s) {
  return swf(UI_FONT_S, s, strlen(s));
}

static int choose_hyphen_split(const char* s, int wl, int maxW) {
  if (!hyphenationEnabled || wl < 4 || maxW <= 0) return 0;
  int hy = gw('-');
  if (hy <= 0) return 0;

  uint8_t splits[64];
  int n = hyphenation_collect_splits(s, wl, splits, 64, 2, 2);
  for (int i = n - 1; i >= 0; i--) {
    int split = splits[i];
    if ((int)sw(s, split) + hy <= maxW) return split;
  }
  return 0;
}

static int hard_split(const char* s, int wl, int maxW) {
  if (wl <= 1 || maxW <= 0) return 0;
  int hy = gw('-');
  int best = 0;
  for (int i = 1; i < wl; i++) {
    int need = (int)sw(s, i) + hy;
    if (need <= maxW) best = i;
    else break;
  }
  return best;
}

static void print_span(const char* s, int n) {
  char buf[32];
  int i = 0;
  while (i < n) {
    int c = n - i;
    if (c > 31) c = 31;
    memcpy(buf, s + i, c);
    buf[c] = 0;
    display.print(buf);
    i += c;
  }
}

// -----------------------------------------------------------------------
// Page and chapter index
// -----------------------------------------------------------------------

#define MAX_PAGES    12288
#define MAX_CHAPTERS 256

static uint32_t* pages     = nullptr; // heap-allocated so WiFi stack has room when not reading
static int       nPages    = 0;
static int      curPage    = 0;
static int      turnsSince = 0;
static char     fpath[64];
static int      chapterStarts[MAX_CHAPTERS];
static int      nChapters = 0;

static void load_prefs_once() {
  if (prefsLoaded) return;
  Preferences prefs;
  prefs.begin("yott-cfg", true);
  statsMode = prefs.getInt("stats_mode", READER_STATS_OFF);
  hyphenationEnabled = (prefs.getInt("hyphen", 0) != 0);
  prefs.end();
  if (statsMode < READER_STATS_OFF || statsMode > READER_STATS_BOOK) {
    statsMode = READER_STATS_OFF;
  }
  prefsLoaded = true;
}

// -----------------------------------------------------------------------
// Page index caching
// -----------------------------------------------------------------------

static void indexPath(char* out, size_t len) {
  snprintf(out, len, "%s.idx", fpath);
}

// Packs current layout settings into a 32-bit key; index is invalid if this changes.
static uint32_t settingsSig() {
  return ((uint32_t)(uint8_t)fontFamily  << 24)
       | ((uint32_t)(uint8_t)fontSizeIdx << 16)
       | ((uint32_t)(uint8_t)statsMode   <<  8)
       | ((uint32_t)(hyphenationEnabled ? 1u : 0u));
}

static bool tryLoadIndex() {
  char ip[72];
  indexPath(ip, sizeof(ip));
  File f = LittleFS.open(ip, "r");
  if (!f) return false;

  // Header: 4 magic + 4 sig + 2 nPages + 2 nChapters = 12 bytes
  uint8_t hdr[12];
  if (f.read(hdr, 12) != 12)                              { f.close(); return false; }
  if (hdr[0]!='Y'||hdr[1]!='I'||hdr[2]!='D'||hdr[3]!='X') { f.close(); return false; }

  uint32_t sig; memcpy(&sig, hdr + 4, 4);
  if (sig != settingsSig())                               { f.close(); return false; }

  uint16_t np, nc;
  memcpy(&np, hdr + 8, 2);
  memcpy(&nc, hdr + 10, 2);
  if (np > MAX_PAGES || nc > MAX_CHAPTERS)                { f.close(); return false; }

  if ((int)f.read((uint8_t*)pages, np * 4) != np * 4)    { f.close(); return false; }

  for (int i = 0; i < (int)nc; i++) {
    uint16_t v;
    if (f.read((uint8_t*)&v, 2) != 2) { f.close(); return false; }
    chapterStarts[i] = (int)v;
  }

  f.close();
  nPages    = (int)np;
  nChapters = (int)nc;
  return true;
}

static void saveIndex() {
  if (nPages <= 0) return;
  char ip[72];
  indexPath(ip, sizeof(ip));
  File f = LittleFS.open(ip, "w");
  if (!f) return;

  uint8_t hdr[12];
  hdr[0]='Y'; hdr[1]='I'; hdr[2]='D'; hdr[3]='X';
  uint32_t sig = settingsSig(); memcpy(hdr + 4, &sig, 4);
  uint16_t np = (uint16_t)nPages, nc = (uint16_t)nChapters;
  memcpy(hdr + 8,  &np, 2);
  memcpy(hdr + 10, &nc, 2);
  f.write(hdr, 12);
  f.write((uint8_t*)pages, nPages * 4);
  for (int i = 0; i < nChapters; i++) {
    uint16_t v = (uint16_t)chapterStarts[i];
    f.write((uint8_t*)&v, 2);
  }
  f.close();
}

static void showLoadingScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(theme_bg());
    display.setTextColor(theme_fg());
    display.setFont(UI_FONT);
    display.setCursor(4, 20);
    display.print("Opening...");
    display.setFont(UI_FONT_S);
    display.setCursor(4, 34);
    display.print("Building page index");
  } while (display.nextPage());
}

// -----------------------------------------------------------------------
// Index builder — reads entire file, simulates layout, fills pages[].
// -----------------------------------------------------------------------

static void buildIndex() {
  File f = LittleFS.open(fpath, "r");
  if (!f) return;
  nPages = 0;
  nChapters = 0;

  int16_t lh   = LH();
  int16_t fy   = FY();
  int16_t xmax = XMAX();
  int16_t ymax = YMAX();
  int16_t cx = X0, cy = fy;
  bool inText = false;
  bool needPage = true;
  bool pendingChapter = false;

  while (f.available()) {
    uint32_t lpos = f.position();
    String   line = f.readStringUntil('\n');
    if (line.endsWith("\r")) line.remove(line.length() - 1);

    if (line.startsWith("::")) {
      if (line == "::CHAPTER::") {
        inText = false;
        pendingChapter = true;
        needPage = true;
      } else if (line == "::TEXT::") {
        inText = true;
        needPage = true;
      } else {
        inText = false;
      }
      continue;
    }
    if (!inText) continue;

    if (line.length() == 0) {
      cx = X0;
      cy += lh;
      if (cy > ymax) { cy = fy; needPage = true; }
      continue;
    }

    if (needPage && nPages < MAX_PAGES) {
      pages[nPages++] = lpos;
      if (pendingChapter && nChapters < MAX_CHAPTERS) {
        chapterStarts[nChapters++] = nPages - 1;
      }
      pendingChapter = false;
      cx = X0;
      cy = fy;
      needPage = false;
    }

    const char* s = line.c_str();
    int len = line.length(), i = 0;

    while (i < len) {
      while (i < len && s[i] == ' ') i++;
      if (i >= len) break;
      int ws = i;
      while (i < len && s[i] != ' ') i++;
      int wl = i - ws;

      int consumed = 0;
      while (consumed < wl) {
        const char* part = s + ws + consumed;
        int rem = wl - consumed;
        int avail = xmax - cx;
        uint16_t ww = sw(part, rem);

        if ((int)ww <= avail) {
          cx += ww;
          consumed = wl;
          break;
        }

        int split = choose_hyphen_split(part, rem, avail);
        if (split <= 0) {
          if (cx > X0) {
            cx = X0;
            cy += lh;
            if (cy > ymax && nPages < MAX_PAGES) {
              pages[nPages++] = (uint32_t)(lpos + ws + consumed);
              cx = X0;
              cy = fy;
            }
            continue;
          }
          split = hard_split(part, rem, avail);
          if (split <= 0) split = 1;
        }

        cx += sw(part, split);
        if (split < rem) cx += gw('-');
        consumed += split;

        if (consumed < wl) {
          cx = X0;
          cy += lh;
          if (cy > ymax && nPages < MAX_PAGES) {
            pages[nPages++] = (uint32_t)(lpos + ws + consumed);
            cx = X0;
            cy = fy;
          }
        }
      }

      if (cx > X0) cx += gw(' ');
    }
  }
  f.close();

  if (nPages > 0 && (nChapters == 0 || chapterStarts[0] != 0)) {
    if (nChapters < MAX_CHAPTERS) {
      for (int i = nChapters; i > 0; i--) chapterStarts[i] = chapterStarts[i - 1];
      chapterStarts[0] = 0;
      nChapters++;
    } else {
      chapterStarts[0] = 0;
    }
  }
}

static int page_for_offset(uint32_t off) {
  if (nPages <= 0) return 0;
  int p = 0;
  while (p + 1 < nPages && pages[p + 1] <= off) p++;
  return p;
}

static int chapter_for_page(int page) {
  if (nChapters <= 0) return 0;
  int idx = 0;
  for (int i = 1; i < nChapters; i++) {
    if (chapterStarts[i] <= page) idx = i;
    else break;
  }
  return idx;
}

static float book_progress() {
  if (nPages <= 1) return 1.0f;
  float p = (float)curPage / (float)(nPages - 1);
  if (p < 0.0f) p = 0.0f;
  if (p > 1.0f) p = 1.0f;
  return p;
}

static float chapter_progress(int* chapterNoOut) {
  if (nPages <= 0) {
    if (chapterNoOut) *chapterNoOut = 1;
    return 0.0f;
  }
  int cidx = chapter_for_page(curPage);
  if (chapterNoOut) *chapterNoOut = cidx + 1;

  int start = (nChapters > 0) ? chapterStarts[cidx] : 0;
  int end = (cidx + 1 < nChapters) ? (chapterStarts[cidx + 1] - 1) : (nPages - 1);
  if (end < start) end = start;
  int denom = end - start;
  if (denom <= 0) return 1.0f;

  float p = (float)(curPage - start) / (float)denom;
  if (p < 0.0f) p = 0.0f;
  if (p > 1.0f) p = 1.0f;
  return p;
}

// -----------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------

static void draw_stats_bar() {
  if (statsMode == READER_STATS_OFF) return;

  int16_t sh = stats_zone_h();
  int16_t zoneTop = display.height() - sh;

  char label[16];
  float p = 0.0f;
  if (statsMode == READER_STATS_CHAPTER) {
    int ch = 1;
    p = chapter_progress(&ch);
    snprintf(label, sizeof(label), "Ch %d", ch);
  } else {
    p = book_progress();
    snprintf(label, sizeof(label), "Book");
  }

  int16_t baseline = display.height() - 2;
  display.setFont(UI_FONT_S);
  display.setTextColor(theme_fg());

  int16_t lx = 3;
  int16_t lw = uiw(label);
  display.setCursor(lx, baseline);
  display.print(label);

  int16_t barX0 = lx + lw + 5;
  int16_t barX1 = display.width() - 3;
  if (barX1 <= barX0 + 6) return;

  int16_t barW = barX1 - barX0;
  int16_t barY = zoneTop + ((sh - 3) / 2);
  display.drawRect(barX0, barY, barW, 3, theme_fg());

  int16_t fillW = (int16_t)((barW - 2) * p + 0.5f);
  if (fillW < 0) fillW = 0;
  if (fillW > barW - 2) fillW = barW - 2;
  if (fillW > 0) display.fillRect(barX0 + 1, barY + 1, fillW, 1, theme_fg());
}

static void renderPage() {
  if (nPages == 0) return;
  File f = LittleFS.open(fpath, "r");
  if (!f) return;
  f.seek(pages[curPage]);

  display.setFont(rf());
  display.setTextColor(theme_fg());

  int16_t lh   = LH();
  int16_t fy   = FY();
  int16_t xmax = XMAX();
  int16_t ymax = YMAX();
  int16_t cx = X0, cy = fy;
  bool stop = false;
  while (f.available() && !stop) {
    String line = f.readStringUntil('\n');
    if (line.endsWith("\r")) line.remove(line.length() - 1);
    if (line.startsWith("::")) break;

    if (line.length() == 0) {
      cx = X0;
      cy += lh;
      if (cy > ymax) break;
      continue;
    }

    const char* s = line.c_str();
    int len = line.length(), i = 0;

    while (i < len) {
      while (i < len && s[i] == ' ') i++;
      if (i >= len) break;
      int ws = i;
      while (i < len && s[i] != ' ') i++;
      int wl = i - ws;

      int consumed = 0;
      while (consumed < wl) {
        const char* part = s + ws + consumed;
        int rem = wl - consumed;
        int avail = xmax - cx;
        uint16_t ww = sw(part, rem);

        if ((int)ww <= avail) {
          display.setCursor(cx, cy);
          print_span(part, rem);
          cx += ww;
          consumed = wl;
          break;
        }

        int split = choose_hyphen_split(part, rem, avail);
        if (split <= 0) {
          if (cx > X0) {
            cx = X0;
            cy += lh;
            if (cy > ymax) { stop = true; break; }
            continue;
          }
          split = hard_split(part, rem, avail);
          if (split <= 0) split = 1;
        }

        display.setCursor(cx, cy);
        print_span(part, split);
        cx += sw(part, split);
        if (split < rem) {
          display.print('-');
          cx += gw('-');
        }
        consumed += split;

        if (consumed < wl) {
          cx = X0;
          cy += lh;
          if (cy > ymax) { stop = true; break; }
        }
      }
      if (stop) break;

      if (cx > X0) cx += gw(' ');
    }
  }
  f.close();
  draw_stats_bar();
}

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

static int fullRefreshEvery = 10;

static void rebuild_keep_offset(bool forceFull) {
  if (nPages <= 0) return;
  uint32_t off = pages[curPage];
  buildIndex();
  saveIndex(); // update cached index whenever settings force a rebuild
  curPage = page_for_offset(off);
  if (forceFull) turnsSince = fullRefreshEvery;
}

void reader_set_refresh_interval(int n) {
  if (n >= 1) fullRefreshEvery = n;
}

void reader_set_font(int family, int sizeIdx) {
  fontFamily  = family;
  fontSizeIdx = sizeIdx;
  // No immediate rebuild: settingsSig() mismatch in tryLoadIndex() handles it
  // the next time reader_open() is called, with a proper loading screen.
}

void reader_set_hyphenation(bool enabled) {
  hyphenationEnabled = enabled;
}

void reader_set_stats_mode(int mode) {
  if (mode < READER_STATS_OFF || mode > READER_STATS_BOOK) mode = READER_STATS_OFF;
  statsMode    = mode;
  prefsLoaded  = true;
}

void reader_draw(bool fullRefresh) {
  if (fullRefresh || turnsSince >= fullRefreshEvery) {
    display.setFullWindow();
    turnsSince = 0;
  } else {
    display.setPartialWindow(0, 0, display.width(), display.height());
  }
  display.firstPage();
  do {
    display.fillScreen(theme_bg());
    renderPage();
  } while (display.nextPage());
}

bool reader_open(const char* path) {
  load_prefs_once();

  // Allocate page index on the heap (freed in reader_close so WiFi has room).
  if (!pages) pages = (uint32_t*)malloc(MAX_PAGES * sizeof(uint32_t));
  if (!pages) return false;

  strncpy(fpath, path, sizeof(fpath) - 1);
  fpath[sizeof(fpath) - 1] = 0;
  nPages    = 0;
  nChapters = 0;
  curPage   = 0;

  if (!tryLoadIndex()) {
    // First open for this book+settings combination: show loading screen then build.
    showLoadingScreen();
    buildIndex();
    saveIndex();
  }

  return nPages > 0;
}

void reader_close() {
  if (pages) { free(pages); pages = nullptr; }
  nPages = 0;
  curPage = 0;
}
int         reader_page_count()   { return nPages; }
int         reader_current_page() { return curPage; }
const char* reader_path()         { return fpath; }
void        reader_goto(int page) { if (page >= 0 && page < nPages) { curPage = page; turnsSince = fullRefreshEvery; } }
void        reader_go_next()      { if (curPage < nPages - 1) { curPage++; turnsSince++; } }
void        reader_go_prev()      { if (curPage > 0)           { curPage--; turnsSince++; } }
