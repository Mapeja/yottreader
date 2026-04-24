#pragma once
#include <Arduino.h>

enum ReaderStatsMode {
  READER_STATS_OFF = 0,
  READER_STATS_CHAPTER = 1,
  READER_STATS_BOOK = 2,
};

bool        reader_open(const char* path);
void        reader_close();
int         reader_page_count();
int         reader_current_page();
const char* reader_path();
void        reader_goto(int page);
void        reader_go_next();
void        reader_go_prev();
void        reader_draw(bool fullRefresh = false);
void        reader_set_refresh_interval(int n);
void        reader_set_font(int family, int sizeIdx);
void        reader_set_hyphenation(bool enabled);
void        reader_set_stats_mode(int mode);
