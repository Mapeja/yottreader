#include "storage.h"
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

static Preferences prefs;

static uint32_t fnv1a(const char* s) {
  uint32_t h = 2166136261u;
  while (*s) {
    h ^= (uint8_t)*s++;
    h *= 16777619u;
  }
  return h;
}

static void progressKey(const char* path, char out[12]) {
  snprintf(out, 12, "p%08lX", (unsigned long)fnv1a(path));
}

static void pageKey(const char* path, char out[12]) {
  snprintf(out, 12, "g%08lX", (unsigned long)fnv1a(path));
}

static void seqKey(const char* path, char out[12]) {
  snprintf(out, 12, "s%08lX", (unsigned long)fnv1a(path));
}

void storage_save_position(const char* path, int page, int totalPages) {
  prefs.begin("yott", false);
  prefs.putString("path", path);
  prefs.putInt("page", page);
  int pct = 0;
  if (totalPages > 1) {
    pct = (int)((long)page * 100L / (long)(totalPages - 1));
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
  }
  char key[12];
  progressKey(path, key);
  prefs.putInt(key, pct);
  pageKey(path, key);
  prefs.putInt(key, page);
  // Monotonic sequence counter so library can sort by recency.
  uint32_t seq = prefs.getUInt("seq", 0) + 1;
  prefs.putUInt("seq", seq);
  seqKey(path, key);
  prefs.putUInt(key, seq);
  prefs.end();
}

bool storage_load_position(char* pathOut, size_t pathLen, int* pageOut) {
  prefs.begin("yott", true);
  bool has = prefs.isKey("path");
  if (has) {
    String p = prefs.getString("path", "");
    strncpy(pathOut, p.c_str(), pathLen - 1);
    pathOut[pathLen - 1] = 0;
    *pageOut = prefs.getInt("page", 0);
  }
  prefs.end();
  return has;
}

int storage_get_book_progress(const char* path) {
  prefs.begin("yott", true);
  char key[12];
  progressKey(path, key);
  int pct = prefs.isKey(key) ? prefs.getInt(key, 0) : -1;
  prefs.end();
  return pct;
}

int storage_get_book_page(const char* path) {
  prefs.begin("yott", true);
  char key[12];
  pageKey(path, key);
  int page = prefs.isKey(key) ? prefs.getInt(key, 0) : 0;
  prefs.end();
  return page;
}

uint32_t storage_get_book_seq(const char* path) {
  prefs.begin("yott", true);
  char key[12];
  seqKey(path, key);
  uint32_t seq = prefs.getUInt(key, 0);
  prefs.end();
  return seq;
}

void storage_clear_position() {
  prefs.begin("yott", false);
  prefs.clear();
  prefs.end();
}
